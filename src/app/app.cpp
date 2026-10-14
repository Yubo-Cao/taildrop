#include "app.h"

#include <commctrl.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <set>

#include "json.h"
#include "localapi.h"
#include "resource.h"
#include "util.h"
#include "views.h"
#include "zip.h"

using util::Narrow;
using util::Widen;

wchar_t KindGlyph(DeviceKind k) {
  switch (k) {
    case DeviceKind::PC: return ui::glyph::kPC;
    case DeviceKind::Laptop: return ui::glyph::kLaptop;
    case DeviceKind::Phone: return ui::glyph::kPhone;
    case DeviceKind::Tablet: return ui::glyph::kTablet;
    case DeviceKind::Server: return ui::glyph::kServer;
    default: return ui::glyph::kDevices;
  }
}

const wchar_t* KindKey(DeviceKind k) {
  switch (k) {
    case DeviceKind::PC: return L"pc";
    case DeviceKind::Laptop: return L"laptop";
    case DeviceKind::Phone: return L"phone";
    case DeviceKind::Tablet: return L"tablet";
    case DeviceKind::Server: return L"server";
    default: return L"other";
  }
}

namespace {

// watch-ipn-bus mask: initial state, per-peer changes as patches, no netmaps.
constexpr uint64_t kBusMask = (1 << 1) | (1 << 12) | (1 << 13) | (1 << 15);

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

DeviceKind InferKind(const std::string& os, const std::string& model, const std::string& name) {
  std::string o = Lower(os), m = Lower(model), n = Lower(name);
  if (o == "ios" || o == "ipados") {
    if (m.rfind("ipad", 0) == 0 || n.find("ipad") != std::string::npos) return DeviceKind::Tablet;
    return DeviceKind::Phone;
  }
  if (o == "android") return n.find("tab") != std::string::npos ? DeviceKind::Tablet : DeviceKind::Phone;
  if (o == "macos" || o == "darwin") return DeviceKind::Laptop;
  if (o == "windows") return n.find("laptop") != std::string::npos ? DeviceKind::Laptop : DeviceKind::PC;
  if (o == "linux" || o == "freebsd" || o == "openbsd") return DeviceKind::Server;
  return DeviceKind::Other;
}

Conn MapState(int64_t s) {
  switch (s) {
    case 1: return Conn::OtherUser;
    case 2: return Conn::NeedsLogin;
    case 3: return Conn::NeedsMachineAuth;
    case 4: return Conn::Stopped;
    case 6: return Conn::Running;
    default: return Conn::Starting;
  }
}

Conn MapStateName(const std::string& s) {
  if (s == "Running") return Conn::Running;
  if (s == "NeedsLogin") return Conn::NeedsLogin;
  if (s == "NeedsMachineAuth") return Conn::NeedsMachineAuth;
  if (s == "Stopped") return Conn::Stopped;
  if (s == "InUseOtherUser") return Conn::OtherUser;
  return Conn::Starting;
}

std::string FirstLabel(const std::string& dns) {
  size_t dot = dns.find('.');
  return dot == std::string::npos ? dns : dns.substr(0, dot);
}

void BusLoop(HWND host, HANDLE stop) {
  const std::string path = "/localapi/v0/watch-ipn-bus?mask=" + std::to_string(kBusMask);
  for (;;) {
    {
      lapi::Conn c(stop);
      int status = 0;
      if (c.Open() && c.SendHead("GET", path, -1) && c.ReadHead(status) && status == 200) {
        std::string line;
        while (c.ReadLine(line)) {
          json::Value v;
          if (line.empty() || !json::Parse(line, v)) continue;
          const json::Value& st = v["State"];
          if (st.type == json::Value::Number) PostMessageW(host, WM_APP_CONN, static_cast<WPARAM>(MapState(st.i64())), 0);
          const json::Value& inc = v["IncomingFiles"];
          if (inc.type == json::Value::Array) {
            auto* list = new std::vector<Partial>;
            for (const json::Value& f : inc.items) {
              Partial p;
              p.name = f["Name"].str();
              p.finalPath = f["FinalPath"].str();
              p.declared = f["DeclaredSize"].i64(-1);
              p.received = f["Received"].i64();
              p.done = f["Done"].boolean();
              list->push_back(std::move(p));
            }
            if (!PostMessageW(host, WM_APP_INCOMING, 0, reinterpret_cast<LPARAM>(list))) delete list;
          }
          if (v.has("FilesWaiting")) PostMessageW(host, WM_APP_FILESWAITING, 0, 0);
          bool dirty = v.has("PeersChanged") || v.has("PeersRemoved") || v.has("LoginFinished") || v.has("SelfChange");
          for (const json::Value& p : v["PeerChangedPatch"].items)
            if (p.has("Online")) dirty = true;
          if (dirty) PostMessageW(host, WM_APP_PEERSDIRTY, 0, 0);
        }
      }
    }
    if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return;
    PostMessageW(host, WM_APP_CONN, static_cast<WPARAM>(Conn::NoService), 0);
    if (WaitForSingleObject(stop, 3000) == WAIT_OBJECT_0) return;
  }
}

DeviceSnapshot* FetchDevices() {
  auto* snap = new DeviceSnapshot;
  lapi::Result st = lapi::Get("/localapi/v0/status?peers=false");
  if (!st.connected) return snap;
  snap->connected = true;
  json::Value v;
  if (st.ok() && json::Parse(st.body, v)) {
    snap->backendState = v["BackendState"].str();
    std::string dns = v["Self"]["DNSName"].str();
    snap->selfName = Widen(dns.empty() ? v["Self"]["HostName"].str() : FirstLabel(dns));
    snap->tailnet = Widen(v["CurrentTailnet"]["Name"].str());
  }
  lapi::Result ft = lapi::Get("/localapi/v0/file-targets");
  if (ft.ok() && json::Parse(ft.body, v)) {
    for (const json::Value& t : v.items) {
      const json::Value& n = t["Node"];
      Device d;
      d.stableId = n["StableID"].str();
      if (d.stableId.empty()) continue;
      d.nodeId = n["ID"].i64();
      std::string dns = n["Name"].str();
      if (!dns.empty() && dns.back() == '.') dns.pop_back();
      std::string name = n["ComputedName"].str();
      if (name.empty()) name = FirstLabel(dns);
      d.name = Widen(name);
      d.dns = Widen(dns);
      d.os = Widen(n["Hostinfo"]["OS"].str());
      d.kind = InferKind(n["Hostinfo"]["OS"].str(), n["Hostinfo"]["DeviceModel"].str(), name);
      d.online = n["Online"].boolean(false);
      snap->devices.push_back(std::move(d));
    }
  }
  return snap;
}

// ---------------------------------------------------------------------------
// Drag-to-tray detection: a low-level mouse hook on its own thread notices a
// drag that reaches the tray icon, because notification-area icons cannot be
// OLE drop targets themselves.

HWND g_hookHost = nullptr;
HANDLE g_hookThread = nullptr;
DWORD g_hookThreadId = 0;
SRWLOCK g_rectLock = SRWLOCK_INIT;
RECT g_iconRect{};
bool g_down = false, g_dragging = false, g_over = false;
POINT g_downPt{};

LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION) {
    auto* m = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
    switch (wp) {
      case WM_LBUTTONDOWN:
        g_down = true;
        g_dragging = g_over = false;
        g_downPt = m->pt;
        break;
      case WM_LBUTTONUP:
        if (g_dragging) PostMessageW(g_hookHost, WM_APP_TRAYDRAG, 3, 0);
        g_down = g_dragging = g_over = false;
        break;
      case WM_MOUSEMOVE:
        if (g_down) {
          if (!g_dragging && (abs(m->pt.x - g_downPt.x) > 12 || abs(m->pt.y - g_downPt.y) > 12)) {
            g_dragging = true;
            PostMessageW(g_hookHost, WM_APP_TRAYDRAG, 1, 0);
          }
          if (g_dragging) {
            AcquireSRWLockShared(&g_rectLock);
            bool over = PtInRect(&g_iconRect, m->pt) != FALSE;
            ReleaseSRWLockShared(&g_rectLock);
            if (over && !g_over) PostMessageW(g_hookHost, WM_APP_TRAYDRAG, 2, 0);
            g_over = over;
          }
        }
        break;
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

DWORD WINAPI HookThread(void*) {
  HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, GetModuleHandleW(nullptr), 0);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
  }
  if (hook) UnhookWindowsHookEx(hook);
  return 0;
}

