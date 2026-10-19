// TailDrop.exe: the background listener. It owns the tray icon, follows
// tailscaled, receives and sends files, and drives TailDropUI.exe (WinUI 3),
// which it starts only while there is something to show.
#pragma once

#include <windows.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../common/shared.h"
#include "ipc.h"
#include "settings.h"

namespace json {
struct Value;
}

// Messages posted to the host window by worker threads.
enum : UINT {
  WM_APP_TRAY = WM_APP + 1,
  WM_APP_CONN,          // wParam = Conn
  WM_APP_INCOMING,      // lParam = std::vector<Partial>*
  WM_APP_FILESWAITING,  //
  WM_APP_PEERSDIRTY,    //
  WM_APP_DEVICES,       // lParam = DeviceSnapshot*
  WM_APP_JOBPROGRESS,   // wParam = job id, lParam = JobProgress*
  WM_APP_SAVED,         // wParam = item id, lParam = SaveResult*
  WM_APP_PULLED,        // lParam = PulledFile*
  WM_APP_TRAYDRAG,      // wParam = 1 drag started, 2 hovering icon, 3 button released
  WM_APP_COMMAND,       // lParam = Command* (from WM_COPYDATA)
};

enum class Conn { Unknown, NoService, OtherUser, NeedsLogin, NeedsMachineAuth, Stopped, Starting, Running };

// What a device's icon shows. Peers only report their OS and name, so the
// automatic choice is a guess; users can pick another per device.
enum class DeviceKind { PC, Laptop, Phone, Tablet, Server, Other };
inline constexpr DeviceKind kAllKinds[] = {DeviceKind::PC,     DeviceKind::Laptop, DeviceKind::Phone,
                                           DeviceKind::Tablet, DeviceKind::Server, DeviceKind::Other};
const wchar_t* KindKey(DeviceKind k);  // stable id: "pc", "laptop", ...
std::optional<DeviceKind> KindFromKey(const std::wstring& key);

struct Device {
  std::string stableId;
  int64_t nodeId = 0;
  std::wstring name;      // what TailDrop shows: the nickname, else hostName
  std::wstring hostName;  // Tailscale's name for it
  std::wstring nickname;
  std::wstring dns;
  std::wstring os;
  std::wstring ip;
  DeviceKind kind = DeviceKind::Other;
  DeviceKind autoKind = DeviceKind::Other;
  bool customKind = false;
  bool online = false;
};

struct DeviceSnapshot {
  bool connected = false;
  std::string backendState;
  std::wstring selfName, tailnet;
  std::vector<Device> devices;
};

struct Partial {
  std::string name;
  std::string finalPath;
  int64_t declared = -1;
  int64_t received = 0;
  bool done = false;
};

enum class ItemState { Receiving, Landing, Pending, Saving, Saved, Failed };

struct Item {
  int id = 0;
  int batch = 0;
  std::wstring name;  // name announced by tailscaled
  std::wstring path;  // where the file is right now
  int64_t size = -1;
  int64_t received = 0;
  ItemState state = ItemState::Receiving;
  ULONGLONG firstSeen = 0;
  FILETIME firstSeenTime{};
  ULONGLONG deadline = 0;
  ULONGLONG doneAt = 0;   // when tailscaled finished receiving it
  std::wstring landName;  // basename tailscaled finalized the file under
  std::string note;       // error code for the UI, e.g. "incomplete"
};

enum class JobState { Preparing, Sending, Done, Failed, Cancelled };

struct SendJob {
  int id = 0;
  Device device;
  std::vector<std::wstring> paths;
  JobState state = JobState::Preparing;
  int64_t total = 0, sent = 0;
  int fileIndex = 0, fileCount = 0;
  std::wstring current;
  std::string error;  // error code for the UI
  std::wstring errorDetail;
  HANDLE cancel = nullptr;
  ULONGLONG started = 0, finished = 0;
  double rate = 0;
  bool dismissed = false;  // its notification is gone
};

struct JobProgress {
  JobState state;
  int64_t total, sent;
  int fileIndex, fileCount;
  std::wstring current;
  std::string error;
  std::wstring errorDetail;
};

struct SaveResult {
  bool ok;
  std::wstring path;
};

struct PulledFile {
  std::wstring name, path;
  int64_t size;
};

class App {
 public:
  static App& Get();
  int Run(const std::vector<std::wstring>& args);

 private:
  // A "show" or "notify" message for the UI: queued until it connects, then
  // kept until the UI acknowledges it so a UI that hangs or dies loses nothing.
  struct ShowMsg {
    std::string json;
    ULONGLONG queued = 0, sent = 0;
    int seq = 0;
    bool flyout = false;
    bool transient = false;  // dropped rather than queued for a new UI
  };

  App() = default;
  bool CreateHost();
  static LRESULT CALLBACK HostProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HostMessage(UINT, WPARAM, LPARAM);
  void HandleArgs(const std::vector<std::wstring>& args);
  bool connected() const { return conn_ == Conn::Running; }

