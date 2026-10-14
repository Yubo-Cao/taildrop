#include "util.h"

#include <knownfolders.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <cstdarg>
#include <memory>

namespace util {

std::wstring Widen(std::string_view s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

std::string Narrow(std::wstring_view s) {
  if (s.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring FormatSize(int64_t bytes) {
  if (bytes < 0) return L"";
  const wchar_t* units[] = {L"bytes", L"KB", L"MB", L"GB", L"TB"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1000 && u < 4) {
    v /= 1024;
    ++u;
  }
  wchar_t buf[64];
  if (u == 0) swprintf_s(buf, L"%lld %s", bytes, bytes == 1 ? L"byte" : L"bytes");
  else swprintf_s(buf, v < 10 ? L"%.1f %s" : L"%.0f %s", v, units[u]);
  return buf;
}

std::wstring FormatRate(double bps) {
  if (bps <= 0) return L"";
  return FormatSize(static_cast<int64_t>(bps)) + L"/s";
}

std::wstring KnownFolder(const GUID& id) {
  PWSTR p = nullptr;
  std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p))) out = p;
  CoTaskMemFree(p);
  return out;
}

std::wstring AppDataDir() {
  std::wstring parent = Join(KnownFolder(FOLDERID_LocalAppData), L"YuboCao");
  CreateDirectoryW(parent.c_str(), nullptr);
  std::wstring dir = Join(parent, L"TailDrop");
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring InboxDir() {
  std::wstring dir = Join(AppDataDir(), L"Inbox");
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring TempDir() {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  std::wstring dir = Join(buf, L"TailDrop");
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring ExePath() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n < buf.size()) {
      buf.resize(n);
      return buf;
    }
    buf.resize(buf.size() * 2);
  }
}

std::wstring ExeDir() { return ParentDir(ExePath()); }

std::wstring FileName(const std::wstring& path) {
  size_t i = path.find_last_of(L"\\/");
  return i == std::wstring::npos ? path : path.substr(i + 1);
}

std::wstring Stem(const std::wstring& name) {
  size_t i = name.find_last_of(L'.');
  return i == std::wstring::npos || i == 0 ? name : name.substr(0, i);
}

std::wstring Extension(const std::wstring& name) {
  size_t i = name.find_last_of(L'.');
  return i == std::wstring::npos || i == 0 ? L"" : name.substr(i);
}

std::wstring ParentDir(const std::wstring& path) {
  size_t i = path.find_last_of(L"\\/");
  return i == std::wstring::npos ? L"" : path.substr(0, i);
}

std::wstring Join(const std::wstring& dir, const std::wstring& name) {
  if (dir.empty()) return name;
  if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
  return dir + L"\\" + name;
}

std::wstring FolderDisplayName(const std::wstring& dir) {
  struct Known {
    const GUID* id;
  } known[] = {{&FOLDERID_Downloads}, {&FOLDERID_Desktop}, {&FOLDERID_Documents}, {&FOLDERID_Pictures},
               {&FOLDERID_Videos},    {&FOLDERID_Music}};
  for (const auto& k : known) {
    if (_wcsicmp(KnownFolder(*k.id).c_str(), dir.c_str()) == 0) {
      IShellItem* item = nullptr;
      std::wstring name;
      if (SUCCEEDED(SHGetKnownFolderItem(*k.id, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&item)))) {
        PWSTR p = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &p))) name = p;
        CoTaskMemFree(p);
        item->Release();
      }
      if (!name.empty()) return name;
    }
  }
  std::wstring leaf = FileName(dir);
  return leaf.empty() ? dir : leaf;
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool IsDirectory(const std::wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int64_t FileSize(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return -1;
  return (static_cast<int64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}

std::wstring UniquePath(const std::wstring& dir, const std::wstring& name) {
  std::wstring candidate = Join(dir, name);
  if (!Exists(candidate)) return candidate;
  std::wstring stem = Stem(name), ext = Extension(name);
  for (int i = 2; i < 10000; ++i) {
    candidate = Join(dir, stem + L" (" + std::to_wstring(i) + L")" + ext);
    if (!Exists(candidate)) return candidate;
  }
  return candidate;
}

std::wstring SafeFileName(std::wstring name) {
  for (wchar_t& c : name)
    if (c < 32 || wcschr(L"<>:\"/\\|?*", c)) c = L'_';
  while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) name.pop_back();
  if (name.empty()) name = L"file";
  return name;
}

std::wstring FileTypeName(const std::wstring& name) {
  SHFILEINFOW info{};
  if (!SHGetFileInfoW(name.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                      SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES))
    return L"";
  return info.szTypeName;
}

bool IsProcessRunning(const wchar_t* exeName) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W pe{sizeof(pe)};
  bool found = false;
  for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
    found = _wcsicmp(pe.szExeFile, exeName) == 0;
  CloseHandle(snap);
  return found;
}