// ---------------------------------------------------------------------------
// Sending.

struct OutFile {
  std::wstring path, name;
  int64_t size;
  std::unique_ptr<ZipStream> zip;  // folders are streamed as .zip
};

std::wstring FriendlySendError(std::string body, const std::wstring& device) {
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ')) body.pop_back();
  std::string l = Lower(body);
  if (l.find("offline") != std::string::npos || l.find("no route") != std::string::npos ||
      l.find("timeout") != std::string::npos || l.find("unreachable") != std::string::npos ||
      l.find("connection refused") != std::string::npos)
    return device + L" isn't reachable right now. Make sure Tailscale is open on it.";
  if (l.find("not a valid") != std::string::npos || l.find("unknown peer") != std::string::npos ||
      l.find("not found") != std::string::npos)
    return device + L" can't receive Taildrop files.";
  if (body.size() > 160) body = body.substr(0, 160) + "...";
  return body.empty() ? L"Sending failed." : Widen(body);
}

void SendWorker(HWND host, int id, std::string stableId, std::wstring deviceName, std::vector<std::wstring> paths,
                HANDLE cancel) {
  JobProgress pr{JobState::Preparing, 0, 0, 0, 0, L"", L""};
  ULONGLONG lastPost = 0;
  auto post = [&](bool force) {
    ULONGLONG now = GetTickCount64();
    if (!force && now - lastPost < 120) return;
    lastPost = now;
    auto* p = new JobProgress(pr);
    if (!PostMessageW(host, WM_APP_JOBPROGRESS, id, reinterpret_cast<LPARAM>(p))) delete p;
  };
  auto cancelled = [&] { return WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0; };

  std::vector<OutFile> files;
  for (const auto& p : paths) {
    if (util::IsDirectory(p)) {
      pr.current = L"Collecting " + util::FileName(p);
      post(true);
      auto zip = std::make_unique<ZipStream>();
      if (!zip->Prepare(p)) {
        pr.state = JobState::Failed;
        pr.error = L"Couldn't read the folder " + util::FileName(p) + L".";
        post(true);
        return;
      }
      int64_t size = zip->size();
      files.push_back({p, util::FileName(p) + L".zip", size, std::move(zip)});
    } else {
      int64_t size = util::FileSize(p);
      if (size < 0) continue;
      files.push_back({p, util::FileName(p), size, nullptr});
    }
  }
  for (const auto& f : files) pr.total += f.size;
  pr.fileCount = static_cast<int>(files.size());
  pr.state = JobState::Sending;
  if (files.empty()) {
    pr.state = JobState::Failed;
    pr.error = L"There was nothing to send.";
  }

  std::vector<char> buf(256 * 1024);
  for (size_t i = 0; i < files.size() && pr.state == JobState::Sending; ++i) {
    OutFile& f = files[i];
    pr.fileIndex = static_cast<int>(i);
    pr.current = f.name;
    post(true);
    lapi::Conn c(cancel);
    int status = 0;
    bool ok = c.Open();
    if (!ok) pr.error = L"Tailscale isn't running on this PC.";
    std::string url = "/localapi/v0/file-put/" + lapi::EscapeSegment(stableId) + "/" +
                      lapi::EscapeSegment(Narrow(f.name));
    if (ok) ok = c.SendHead("PUT", url, f.size, "application/octet-stream");
    auto sink = [&](const void* data, size_t n) {
      if (!c.Write(data, n)) return false;
      pr.sent += static_cast<int64_t>(n);
      post(false);
      return true;
    };
    if (ok && f.zip) {
      ok = f.zip->Write(sink, &pr.error);
    } else if (ok) {
      HANDLE in = CreateFileW(f.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
      if (in == INVALID_HANDLE_VALUE) {
        ok = false;
        pr.error = L"Couldn't open " + f.name + L".";
      }
      int64_t left = f.size;
      while (ok && left > 0) {
        DWORD n = 0;
        if (!ReadFile(in, buf.data(), static_cast<DWORD>(std::min<int64_t>(left, buf.size())), &n, nullptr) || !n) {
          ok = false;
          pr.error = L"Couldn't read " + f.name + L".";
          break;
        }
        ok = sink(buf.data(), n);
        left -= n;
      }
      if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
    }
    std::string body;
    if (ok) {
      ok = c.ReadHead(status);
      c.ReadAll(body, 64 * 1024);
    }
    if (cancelled()) {
      pr.state = JobState::Cancelled;
      break;
    }
    if (!ok || status < 200 || status >= 300) {
      pr.state = JobState::Failed;
      if (pr.error.empty())
        pr.error = status ? FriendlySendError(body, deviceName)
                          : deviceName + L" didn't accept the file. Is Tailscale open on it?";
      break;
    }
  }
  if (pr.state == JobState::Sending) {
    pr.state = JobState::Done;
    pr.sent = pr.total;
  }
  post(true);
}

}  // namespace

// ---------------------------------------------------------------------------

App& App::Get() {
  static App app;
  return app;
}

std::wstring App::ConnMessage() const {
  switch (conn_) {
    case Conn::NeedsLogin: return L"Sign in to Tailscale to send and receive files with your devices.";
    case Conn::NeedsMachineAuth: return L"This PC is waiting for approval from your tailnet admin.";
    case Conn::Stopped: return L"Tailscale is disconnected. Connect to your tailnet to send and receive files.";
    case Conn::Starting: return L"Tailscale is still connecting.";
    case Conn::OtherUser: return L"Tailscale on this PC is in use by another Windows user.";
    case Conn::NoService:
    case Conn::Unknown: return L"The Tailscale service isn't running. Open Tailscale and sign in to continue.";
    default: return L"";
  }
}

