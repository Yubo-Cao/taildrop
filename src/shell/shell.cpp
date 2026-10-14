// TailDropShell.dll: the "Send with TailDrop" Explorer command.
//
// Works in both the Windows 11 context menu (registered through the sparse
// package in package/AppxManifest.xml) and the classic menu (HKCU
// ExplorerCommandHandler verbs). It does no work itself: it hands the
// selection to TailDrop.exe over WM_COPYDATA.
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <new>
#include <string>
#include <vector>

#include "../common/shared.h"
#include "resource.h"

namespace {

HMODULE g_module = nullptr;
LONG g_objects = 0;
LONG g_locks = 0;

std::wstring ModulePath() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(g_module, buf, ARRAYSIZE(buf));
  return std::wstring(buf, n);
}

std::wstring ModuleDir() {
  std::wstring p = ModulePath();
  size_t i = p.find_last_of(L'\\');
  return i == std::wstring::npos ? p : p.substr(0, i);
}

bool MenuIsDark() {
  DWORD v = 1, size = sizeof(v);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size);
  return v == 0;
}

int DeviceIcon(const std::wstring& kind) {
  static const struct {
    const wchar_t* kind;
    int id;
  } map[] = {{L"pc", IDI_DEV_PC},         {L"laptop", IDI_DEV_LAPTOP}, {L"phone", IDI_DEV_PHONE},
             {L"tablet", IDI_DEV_TABLET}, {L"server", IDI_DEV_SERVER}, {L"more", IDI_DEV_MORE}};
  int id = IDI_DEV_OTHER;
  for (const auto& m : map)
    if (kind == m.kind) id = m.id;
  return id + (MenuIsDark() ? 1 : 0);
}

HRESULT IconString(int id, LPWSTR* out) { return SHStrDupW((ModulePath() + L",-" + std::to_wstring(id)).c_str(), out); }

std::vector<std::wstring> SelectedPaths(IShellItemArray* items) {
  std::vector<std::wstring> out;
  DWORD n = 0;
  if (!items || FAILED(items->GetCount(&n))) return out;
  for (DWORD i = 0; i < n; ++i) {
    IShellItem* it = nullptr;
    if (FAILED(items->GetItemAt(i, &it))) continue;
    PWSTR path = nullptr;
    if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &path))) out.emplace_back(path);
    CoTaskMemFree(path);
    it->Release();
  }
  return out;
}

HWND FindOrStartHost() {
  HWND host = FindWindowW(td::kHostClass, nullptr);
  if (host) return host;
  std::wstring exe = ModuleDir() + L"\\" + td::kExeName;
  std::wstring cmd = L"\"" + exe + L"\" --background";
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
  AllowSetForegroundWindow(pi.dwProcessId);
  WaitForInputIdle(pi.hProcess, 5000);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  for (int i = 0; i < 50 && !host; ++i) {
    host = FindWindowW(td::kHostClass, nullptr);
    if (!host) Sleep(100);
  }
  return host;
}

HRESULT Dispatch(ULONG_PTR command, const std::vector<std::wstring>& parts) {
  HWND host = FindOrStartHost();
  if (!host) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
  DWORD pid = 0;
  GetWindowThreadProcessId(host, &pid);
  AllowSetForegroundWindow(pid);
  std::wstring payload = td::PackStrings(parts);
  COPYDATASTRUCT cds{command, static_cast<DWORD>(payload.size() * sizeof(wchar_t)), payload.data()};
  DWORD_PTR result = 0;
  SendMessageTimeoutW(host, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 3000, &result);
  return S_OK;
}

class CommandBase : public IExplorerCommand {
 public:
  CommandBase() { InterlockedIncrement(&g_objects); }
  virtual ~CommandBase() { InterlockedDecrement(&g_objects); }

  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IExplorerCommand) {
      *ppv = static_cast<IExplorerCommand*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
  IFACEMETHODIMP_(ULONG) Release() override {
    LONG r = InterlockedDecrement(&ref_);
    if (!r) delete this;
    return r;
  }

  IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* tip) override {
    *tip = nullptr;
    return E_NOTIMPL;
  }
  IFACEMETHODIMP GetCanonicalName(GUID* guid) override {
    *guid = GUID_NULL;
    return S_OK;
  }
  IFACEMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* state) override {
    *state = ECS_ENABLED;
    return S_OK;
  }
  IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override {
    *flags = ECF_DEFAULT;
    return S_OK;
  }
  IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** out) override {
    *out = nullptr;
    return E_NOTIMPL;
  }

 private:
  LONG ref_ = 1;
};

// One favorite device, or "Other devices..." when stableId is empty.
class DeviceCommand : public CommandBase {
 public:
  DeviceCommand(td::Favorite fav, bool other) : fav_(std::move(fav)), other_(other) {}

  IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* name) override {
    if (other_) return SHStrDupW(fav_.name.c_str(), name);
    std::wstring t = fav_.name + (fav_.online ? L"" : L" (offline)");
    return SHStrDupW(t.c_str(), name);
  }
  IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override {
    return IconString(DeviceIcon(other_ ? L"more" : fav_.kind), icon);
  }
  IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
    std::vector<std::wstring> paths = SelectedPaths(items);
    if (paths.empty()) return S_OK;
    if (other_) return Dispatch(td::kCopyPick, paths);
    paths.insert(paths.begin(), fav_.stableId);
    return Dispatch(td::kCopySend, paths);
  }

 private:
  td::Favorite fav_;
  bool other_;
};

class CommandEnum : public IEnumExplorerCommand {
 public:
  explicit CommandEnum(std::vector<IExplorerCommand*> cmds) : cmds_(std::move(cmds)) {
    InterlockedIncrement(&g_objects);
  }
  ~CommandEnum() {
    for (auto* c : cmds_) c->Release();
    InterlockedDecrement(&g_objects);
  }
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IEnumExplorerCommand) {
      *ppv = static_cast<IEnumExplorerCommand*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
  IFACEMETHODIMP_(ULONG) Release() override {
    LONG r = InterlockedDecrement(&ref_);
    if (!r) delete this;
    return r;
  }
  IFACEMETHODIMP Next(ULONG count, IExplorerCommand** out, ULONG* fetched) override {
    ULONG n = 0;
    while (n < count && pos_ < cmds_.size()) {
      out[n] = cmds_[pos_++];
      out[n]->AddRef();
      ++n;
    }
    if (fetched) *fetched = n;
    return n == count ? S_OK : S_FALSE;
  }
  IFACEMETHODIMP Skip(ULONG count) override {
    pos_ = std::min(cmds_.size(), pos_ + count);
    return pos_ < cmds_.size() ? S_OK : S_FALSE;
  }
  IFACEMETHODIMP Reset() override {
    pos_ = 0;
    return S_OK;
  }
  IFACEMETHODIMP Clone(IEnumExplorerCommand** out) override {
    *out = nullptr;
    return E_NOTIMPL;
  }

 private:
  LONG ref_ = 1;
  std::vector<IExplorerCommand*> cmds_;
  size_t pos_ = 0;
};

class RootCommand : public CommandBase {
 public:
  IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* name) override { return SHStrDupW(L"Send with TailDrop", name); }
  IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override { return IconString(IDI_SHELL_APP, icon); }
  IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
    *state = ECS_HIDDEN;
    DWORD n = 0;
    if (!items || FAILED(items->GetCount(&n)) || n == 0) return S_OK;
    // Only real files and folders can be sent.
    SFGAOF attrs = 0;
    if (SUCCEEDED(items->GetAttributes(SIATTRIBFLAGS_AND, SFGAO_FILESYSTEM, &attrs)) && (attrs & SFGAO_FILESYSTEM))
      *state = ECS_ENABLED;
    return S_OK;
  }
  IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override {
    *flags = ECF_HASSUBCOMMANDS;
    return S_OK;
  }
  IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
    std::vector<std::wstring> paths = SelectedPaths(items);
    return paths.empty() ? S_OK : Dispatch(td::kCopyPick, paths);
  }
  IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** out) override {
    std::vector<IExplorerCommand*> cmds;
    std::vector<td::Favorite> favs = td::LoadFavorites();
    for (auto& f : favs) cmds.push_back(new (std::nothrow) DeviceCommand(f, false));
    td::Favorite other;
    other.name = favs.empty() ? L"Choose a device…" : L"Other devices…";
    cmds.push_back(new (std::nothrow) DeviceCommand(other, true));
    cmds.erase(std::remove(cmds.begin(), cmds.end(), nullptr), cmds.end());
    // Let the app refresh favorites' online state for next time.
    if (HWND host = FindWindowW(td::kHostClass, nullptr)) PostMessageW(host, td::kMsgRefreshDevices, 0, 0);
    *out = new (std::nothrow) CommandEnum(std::move(cmds));
    return *out ? S_OK : E_OUTOFMEMORY;
  }
};

class Factory : public IClassFactory {
 public:
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
      *ppv = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }
  IFACEMETHODIMP_(ULONG) Release() override { return 1; }
  IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    *ppv = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    auto* cmd = new (std::nothrow) RootCommand();
    if (!cmd) return E_OUTOFMEMORY;
    HRESULT hr = cmd->QueryInterface(riid, ppv);
    cmd->Release();
    return hr;
  }
  IFACEMETHODIMP LockServer(BOOL lock) override {
    if (lock) InterlockedIncrement(&g_locks);
    else InterlockedDecrement(&g_locks);
    return S_OK;
  }
};

Factory g_factory;

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = module;
    DisableThreadLibraryCalls(module);
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (clsid != td::kClsidSendCommand) {
    *ppv = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_objects == 0 && g_locks == 0 ? S_OK : S_FALSE; }