void RunAsync(std::function<void()> fn) {
  auto* heap = new std::function<void()>(std::move(fn));
  HANDLE t = CreateThread(
      nullptr, 256 * 1024,
      [](void* p) -> DWORD {
        std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()>*>(p));
        (*f)();
        return 0;
      },
      heap, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (t) CloseHandle(t);
  else delete heap;
}

bool MoveFileTo(const std::wstring& from, const std::wstring& to) {
  return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool RecycleFile(const std::wstring& path) {
  IFileOperation* op = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))) return false;
  op->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_RECYCLEONDELETE);
  IShellItem* item = nullptr;
  bool ok = false;
  if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
    ok = SUCCEEDED(op->DeleteItem(item, nullptr)) && SUCCEEDED(op->PerformOperations());
    item->Release();
  }
  op->Release();
  return ok;
}

void OpenPath(const std::wstring& path) {
  SHELLEXECUTEINFOW sei{sizeof(sei)};
  sei.fMask = SEE_MASK_INVOKEIDLIST;
  sei.lpFile = path.c_str();
  sei.nShow = SW_SHOWNORMAL;
  ShellExecuteExW(&sei);
}

void RevealInExplorer(const std::wstring& path) {
  PIDLIST_ABSOLUTE pidl = nullptr;
  if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) {
    SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    CoTaskMemFree(pidl);
  }
}

std::wstring PickFolder(HWND owner, const std::wstring& initial, const wchar_t* title) {
  IFileOpenDialog* dlg = nullptr;
  std::wstring out;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dlg->SetTitle(title);
  IShellItem* init = nullptr;
  if (!initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&init)))) {
    dlg->SetFolder(init);
    init->Release();
  }
  if (SUCCEEDED(dlg->Show(owner))) {
    IShellItem* res = nullptr;
    if (SUCCEEDED(dlg->GetResult(&res))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(res->GetDisplayName(SIGDN_FILESYSPATH, &p))) out = p;
      CoTaskMemFree(p);
      res->Release();
    }
  }
  dlg->Release();
  return out;
}

std::vector<std::wstring> PickFiles(HWND owner, const wchar_t* title) {
  std::vector<std::wstring> out;
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
  dlg->SetTitle(title);
  if (SUCCEEDED(dlg->Show(owner))) {
    IShellItemArray* arr = nullptr;
    if (SUCCEEDED(dlg->GetResults(&arr))) {
      DWORD n = 0;
      arr->GetCount(&n);
      for (DWORD i = 0; i < n; ++i) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(arr->GetItemAt(i, &it))) {
          PWSTR p = nullptr;
          if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) out.emplace_back(p);
          CoTaskMemFree(p);
          it->Release();
        }
      }
      arr->Release();
    }
  }
  dlg->Release();
  return out;
}

bool IsWindows11() {
  static int cached = -1;
  if (cached < 0) {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW v{sizeof(v)};
    cached = fn && fn(&v) == 0 && v.dwBuildNumber >= 22000;
  }
  return cached == 1;
}

void Log(const wchar_t* fmt, ...) {
  wchar_t buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
  va_end(ap);
  if (n < 0) n = static_cast<int>(wcslen(buf));
  OutputDebugStringW(buf);
  OutputDebugStringW(L"\n");
  static int enabled = -1;
  if (enabled < 0) enabled = GetEnvironmentVariableW(L"TAILDROP_LOG", nullptr, 0) > 0;
  if (!enabled) return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t line[1200];
  swprintf_s(line, L"%02d:%02d:%02d.%03d %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
  std::string utf8 = Narrow(line);
  HANDLE f = CreateFileW(Join(AppDataDir(), L"taildrop.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD w;
  WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &w, nullptr);
  CloseHandle(f);
}

void TrimMemory() {
  HeapCompact(GetProcessHeap(), 0);
  EmptyWorkingSet(GetCurrentProcess());
}

}  // namespace util