int App::Run(const std::vector<std::wstring>& args) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\YuboCao.TailDrop");
  bool firstInstance = GetLastError() != ERROR_ALREADY_EXISTS;
  // --exit stops a running copy; with none running there is nothing to do.
  if (firstInstance && std::find(args.begin(), args.end(), L"--exit") != args.end()) return 0;
  if (!firstInstance) {
    HWND other = nullptr;
    for (int i = 0; i < 50 && !other; ++i) {
      other = FindWindowW(td::kHostClass, nullptr);
      if (!other) Sleep(100);
    }
    if (!other) return 1;
    DWORD pid = 0;
    GetWindowThreadProcessId(other, &pid);
    AllowSetForegroundWindow(pid);
    auto has = [&](const wchar_t* f) { return std::find(args.begin(), args.end(), f) != args.end(); };
    if (has(L"--exit")) {
      PostMessageW(other, WM_CLOSE, 0, 0);
      return 0;
    }
    std::vector<std::wstring> parts;
    ULONG_PTR cmd = td::kCopyShow;
    for (size_t i = 0; i < args.size(); ++i) {
      if (args[i] == L"--send" && i + 1 < args.size()) {
        cmd = td::kCopySend;
        parts.assign(args.begin() + i + 1, args.end());
        break;
      }
      if (args[i] == L"--pick") {
        cmd = td::kCopyPick;
        parts.assign(args.begin() + i + 1, args.end());
        break;
      }
      if (args[i] == L"--settings") parts = {L"settings"};
      if (args[i] == L"--background") return 0;
    }
    std::wstring payload = td::PackStrings(parts);
    COPYDATASTRUCT cds{cmd, static_cast<DWORD>(payload.size() * sizeof(wchar_t)), payload.data()};
    SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 5000, nullptr);
    return 0;
  }

  if (FAILED(OleInitialize(nullptr))) return 1;
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  ui::InitDarkMenus();
  settings_.Load();
  favorites_ = td::LoadFavorites();
  if (td::RegGetDword(L"Configured", 0) == 0) {
    // First run: register autostart once; the user can turn it off in Settings.
    settings_.ApplyRunAtLogin();
    td::RegSetDword(L"Configured", 1);
  }
  if (!CreateHost()) return 1;
  TrayAdd();
  ScanInbox();
  StartBus();
  RefreshDevices();
  if (settings_.trayDrop) StartDragWatch();
  HandleArgs(args);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  StopDragWatch();
  if (stop_) SetEvent(stop_);
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = host_;
  nid.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  OleUninitialize();
  if (mutex) CloseHandle(mutex);
  return 0;
}

void App::HandleArgs(const std::vector<std::wstring>& args) {
  bool background = false;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == L"--background") background = true;
    if (args[i] == L"--send" && i + 2 < args.size()) {
      SendTo(Narrow(args[i + 1]), std::vector<std::wstring>(args.begin() + i + 2, args.end()));
      return;
    }
    if (args[i] == L"--pick") {
      ShowPicker(std::vector<std::wstring>(args.begin() + i + 1, args.end()));
      return;
    }
    if (args[i] == L"--settings") {
      ShowSettings();
      return;
    }
  }
  if (!background) SetTimer(host_, kTimerStartup + 100, 600, nullptr);  // show the flyout once state is known
}

bool App::CreateHost() {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = HostProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = td::kHostClass;
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP));
  RegisterClassExW(&wc);
  // A hidden top-level window (not message-only) so it gets TaskbarCreated.
  host_ = CreateWindowExW(WS_EX_TOOLWINDOW, td::kHostClass, L"TailDrop", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                          wc.hInstance, nullptr);
  if (!host_) return false;
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  ChangeWindowMessageFilterEx(host_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
  ChangeWindowMessageFilterEx(host_, td::kMsgRefreshDevices, MSGFLT_ALLOW, nullptr);
  ui::AllowDarkFor(host_);
  return true;
}

LRESULT CALLBACK App::HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  App& app = Get();
  if (!app.host_) app.host_ = hwnd;
  return app.HostMessage(msg, wp, lp);
}

namespace {
struct Command {
  ULONG_PTR kind;
  std::vector<std::wstring> parts;
};
constexpr UINT WM_APP_COMMAND = WM_APP + 30;
}  // namespace

LRESULT App::HostMessage(UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == taskbarCreated_ && taskbarCreated_) {
    trayIconKind_ = -1;
    TrayAdd();
    return 0;
  }
  switch (msg) {
    case WM_APP_TRAY:
      switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
          ToggleFlyout();
          break;
        case WM_CONTEXTMENU:
          TrayMenu();
          break;
      }
      return 0;
    case WM_APP_CONN:
      SetConn(static_cast<Conn>(wp));
      return 0;
    case WM_APP_INCOMING: {
      std::unique_ptr<std::vector<Partial>> list(reinterpret_cast<std::vector<Partial>*>(lp));
      OnIncoming(list.get());
      return 0;
    }
    case WM_APP_FILESWAITING:
      OnFilesWaiting();
      return 0;
    case WM_APP_PEERSDIRTY:
      RefreshDevices(true);
      return 0;
    case WM_APP_DEVICES: {
      std::unique_ptr<DeviceSnapshot> snap(reinterpret_cast<DeviceSnapshot*>(lp));
      OnDevices(snap.get());
      return 0;
    }
    case WM_APP_THUMB: {
      std::unique_ptr<std::shared_ptr<ui::Image>> img(reinterpret_cast<std::shared_ptr<ui::Image>*>(lp));
      int id = static_cast<int>(wp);
      if (Item* it = FindItem(id)) {
        it->thumb = *img;
        UpdateBatch(it->batch);
        if (flyout_) flyout_->Changed();
      } else if (SendJob* j = FindJob(id)) {
        j->thumb = *img;
        if (j->toast) j->toast->Changed();
      }
      return 0;
    }
    case WM_APP_JOBPROGRESS: {
      std::unique_ptr<JobProgress> p(reinterpret_cast<JobProgress*>(lp));
      OnJobProgress(static_cast<int>(wp), p.get());
      return 0;
    }
    case WM_APP_SAVED: {
      std::unique_ptr<SaveResult> r(reinterpret_cast<SaveResult*>(lp));
      OnSaved(static_cast<int>(wp), r.get());
      return 0;
    }
    case WM_APP_PULLED: {
      std::unique_ptr<PulledFile> f(reinterpret_cast<PulledFile*>(lp));
      OnPulled(f.get());
      return 0;
    }
    case WM_APP_DELETEWINDOW:
      delete reinterpret_cast<ui::Window*>(lp);
      MaybeTrim();
      return 0;
    case WM_APP_TRAYDRAG:
      if (wp == 1) {
        RECT r = TrayIconRect();
        InflateRect(&r, 2, 2);
        AcquireSRWLockExclusive(&g_rectLock);
        g_iconRect = r;
        ReleaseSRWLockExclusive(&g_rectLock);
      } else if (wp == 2) {
        if (!flyout_ || !flyout_->visible()) {
          ShowFlyout(true);
          trayDragShown_ = true;
        }
      } else if (wp == 3 && trayDragShown_) {
        SetTimer(host_, kTimerTrayDragEnd, 500, nullptr);
      }
      return 0;
    case WM_APP_COMMAND: {
      std::unique_ptr<Command> c(reinterpret_cast<Command*>(lp));
      if (c->kind == td::kCopySend && c->parts.size() >= 2)
        SendTo(Narrow(c->parts[0]), std::vector<std::wstring>(c->parts.begin() + 1, c->parts.end()));
      else if (c->kind == td::kCopyPick)
        ShowPicker(c->parts);
      else if (c->kind == td::kCopyShow && !c->parts.empty() && c->parts[0] == L"settings")
        ShowSettings();
      else if (c->kind == td::kCopyShow)
        ShowFlyout(false);
      return 0;
    }
    case WM_COPYDATA: {
      auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
      if (cds->dwData != td::kCopySend && cds->dwData != td::kCopyPick && cds->dwData != td::kCopyShow) return FALSE;
      // Explorer is blocked in SendMessage until we return; act asynchronously.
      auto* c = new Command{cds->dwData, td::UnpackStrings(static_cast<const wchar_t*>(cds->lpData),
                                                           cds->cbData / sizeof(wchar_t))};
      PostMessageW(host_, WM_APP_COMMAND, 0, reinterpret_cast<LPARAM>(c));
      return TRUE;
    }
    case td::kMsgRefreshDevices:
      RefreshDevices(true);
      return 0;
    case WM_TIMER:
      switch (wp) {
        case kTimerLanding:
          PollLanding();
          break;
        case kTimerDevices:
          KillTimer(host_, kTimerDevices);
          RefreshDevices();
          break;
        case kTimerTrim:
          KillTimer(host_, kTimerTrim);
          MaybeTrim();
          break;
        case kTimerTrayDragEnd:
          KillTimer(host_, kTimerTrayDragEnd);
          trayDragShown_ = false;
          if (flyout_ && flyout_->visible() && flyout_->dropMode() && GetForegroundWindow() != flyout_->hwnd())
            HideFlyout();
          break;
        case kTimerStartup:
          KillTimer(host_, kTimerStartup);
          if (!connected() && !errorShownOnce_) {
            errorShownOnce_ = true;
            ShowError();
          }
          break;
        case kTimerStartup + 100:
          KillTimer(host_, kTimerStartup + 100);
          ShowFlyout(false);
          break;
        case kTimerStartup + 200: {
          KillTimer(host_, kTimerStartup + 200);
          // Files dragged out of TailDrop and moved elsewhere disappear from the list.
          std::vector<int> gone;
          for (auto& it : items_)
            if ((it->state == ItemState::Pending || it->state == ItemState::Saved) && !util::Exists(it->path))
              gone.push_back(it->id);
          for (int id : gone) ForgetItem(id);
          break;
        }
      }
      return 0;
    case WM_SETTINGCHANGE:
      if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) {
        ui::ReloadThemes();
        ui::InitDarkMenus();
        trayIconKind_ = -1;
        TrayUpdate();
        for (ui::Window* w : {static_cast<ui::Window*>(flyout_.get()), static_cast<ui::Window*>(picker_.get()),
                              static_cast<ui::Window*>(settingsWnd_.get()), static_cast<ui::Window*>(errorWnd_.get())})
          if (w) w->ApplyTheme();
        for (ToastBase* t : toasts_) t->ApplyTheme();
      }
      return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
      trayIconKind_ = -1;
      TrayUpdate();
      LayoutToasts();
      return 0;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_ENDSESSION:
      if (wp) PostQuitMessage(0);
      return 0;
    case WM_CLOSE:
      DestroyWindow(host_);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(host_, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Tray

RECT App::TrayIconRect() const {
  NOTIFYICONIDENTIFIER id{sizeof(id)};
  id.hWnd = host_;
  id.uID = 1;
  RECT r{};
  if (SUCCEEDED(Shell_NotifyIconGetRect(&id, &r)) && r.right > r.left) return r;
  // Icon hidden in the overflow area: use the notification area instead.
  if (HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr)) {
    if (HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr)) GetWindowRect(notify, &r);
    else GetWindowRect(tray, &r);
  }
  return r;
}