  // Tray.
  void TrayAdd();
  void TrayUpdate();
  void TrayMenu();
  RECT TrayIconRect() const;
  void StartDragWatch();
  void StopDragWatch();

  // Tailscale.
  void StartBus();
  void OnResume();
  void SetConn(Conn c);
  void RefreshDevices(bool soon = false);
  void OnDevices(DeviceSnapshot* snap);
  void LoadDeviceOverrides();
  void ApplyDeviceOverrides();
  void SyncFavorites(bool markMissingOffline);
  void SyncMenuDevices();
  void RememberRecent(const std::string& stableId);
  const Device* FindDevice(const std::string& stableId) const;
  bool IsFavorite(const std::string& stableId) const;
  void SetFavorite(const std::string& stableId, bool on);
  void SetDeviceKind(const std::string& stableId, std::optional<DeviceKind> kind);
  void SetNickname(const std::string& stableId, const std::wstring& name);

  // Receiving.
  void OnIncoming(std::vector<Partial>* list);
  void OnFilesWaiting(bool force = false);
  void OnPulled(PulledFile* f);
  void PollLanding();
  bool TryLand(Item* it);
  void ScanInbox();
  Item* NewItem(const std::wstring& name);
  Item* FindItem(int id);
  void AttachToBatch(Item* it);
  void ItemReady(Item* it);
  int PendingCount() const;
  void SaveItem(int id, const std::wstring& dir);
  void SaveBatch(int batch, const std::wstring& dir);
  void OnSaved(int id, SaveResult* r);
  void OpenItem(int id);
  void RevealItem(int id);
  void DiscardItem(int id);
  void ForgetItem(int id);
  void CheckVanished();

  // Sending.
  void SendTo(const std::string& stableId, std::vector<std::wstring> paths);
  void OnJobProgress(int id, JobProgress* p);
  SendJob* FindJob(int id);
  void CancelJob(int id);
  void RetryJob(int id);
  void DismissJob(int id);

  // The WinUI process.
  void EnsureUi(bool warm);
  void KillUi();
  void Show(const std::string& json);  // queued until the UI connects
  void SendShow(ShowMsg m);
  void QueueShow(ShowMsg m);
  void CheckUiAlive();
  void ShowFlyout(bool dropMode);
  void ShowWhat(const char* what);
  void OnUiConnected();
  void OnUiCommand(const std::string& line);
  void OnUiDisconnected(DWORD pid);
  void Publish(unsigned what);  // coalesced; flushed by kTimerPublish
  void Flush();
  std::string StateJson() const;
  std::string SettingsJson() const;
  std::string DevicesJson() const;
  std::string ItemsJson() const;
  std::string JobsJson() const;
  std::string TrayJson() const;
  void ApplySetting(const std::string& key, const json::Value& value);

  void OpenTailscale();
  void OpenInbox();

  HWND host_ = nullptr;
  UINT taskbarCreated_ = 0;
  Conn conn_ = Conn::Unknown;
  std::wstring selfName_, tailnet_;
  std::vector<Device> devices_;
  std::vector<td::Favorite> favorites_;
  std::map<std::string, DeviceKind> kindOverrides_;
  std::map<std::string, std::wstring> nicknames_;
  std::vector<std::string> recent_;  // devices files were sent to, newest first
  Settings settings_;

  std::vector<std::unique_ptr<Item>> items_;
  std::vector<std::unique_ptr<SendJob>> jobs_;
  int nextId_ = 1;
  int openBatch_ = 0;
  ULONGLONG openBatchAt_ = 0;

  UiLink ui_;
  bool uiReady_ = false;  // connected and given the initial state
  HANDLE uiProcess_ = nullptr;
  ULONGLONG uiStarted_ = 0;
  int uiFailures_ = 0;  // starts in a row that never connected
  std::vector<ShowMsg> pendingShows_, unacked_;
  int showSeq_ = 0;
  unsigned dirty_ = 0;

  HANDLE stop_ = nullptr;
  HANDLE busKick_ = nullptr;
  ULONGLONG resumedAt_ = 0;
  bool devicesInFlight_ = false, devicesAgain_ = false;
  bool pulling_ = false;
  bool trayDragShown_ = false;
  bool errorShownOnce_ = false;
  bool landingTimer_ = false;
  ULONGLONG forcedPullAt_ = 0;
  HICON trayIcon_ = nullptr;
  int trayIconKind_ = -1;
};

enum : unsigned { kPubState = 1, kPubSettings = 2, kPubDevices = 4, kPubItems = 8, kPubJobs = 16, kPubAll = 31 };

enum : UINT_PTR {
  kTimerLanding = 1,
  kTimerDevices,
  kTimerTrayDragEnd,
  kTimerStartupCheck,
  kTimerVanished,
  kTimerPublish,
  kTimerJobs,
  kTimerTrim,
  kTimerUiWatch,
  kTimerUiRespawn,
};
