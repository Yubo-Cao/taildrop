#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "../common/shared.h"
#include "settings.h"
#include "ui.h"

// Messages posted to the host window by worker threads.
enum : UINT {
  WM_APP_TRAY = WM_APP + 1,
  WM_APP_CONN,          // wParam = Conn
  WM_APP_INCOMING,      // lParam = std::vector<Partial>*
  WM_APP_FILESWAITING,  //
  WM_APP_PEERSDIRTY,    //
  WM_APP_DEVICES,       // lParam = DeviceSnapshot*
  WM_APP_THUMB,         // wParam = item id, lParam = std::shared_ptr<ui::Image>*
  WM_APP_JOBPROGRESS,   // wParam = job id, lParam = JobProgress*
  WM_APP_SAVED,         // wParam = item id, lParam = SaveResult*
  WM_APP_PULLED,        // lParam = PulledFile*
  WM_APP_DELETEWINDOW,  // lParam = ui::Window*
  WM_APP_TRAYDRAG,      // wParam = 1 drag started, 2 hovering icon, 3 button released
};

enum class Conn { Unknown, NoService, OtherUser, NeedsLogin, NeedsMachineAuth, Stopped, Starting, Running };

enum class DeviceKind { PC, Laptop, Phone, Tablet, Server, Other };
wchar_t KindGlyph(DeviceKind k);
const wchar_t* KindKey(DeviceKind k);

struct Device {
  std::string stableId;
  int64_t nodeId = 0;
  std::wstring name;
  std::wstring dns;
  std::wstring os;
  DeviceKind kind = DeviceKind::Other;
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
  std::wstring landName;  // basename tailscaled finalized the file under
  std::shared_ptr<ui::Image> thumb;
  bool thumbRequested = false;
  std::wstring note;
};

struct Batch {
  int id = 0;
  std::vector<int> items;
  class ReceiveToast* toast = nullptr;
  ULONGLONG lastStart = 0;
};

enum class JobState { Preparing, Sending, Done, Failed, Cancelled };

struct SendJob {
  int id = 0;
  Device device;
  std::vector<std::wstring> paths;
  JobState state = JobState::Preparing;
  int64_t total = 0, sent = 0;
  int fileIndex = 0, fileCount = 0;
  std::wstring current, error;
  HANDLE cancel = nullptr;
  ULONGLONG started = 0;
  double rate = 0;
  class SendToast* toast = nullptr;
  std::shared_ptr<ui::Image> thumb;
};

struct JobProgress {
  JobState state;
  int64_t total, sent;
  int fileIndex, fileCount;
  std::wstring current, error;
};

struct SaveResult {
  bool ok;
  std::wstring path;
};

struct PulledFile {
  std::wstring name, path;
  int64_t size;
};

class Flyout;
class PickerWindow;
class SettingsWindow;
class ErrorWindow;
class ToastBase;

class App {
 public:
  static App& Get();
  int Run(const std::vector<std::wstring>& args);

  HWND host() const { return host_; }
  Conn conn() const { return conn_; }
  bool connected() const { return conn_ == Conn::Running; }
  std::wstring ConnMessage() const;
  const std::wstring& selfName() const { return selfName_; }
  const std::wstring& tailnet() const { return tailnet_; }
  const std::vector<Device>& devices() const { return devices_; }
  // Devices with favorites first, then online, then by name.
  std::vector<const Device*> SortedDevices() const;
  const Device* FindDevice(const std::string& stableId) const;
  bool IsFavorite(const std::string& stableId) const;
  void ToggleFavorite(const Device& d);
  const std::vector<td::Favorite>& favorites() const { return favorites_; }

  Settings& settings() { return settings_; }
  void SettingsChanged();

  const std::vector<std::unique_ptr<Item>>& items() const { return items_; }
  Item* FindItem(int id);
  Batch* FindBatch(int id);
  SendJob* FindJob(int id);
  int PendingCount() const;

  // Receiving.
  void SaveItem(int id, const std::wstring& dir = L"");
  void SaveBatch(int batch, const std::wstring& dir = L"");
  void OpenItem(int id);
  void RevealItem(int id);
  void DiscardItem(int id);
  void ForgetItem(int id);
  void DragItems(HWND from, const std::vector<int>& ids);
  void RequestThumb(Item* it);

  // Sending.
  void SendTo(const std::string& stableId, std::vector<std::wstring> paths);
  void PickAndSend(const std::string& stableId);
  void CancelJob(int id);
  void RetryJob(int id);

  // Windows.
  void ToggleFlyout();
  void ShowFlyout(bool dropMode);
  void HideFlyout();
  void ShowPicker(std::vector<std::wstring> paths);
  void ShowSettings();
  void ShowError();
  void OpenTailscale();
  void OpenInbox();
  void RefreshDevices(bool soon = false);
  void LayoutToasts();
  void CloseToast(ToastBase* t);
  void Repaint();
  void ScheduleTrim();
  RECT TrayIconRect() const;

 private:
  App() = default;
  bool CreateHost();
  static LRESULT CALLBACK HostProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HostMessage(UINT, WPARAM, LPARAM);
  void HandleArgs(const std::vector<std::wstring>& args);
  void OnCopyData(const COPYDATASTRUCT* cds);

  // Tray.
  void TrayAdd();
  void TrayUpdate();
  void TrayMenu();
  void StartDragWatch();
  void StopDragWatch();

  // Tailscale.
  void StartBus();
  void SetConn(Conn c);
  void OnDevices(DeviceSnapshot* snap);
  void OnIncoming(std::vector<Partial>* list);
  void OnFilesWaiting();
  void OnPulled(PulledFile* f);
  void PollLanding();
  bool TryLand(Item* it);
  void ScanInbox();

  void OnJobProgress(int id, JobProgress* p);
  void OnSaved(int id, SaveResult* r);
  Item* NewItem(const std::wstring& name);
  void AttachToBatch(Item* it, bool allowJoin);
  void ItemReady(Item* it);
  void UpdateBatch(int batch);
  void DeleteWindow(ui::Window* w);
  void MaybeTrim();

  HWND host_ = nullptr;
  UINT taskbarCreated_ = 0;
  Conn conn_ = Conn::Unknown;
  std::wstring selfName_, tailnet_;
  std::vector<Device> devices_;
  std::vector<td::Favorite> favorites_;
  Settings settings_;

  std::vector<std::unique_ptr<Item>> items_;
  std::vector<std::unique_ptr<Batch>> batches_;
  std::vector<std::unique_ptr<SendJob>> jobs_;
  int nextId_ = 1;

  std::unique_ptr<Flyout> flyout_;
  std::unique_ptr<PickerWindow> picker_;
  std::unique_ptr<SettingsWindow> settingsWnd_;
  std::unique_ptr<ErrorWindow> errorWnd_;
  std::vector<ToastBase*> toasts_;  // owned; deleted via WM_APP_DELETEWINDOW

  HANDLE stop_ = nullptr;
  bool devicesInFlight_ = false, devicesAgain_ = false;
  bool pulling_ = false;
  ULONGLONG flyoutHiddenAt_ = 0;
  bool trayDragShown_ = false;
  bool errorShownOnce_ = false;
  bool landingTimer_ = false;
  HICON trayIcon_ = nullptr;
  int trayIconKind_ = -1;
};

enum : UINT_PTR { kTimerLanding = 1, kTimerDevices = 2, kTimerTrim = 3, kTimerTrayDragEnd = 4, kTimerStartup = 5 };