void App::TrayAdd() {
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = host_;
  nid.uID = 1;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.uCallbackMessage = WM_APP_TRAY;
  LoadIconMetric(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_TRAY_LIGHT), LIM_SMALL, &nid.hIcon);
  wcscpy_s(nid.szTip, L"TailDrop");
  Shell_NotifyIconW(NIM_ADD, &nid);
  nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &nid);
  if (nid.hIcon) DestroyIcon(nid.hIcon);
  trayIconKind_ = -1;
  TrayUpdate();
}

void App::TrayUpdate() {
  int pending = PendingCount();
  int kind = !connected() ? 2 : pending > 0 ? 1 : 0;
  bool dark = ui::SystemTheme().dark;
  int key = kind + (dark ? 3 : 0);
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = host_;
  nid.uID = 1;
  nid.uFlags = NIF_TIP | NIF_SHOWTIP;
  HICON icon = nullptr;
  if (key != trayIconKind_) {
    static const int ids[6] = {IDI_TRAY_LIGHT, IDI_TRAY_LIGHT_BADGE, IDI_TRAY_LIGHT_OFF,
                               IDI_TRAY_DARK,  IDI_TRAY_DARK_BADGE,  IDI_TRAY_DARK_OFF};
    LoadIconMetric(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(ids[key]), LIM_SMALL, &icon);
    nid.hIcon = icon;
    nid.uFlags |= NIF_ICON;
    trayIconKind_ = key;
  }
  std::wstring tip;
  if (!connected()) tip = L"TailDrop\nTailscale isn't connected";
  else if (pending == 1) tip = L"TailDrop\n1 file waiting";
  else if (pending > 1) tip = L"TailDrop\n" + std::to_wstring(pending) + L" files waiting";
  else tip = L"TailDrop\n" + (tailnet_.empty() ? std::wstring(L"Ready") : tailnet_);
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
  if (icon) {
    if (trayIcon_) DestroyIcon(trayIcon_);
    trayIcon_ = icon;
  }
}

void App::TrayMenu() {
  POINT pt;
  GetCursorPos(&pt);
  enum { kOpen = 1, kSend, kInbox, kSettings, kExit };
  int cmd = ui::ShowMenu(host_, pt,
                         {{kOpen, L"Open TailDrop"},
                          {kSend, L"Send files\u2026", false, !connected()},
                          {kInbox, L"Open received files"},
                          {0},
                          {kSettings, L"Settings"},
                          {kExit, L"Exit"}});
  switch (cmd) {
    case kOpen: ShowFlyout(false); break;
    case kSend: {
      auto files = util::PickFiles(host_, L"Choose files to send with TailDrop");
      if (!files.empty()) ShowPicker(files);
      break;
    }
    case kInbox: OpenInbox(); break;
    case kSettings: ShowSettings(); break;
    case kExit: DestroyWindow(host_); break;
  }
}

void App::StartDragWatch() {
  if (g_hookThread) return;
  g_hookHost = host_;
  g_hookThread = CreateThread(nullptr, 64 * 1024, HookThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION,
                              &g_hookThreadId);
}

void App::StopDragWatch() {
  if (!g_hookThread) return;
  PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
  WaitForSingleObject(g_hookThread, 2000);
  CloseHandle(g_hookThread);
  g_hookThread = nullptr;
}

// ---------------------------------------------------------------------------
// Tailscale state

void App::StartBus() {
  stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HWND host = host_;
  HANDLE stop = stop_;
  util::RunAsync([host, stop] { BusLoop(host, stop); });
}

void App::SetConn(Conn c) {
  // TAILDROP_DEMO=offline previews the signed-out screens.
  static const bool demoOffline = [] {
    wchar_t v[16] = L"";
    GetEnvironmentVariableW(L"TAILDROP_DEMO", v, 16);
    return _wcsicmp(v, L"offline") == 0;
  }();
  if (demoOffline) c = Conn::NeedsLogin;
  if (c == conn_) return;
  util::Log(L"tailscale state %d -> %d", static_cast<int>(conn_), static_cast<int>(c));
  bool wasRunning = conn_ == Conn::Running;
  conn_ = c;
  TrayUpdate();
  if (c == Conn::Running) {
    KillTimer(host_, kTimerStartup);
    if (errorWnd_ && errorWnd_->visible()) errorWnd_->Close();
    RefreshDevices();
  } else {
    if (wasRunning) errorShownOnce_ = false;
    // Give tailscaled a moment: Starting -> Running is normal at boot.
    SetTimer(host_, kTimerStartup, c == Conn::Starting ? 8000 : 2500, nullptr);
  }
  Repaint();
}

void App::RefreshDevices(bool soon) {
  if (soon) {
    SetTimer(host_, kTimerDevices, 1500, nullptr);
    return;
  }
  if (devicesInFlight_) {
    devicesAgain_ = true;
    return;
  }
  devicesInFlight_ = true;
  HWND host = host_;
  util::RunAsync([host] {
    DeviceSnapshot* s = FetchDevices();
    if (!PostMessageW(host, WM_APP_DEVICES, 0, reinterpret_cast<LPARAM>(s))) delete s;
  });
}

void App::OnDevices(DeviceSnapshot* snap) {
  devicesInFlight_ = false;
  if (!snap->connected) {
    SetConn(Conn::NoService);
  } else if (!snap->backendState.empty()) {
    SetConn(MapStateName(snap->backendState));
  }
  if (!snap->selfName.empty()) selfName_ = snap->selfName;
  if (!snap->tailnet.empty()) tailnet_ = snap->tailnet;
  if (snap->connected) devices_ = std::move(snap->devices);
  if (GetEnvironmentVariableW(L"TAILDROP_DEMO", nullptr, 0) > 0) {
    selfName_ = L"desktop";
    tailnet_ = L"example.github";
    devices_.clear();
    const struct {
      const char* id;
      const wchar_t* name;
      const wchar_t* os;
      DeviceKind kind;
      bool online;
    } demo[] = {{"demo-ipad", L"ipad", L"iOS", DeviceKind::Tablet, true},
                {"demo-iphone", L"iphone", L"iOS", DeviceKind::Phone, true},
                {"demo-mac", L"macbook-air", L"macOS", DeviceKind::Laptop, true},
                {"demo-pc", L"studio-pc", L"windows", DeviceKind::PC, true},
                {"demo-nas", L"homelab", L"linux", DeviceKind::Server, true},
                {"demo-pixel", L"pixel", L"android", DeviceKind::Phone, false}};
    for (const auto& d : demo) {
      Device dev;
      dev.stableId = d.id;
      dev.name = d.name;
      dev.dns = std::wstring(d.name) + L".example.ts.net";
      dev.os = d.os;
      dev.kind = d.kind;
      dev.online = d.online;
      devices_.push_back(std::move(dev));
    }
  }
  // Keep the favorites the Explorer menu shows in sync with names and status.
  bool changed = false;
  for (td::Favorite& f : favorites_) {
    if (const Device* d = FindDevice(Narrow(f.stableId))) {
      if (f.name != d->name || f.online != d->online || f.kind != KindKey(d->kind)) {
        f.name = d->name;
        f.online = d->online;
        f.kind = KindKey(d->kind);
        changed = true;
      }
    } else if (f.online && snap->connected) {
      f.online = false;
      changed = true;
    }
  }
  if (changed) td::SaveFavorites(favorites_);
  TrayUpdate();
  Repaint();
  if (devicesAgain_) {
    devicesAgain_ = false;
    RefreshDevices();
  }
}

std::vector<const Device*> App::SortedDevices() const {
  std::vector<const Device*> out;
  for (const Device& d : devices_) out.push_back(&d);
  std::stable_sort(out.begin(), out.end(), [this](const Device* a, const Device* b) {
    bool fa = IsFavorite(a->stableId), fb = IsFavorite(b->stableId);
    if (fa != fb) return fa;
    if (a->online != b->online) return a->online;
    return _wcsicmp(a->name.c_str(), b->name.c_str()) < 0;
  });
  return out;
}

const Device* App::FindDevice(const std::string& id) const {
  for (const Device& d : devices_)
    if (d.stableId == id) return &d;
  return nullptr;
}

bool App::IsFavorite(const std::string& id) const {
  std::wstring w = Widen(id);
  for (const auto& f : favorites_)
    if (f.stableId == w) return true;
  return false;
}

void App::ToggleFavorite(const Device& d) {
  std::wstring id = Widen(d.stableId);
  auto it = std::find_if(favorites_.begin(), favorites_.end(), [&](const td::Favorite& f) { return f.stableId == id; });
  if (it != favorites_.end()) {
    favorites_.erase(it);
  } else {
    if (favorites_.size() >= td::kMaxFavorites) favorites_.erase(favorites_.begin());
    favorites_.push_back({id, d.name, KindKey(d.kind), d.online});
  }
  td::SaveFavorites(favorites_);
  Repaint();
}

void App::SettingsChanged() {
  settings_.Save();
  settings_.ApplyRunAtLogin();
  if (settings_.trayDrop) StartDragWatch();
  else StopDragWatch();
  Repaint();
}

// ---------------------------------------------------------------------------
// Receiving

Item* App::FindItem(int id) {
  for (auto& it : items_)
    if (it->id == id) return it.get();
  return nullptr;
}

Batch* App::FindBatch(int id) {
  for (auto& b : batches_)
    if (b->id == id) return b.get();
  return nullptr;
}

SendJob* App::FindJob(int id) {
  for (auto& j : jobs_)
    if (j->id == id) return j.get();
  return nullptr;
}

int App::PendingCount() const {
  int n = 0;
  for (const auto& it : items_)
    if (it->state == ItemState::Pending || it->state == ItemState::Receiving || it->state == ItemState::Landing) ++n;
  return n;
}

Item* App::NewItem(const std::wstring& name) {
  auto it = std::make_unique<Item>();
  it->id = nextId_++;
  it->name = name;
  it->landName = name;
  it->firstSeen = GetTickCount64();
  GetSystemTimeAsFileTime(&it->firstSeenTime);
  Item* raw = it.get();
  items_.push_back(std::move(it));
  // Keep the history short; pending files are never dropped.
  int saved = 0;
  for (auto i = items_.rbegin(); i != items_.rend(); ++i)
    if ((*i)->state == ItemState::Saved || (*i)->state == ItemState::Failed) ++saved;
  while (saved > 12) {
    auto i = std::find_if(items_.begin(), items_.end(), [](const std::unique_ptr<Item>& x) {
      return x->state == ItemState::Saved || x->state == ItemState::Failed;
    });
    if (i == items_.end()) break;
    ForgetItem((*i)->id);
    --saved;
  }
  return raw;
}

void App::AttachToBatch(Item* it, bool allowJoin) {
  ULONGLONG now = GetTickCount64();
  Batch* target = nullptr;
  if (allowJoin) {
    for (auto& b : batches_) {
      if (b->toast && now - b->lastStart < 8000) {
        bool open = std::all_of(b->items.begin(), b->items.end(), [this](int id) {
          Item* x = FindItem(id);
          return x && x->state != ItemState::Saved;
        });
        if (open) target = b.get();
      }
    }
  }
  if (!target) {
    auto b = std::make_unique<Batch>();
    b->id = nextId_++;
    target = b.get();
    batches_.push_back(std::move(b));
  }
  target->items.push_back(it->id);
  target->lastStart = now;
  it->batch = target->id;
  if (!target->toast) {
    auto* t = new ReceiveToast(target->id);
    t->onDestroyed = [this](ui::Window* w) {
      auto* rt = static_cast<ReceiveToast*>(w);
      toasts_.erase(std::remove(toasts_.begin(), toasts_.end(), rt), toasts_.end());
      if (Batch* b = FindBatch(rt->batch())) b->toast = nullptr;
      PostMessageW(host_, WM_APP_DELETEWINDOW, 0, reinterpret_cast<LPARAM>(w));
      LayoutToasts();
    };
    target->toast = t;
    toasts_.push_back(t);
  }
  LayoutToasts();
}

void App::OnIncoming(std::vector<Partial>* list) {
  ULONGLONG now = GetTickCount64();
  std::set<std::wstring> present;
  std::set<int> touched;
  for (const Partial& p : *list) {
    std::wstring name = Widen(p.name);
    present.insert(name);
    Item* it = nullptr;
    for (auto& x : items_) {
      if (x->name == name && (x->state == ItemState::Receiving ||
                              (x->state == ItemState::Landing && now - x->firstSeen < 120000))) {
        it = x.get();
        break;
      }
    }
    if (!it) {
      it = NewItem(name);
      it->state = ItemState::Receiving;
      AttachToBatch(it, true);
    }
    if (it->state != ItemState::Receiving) continue;
    util::Log(L"incoming %s %lld/%lld%s", name.c_str(), p.received, p.declared, p.done ? L" done" : L"");
    it->received = p.received;
    if (p.declared >= 0) it->size = p.declared;
    if (p.done) {
      it->state = ItemState::Landing;
      it->landName = p.finalPath.empty() ? name : util::FileName(Widen(p.finalPath));
      it->deadline = now + 30000;
      if (it->size >= 0) it->received = it->size;
    }
    touched.insert(it->batch);
  }
  for (auto& x : items_) {
    if (x->state == ItemState::Receiving && !present.count(x->name)) {
      // Finished (or aborted) between two notifications.
      x->state = ItemState::Landing;
      x->deadline = now + 30000;
      touched.insert(x->batch);
    }
  }
  bool landing = false;
  for (auto& x : items_)
    if (x->state == ItemState::Landing) landing = true;
  if (landing && !landingTimer_) {
    SetTimer(host_, kTimerLanding, 250, nullptr);
    landingTimer_ = true;
  }
  for (int b : touched) UpdateBatch(b);
  TrayUpdate();
  if (flyout_) flyout_->Changed();
}

bool App::TryLand(Item* it) {
  // The official Tailscale app moves finished files into Downloads; find ours
  // there by name, size and creation time.
  static std::wstring downloads = util::KnownFolder(FOLDERID_Downloads);
  std::wstring stem = util::Stem(it->landName), ext = util::Extension(it->landName);
  std::wstring pattern = util::Join(downloads, stem + L"*" + ext);
  ULARGE_INTEGER since;
  since.LowPart = it->firstSeenTime.dwLowDateTime;
  since.HighPart = it->firstSeenTime.dwHighDateTime;
  since.QuadPart -= 10ULL * 10000000ULL;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
  if (h == INVALID_HANDLE_VALUE) return false;
  std::wstring best;
  ULONGLONG bestTime = 0;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    int64_t size = (static_cast<int64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    if (it->size >= 0 && size != it->size) continue;
    ULARGE_INTEGER ct;
    ct.LowPart = fd.ftCreationTime.dwLowDateTime;
    ct.HighPart = fd.ftCreationTime.dwHighDateTime;
    ULARGE_INTEGER wt;
    wt.LowPart = fd.ftLastWriteTime.dwLowDateTime;
    wt.HighPart = fd.ftLastWriteTime.dwHighDateTime;
    ULONGLONG t = std::max(ct.QuadPart, wt.QuadPart);
    if (t < since.QuadPart) continue;
    std::wstring full = util::Join(downloads, fd.cFileName);
    bool claimed = std::any_of(items_.begin(), items_.end(), [&](const std::unique_ptr<Item>& x) {
      return x.get() != it && _wcsicmp(x->path.c_str(), full.c_str()) == 0;
    });
    if (claimed || t < bestTime) continue;
    best = full;
    bestTime = t;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  if (best.empty()) return false;
  util::Log(L"landed %s at %s", it->landName.c_str(), best.c_str());
  // Still being written by the Tailscale app?
  HANDLE f = CreateFileW(best.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  CloseHandle(f);
  bool keepInPlace = settings_.autoSave && _wcsicmp(settings_.EffectiveDownloadDir().c_str(), downloads.c_str()) == 0;
  it->path = best;
  if (!keepInPlace) {
    std::wstring dest = util::UniquePath(util::InboxDir(), util::FileName(best));
    // Same volume: an instant rename. Otherwise leave it in Downloads.
    if (towupper(best[0]) == towupper(dest[0]) && MoveFileExW(best.c_str(), dest.c_str(), 0)) it->path = dest;
  } else {
    it->state = ItemState::Saved;
  }
  return true;
}

void App::PollLanding() {
  ULONGLONG now = GetTickCount64();
  bool any = false;
  for (auto& x : items_) {
    Item* it = x.get();
    if (it->state != ItemState::Landing) continue;
    if (!it->path.empty() || TryLand(it)) {
      if (it->state == ItemState::Saved) {
        RequestThumb(it);
        UpdateBatch(it->batch);
      } else {
        ItemReady(it);
      }
      continue;
    }
    if (now > it->deadline) {
      it->state = ItemState::Failed;
      it->note = L"The transfer didn't finish.";
      UpdateBatch(it->batch);
      continue;
    }
    any = true;
  }
  if (!any) {
    KillTimer(host_, kTimerLanding);
    landingTimer_ = false;
  }
  TrayUpdate();
  if (flyout_) flyout_->Changed();
}

void App::ItemReady(Item* it) {
  it->state = ItemState::Pending;
  it->size = util::FileSize(it->path);
  RequestThumb(it);
  if (settings_.autoSave) SaveItem(it->id);
  UpdateBatch(it->batch);
  TrayUpdate();
  if (flyout_) flyout_->Changed();
}

void App::OnFilesWaiting() {
  // Only pull files ourselves when the official Tailscale app isn't running;
  // otherwise it saves them to Downloads and TryLand picks them up there.
  if (pulling_ || util::IsProcessRunning(L"tailscale-ipn.exe")) return;
  pulling_ = true;
  HWND host = host_;
  std::wstring inbox = util::InboxDir();
  util::RunAsync([host, inbox] {
    std::vector<char> buf(64 * 1024);
    for (int round = 0; round < 4; ++round) {
      lapi::Result r = lapi::Get("/localapi/v0/files/");
      json::Value v;
      if (!r.ok() || !json::Parse(r.body, v) || v.size() == 0) break;
      for (const json::Value& f : v.items) {
        if (util::IsProcessRunning(L"tailscale-ipn.exe")) goto done;
        std::string name = f["Name"].str();
        std::wstring dest = util::UniquePath(inbox, util::SafeFileName(Widen(name)));
        lapi::Conn c;
        int status = 0;
        if (!c.Open() || !c.SendHead("GET", "/localapi/v0/files/" + lapi::EscapeSegment(name), -1) ||
            !c.ReadHead(status) || status != 200)
          continue;
        HANDLE out = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool ok = out != INVALID_HANDLE_VALUE;
        int n = 0;
        while (ok && (n = c.ReadBody(buf.data(), static_cast<int>(buf.size()))) > 0) {
          DWORD w = 0;
          ok = WriteFile(out, buf.data(), n, &w, nullptr) && w == static_cast<DWORD>(n);
        }
        if (n < 0) ok = false;
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (!ok) {
          DeleteFileW(dest.c_str());
          continue;
        }
        lapi::Send("DELETE", "/localapi/v0/files/" + lapi::EscapeSegment(name), "");
        auto* pf = new PulledFile{Widen(name), dest, util::FileSize(dest)};
        if (!PostMessageW(host, WM_APP_PULLED, 0, reinterpret_cast<LPARAM>(pf))) delete pf;
      }
    }
  done:
    PostMessageW(host, WM_APP_PULLED, 0, 0);
  });
}

void App::OnPulled(PulledFile* f) {
  if (!f) {
    pulling_ = false;
    return;
  }
  Item* it = nullptr;
  for (auto& x : items_)
    if ((x->state == ItemState::Landing || x->state == ItemState::Receiving) &&
        (x->landName == f->name || x->name == f->name) && x->path.empty())
      it = x.get();
  if (!it) {
    it = NewItem(f->name);
    AttachToBatch(it, true);
  }
  it->path = f->path;
  ItemReady(it);
}

void App::ScanInbox() {
  WIN32_FIND_DATAW fd;
  std::wstring inbox = util::InboxDir();
  HANDLE h = FindFirstFileExW(util::Join(inbox, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
    Item* it = NewItem(fd.cFileName);
    it->path = util::Join(inbox, fd.cFileName);
    it->size = (static_cast<int64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    it->state = ItemState::Pending;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

void App::RequestThumb(Item* it) {
  if (it->thumbRequested || it->path.empty()) return;
  it->thumbRequested = true;
  HWND host = host_;
  int id = it->id;
  std::wstring path = it->path;
  int px = static_cast<int>(320 * ui::DpiScaleForPoint({0, 0}));
  util::RunAsync([host, id, path, px] {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto img = ui::LoadThumbnail(path, px);
    CoUninitialize();
    if (!img) return;
    auto* p = new std::shared_ptr<ui::Image>(std::move(img));
    if (!PostMessageW(host, WM_APP_THUMB, id, reinterpret_cast<LPARAM>(p))) delete p;
  });
}

void App::UpdateBatch(int batchId) {
  Batch* b = FindBatch(batchId);
  if (!b) return;
  b->items.erase(std::remove_if(b->items.begin(), b->items.end(), [this](int id) { return !FindItem(id); }),
                 b->items.end());
  if (b->toast) {
    if (b->items.empty()) {
      b->toast->Dismiss();
    } else {
      b->toast->Changed();
      LayoutToasts();
    }
  }
}

void App::SaveItem(int id, const std::wstring& dir) {
  Item* it = FindItem(id);
  if (!it || it->state != ItemState::Pending) return;
  std::wstring target = dir.empty() ? settings_.EffectiveDownloadDir() : dir;
  if (_wcsicmp(util::ParentDir(it->path).c_str(), target.c_str()) == 0) {
    it->state = ItemState::Saved;
    UpdateBatch(it->batch);
    TrayUpdate();
    if (flyout_) flyout_->Changed();
    return;
  }
  it->state = ItemState::Saving;
  HWND host = host_;
  std::wstring src = it->path, name = util::SafeFileName(it->name);
  util::RunAsync([host, id, src, name, target] {
    auto* r = new SaveResult{false, src};
    for (int attempt = 0; attempt < 3 && !r->ok; ++attempt) {
      std::wstring dest = util::UniquePath(target, name);
      if (util::MoveFileTo(src, dest)) {
        r->ok = true;
        r->path = dest;
      }
    }
    if (!PostMessageW(host, WM_APP_SAVED, id, reinterpret_cast<LPARAM>(r))) delete r;
  });
  UpdateBatch(it->batch);
}

void App::SaveBatch(int batchId, const std::wstring& dir) {
  Batch* b = FindBatch(batchId);
  if (!b) return;
  // Copy: SaveItem may change the batch's item list.
  std::vector<int> ids = b->items;
  for (int id : ids) SaveItem(id, dir);
}

void App::OnSaved(int id, SaveResult* r) {
  Item* it = FindItem(id);
  if (!it) return;
  it->path = r->path;
  it->state = r->ok ? ItemState::Saved : ItemState::Pending;
  it->note = r->ok ? L"" : L"Couldn't move the file there.";
  UpdateBatch(it->batch);
  TrayUpdate();
  if (flyout_) flyout_->Changed();
}

void App::OpenItem(int id) {
  if (Item* it = FindItem(id); it && !it->path.empty()) util::OpenPath(it->path);
}

void App::RevealItem(int id) {
  if (Item* it = FindItem(id); it && !it->path.empty()) util::RevealInExplorer(it->path);
}

void App::DiscardItem(int id) {
  Item* it = FindItem(id);
  if (!it) return;
  if (!it->path.empty() && util::Exists(it->path)) util::RecycleFile(it->path);
  ForgetItem(id);
}

void App::ForgetItem(int id) {
  auto i = std::find_if(items_.begin(), items_.end(), [id](const std::unique_ptr<Item>& x) { return x->id == id; });
  if (i == items_.end()) return;
  int batch = (*i)->batch;
  items_.erase(i);
  UpdateBatch(batch);
  TrayUpdate();
  if (flyout_) flyout_->Changed();
}

void App::DragItems(HWND from, const std::vector<int>& ids) {
  std::vector<std::wstring> paths;
  const ui::Image* preview = nullptr;
  for (int id : ids) {
    Item* it = FindItem(id);
    if (!it || it->path.empty() || !util::Exists(it->path)) continue;
    if (it->state != ItemState::Pending && it->state != ItemState::Saved) continue;
    paths.push_back(it->path);
    if (!preview && it->thumb) preview = it->thumb.get();
  }
  if (paths.empty()) return;
  ui::StartFileDrag(from, paths, preview);
  // Explorer may finish an "optimized move" asynchronously; check shortly after.
  SetTimer(host_, kTimerStartup + 200, 900, nullptr);
}

// ---------------------------------------------------------------------------
// Sending

void App::SendTo(const std::string& stableId, std::vector<std::wstring> paths) {
  paths.erase(std::remove_if(paths.begin(), paths.end(), [](const std::wstring& p) { return !util::Exists(p); }),
              paths.end());
  if (paths.empty()) return;
  if (!connected()) {
    ShowError();
    return;
  }
  auto job = std::make_unique<SendJob>();
  job->id = nextId_++;
  if (const Device* d = FindDevice(stableId)) {
    job->device = *d;
  } else {
    job->device.stableId = stableId;
    job->device.name = Widen(stableId);
    for (const auto& f : favorites_)
      if (f.stableId == Widen(stableId)) job->device.name = f.name;
  }
  job->paths = paths;
  job->cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  job->started = GetTickCount64();
  job->fileCount = static_cast<int>(paths.size());
  job->current = util::FileName(paths[0]);
  SendJob* raw = job.get();
  jobs_.push_back(std::move(job));

  auto* t = new SendToast(raw->id);
  t->onDestroyed = [this](ui::Window* w) {
    auto* st = static_cast<SendToast*>(w);
    toasts_.erase(std::remove(toasts_.begin(), toasts_.end(), st), toasts_.end());
    auto j = std::find_if(jobs_.begin(), jobs_.end(), [st](const std::unique_ptr<SendJob>& x) { return x->id == st->job(); });
    if (j != jobs_.end()) {
      (*j)->toast = nullptr;
      if ((*j)->state != JobState::Preparing && (*j)->state != JobState::Sending) {
        CloseHandle((*j)->cancel);
        jobs_.erase(j);
      }
    }
    PostMessageW(host_, WM_APP_DELETEWINDOW, 0, reinterpret_cast<LPARAM>(w));
    LayoutToasts();
  };
  raw->toast = t;
  toasts_.push_back(t);
  LayoutToasts();

  HWND host = host_;
  int id = raw->id;
  std::string sid = stableId;
  std::wstring name = raw->device.name;
  HANDLE cancel = raw->cancel;
  util::RunAsync([host, id, sid, name, paths, cancel] { SendWorker(host, id, sid, name, paths, cancel); });
  std::wstring first = paths[0];
  int px = static_cast<int>(40 * ui::DpiScaleForPoint({0, 0}));
  util::RunAsync([host, id, first, px] {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto img = ui::LoadThumbnail(first, px);
    CoUninitialize();
    if (!img) return;
    auto* p = new std::shared_ptr<ui::Image>(std::move(img));
    if (!PostMessageW(host, WM_APP_THUMB, id, reinterpret_cast<LPARAM>(p))) delete p;
  });
}

void App::PickAndSend(const std::string& stableId) {
  const Device* d = FindDevice(stableId);
  std::wstring title = L"Choose files to send to " + (d ? d->name : std::wstring(L"device"));
  auto files = util::PickFiles(host_, title.c_str());
  if (!files.empty()) SendTo(stableId, files);
}

void App::OnJobProgress(int id, JobProgress* p) {
  SendJob* j = FindJob(id);
  if (!j) return;
  if (p->state != j->state)
    util::Log(L"send %d -> %s: state %d %s", id, j->device.name.c_str(), static_cast<int>(p->state), p->error.c_str());
  ULONGLONG now = GetTickCount64();
  double dt = (now - j->started) / 1000.0;
  if (p->state == JobState::Sending && dt > 0.5) j->rate = p->sent / dt;
  j->state = p->state;
  j->total = p->total;
  j->sent = p->sent;
  j->fileIndex = p->fileIndex;
  j->fileCount = p->fileCount ? p->fileCount : j->fileCount;
  if (!p->current.empty()) j->current = p->current;
  j->error = p->error;
  if (j->toast) {
    j->toast->Changed();
    if (j->state == JobState::Done) j->toast->ArmAutoDismiss(4500);
    if (j->state == JobState::Cancelled) j->toast->Dismiss();
  } else if (j->state != JobState::Preparing && j->state != JobState::Sending) {
    CloseHandle(j->cancel);
    jobs_.erase(std::find_if(jobs_.begin(), jobs_.end(), [id](const std::unique_ptr<SendJob>& x) { return x->id == id; }));
  }
  LayoutToasts();
}

void App::CancelJob(int id) {
  if (SendJob* j = FindJob(id)) SetEvent(j->cancel);
}

void App::RetryJob(int id) {
  SendJob* j = FindJob(id);
  if (!j) return;
  std::string sid = j->device.stableId;
  std::vector<std::wstring> paths = j->paths;
  if (j->toast) j->toast->Dismiss();
  SendTo(sid, paths);
}

// ---------------------------------------------------------------------------
// Windows

void App::ToggleFlyout() {
  if (flyout_ && flyout_->visible()) {
    HideFlyout();
    return;
  }
  // The click that deactivated (and hid) the flyout also reaches the icon.
  if (GetTickCount64() - flyoutHiddenAt_ < 350) return;
  ShowFlyout(false);
}

void App::ShowFlyout(bool dropMode) {
  if (!flyout_) {
    flyout_ = std::make_unique<Flyout>();
  }
  RefreshDevices();
  for (ToastBase* t : toasts_)
    if (t->hwnd()) ShowWindow(t->hwnd(), SW_HIDE);
  flyout_->Show(dropMode);
}

void App::HideFlyout() {
  if (!flyout_ || !flyout_->visible()) return;
  flyout_->Hide();
  flyoutHiddenAt_ = GetTickCount64();
  LayoutToasts();
  SetTimer(host_, kTimerTrim, 4000, nullptr);
}

void App::ShowPicker(std::vector<std::wstring> paths) {
  paths.erase(std::remove_if(paths.begin(), paths.end(), [](const std::wstring& p) { return !util::Exists(p); }),
              paths.end());
  if (paths.empty()) return;
  if (!connected()) {
    ShowError();
    return;
  }
  HideFlyout();
  RefreshDevices();
  if (!picker_) {
    picker_ = std::make_unique<PickerWindow>();
  }
  picker_->Open(std::move(paths));
}

void App::ShowSettings() {
  HideFlyout();
  RefreshDevices();
  if (!settingsWnd_) settingsWnd_ = std::make_unique<SettingsWindow>();
  settingsWnd_->Open();
}

void App::ShowError() {
  HideFlyout();
  if (!errorWnd_) errorWnd_ = std::make_unique<ErrorWindow>();
  errorWnd_->Open();
}

void App::OpenTailscale() {
  wchar_t pf[MAX_PATH];
  ExpandEnvironmentStringsW(L"%ProgramFiles%\\Tailscale\\tailscale-ipn.exe", pf, MAX_PATH);
  if (util::Exists(pf)) ShellExecuteW(nullptr, L"open", pf, nullptr, nullptr, SW_SHOWNORMAL);
  else ShellExecuteW(nullptr, L"open", L"https://tailscale.com/download/windows", nullptr, nullptr, SW_SHOWNORMAL);
  RefreshDevices(true);
}

void App::OpenInbox() { ShellExecuteW(nullptr, L"open", util::InboxDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void App::LayoutToasts() {
  if (flyout_ && flyout_->visible()) return;
  RECT ir = TrayIconRect();
  POINT anchor{(ir.left + ir.right) / 2, (ir.top + ir.bottom) / 2};
  if (ir.right <= ir.left) {
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    anchor = {wa.right - 1, wa.bottom - 1};
  }
  RECT work = ui::WorkAreaForPoint(anchor);
  float s = ui::DpiScaleForPoint(anchor);
  int margin = static_cast<int>(12 * s), gap = static_cast<int>(8 * s);
  int w = static_cast<int>(360 * s);
  bool top = ir.bottom <= work.top + 4 && ir.right > ir.left;  // taskbar at the top
  int y = top ? work.top + margin : work.bottom - margin;
  for (auto i = toasts_.rbegin(); i != toasts_.rend(); ++i) {
    ToastBase* t = *i;
    int h = static_cast<int>(t->DesiredHeight() * s);
    if (top) {
      t->Place(work.right - margin - w, y, w, h, true);
      y += h + gap;
    } else {
      y -= h;
      if (y < work.top) break;
      t->Place(work.right - margin - w, y, w, h, true);
      y -= gap;
    }
  }
}

void App::CloseToast(ToastBase* t) { t->Close(); }

void App::Repaint() {
  if (flyout_) flyout_->Changed();
  if (picker_) picker_->Changed();
  if (settingsWnd_) settingsWnd_->Changed();
  if (errorWnd_) errorWnd_->Changed();
  for (ToastBase* t : toasts_) t->Invalidate();
}

void App::DeleteWindow(ui::Window* w) { delete w; }

void App::ScheduleTrim() { SetTimer(host_, kTimerTrim, 3000, nullptr); }

void App::MaybeTrim() {
  bool any = !toasts_.empty() || (flyout_ && flyout_->visible()) || (picker_ && picker_->visible()) ||
             (settingsWnd_ && settingsWnd_->visible()) || (errorWnd_ && errorWnd_->visible());
  if (any) return;
  // Drop decoded bitmaps of anything not on screen; thumbnails reload lazily.
  for (auto& it : items_) {
    it->thumb.reset();
    it->thumbRequested = false;
  }
  for (auto& j : jobs_) j->thumb.reset();
  ReleaseAppIconImages();
  ui::Gfx::Get().ReleaseIdle();
  util::TrimMemory();
}
