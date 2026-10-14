// TailDrop's windows: tray flyout, notifications, device picker, settings
// and the "Tailscale isn't connected" screen.
#pragma once

#include "app.h"
#include "ui.h"

// The app icon as an Image, cached per pixel size.
ui::Image* AppIconImage(int sizePx);
void ReleaseAppIconImages();

// Notification-style popup stacked above the tray (see App::LayoutToasts).
class ToastBase : public ui::Window {
 public:
  ~ToastBase() override;
  virtual float DesiredHeight() = 0;
  void Place(int x, int y, int w, int h, bool show);
  void Dismiss();
  // Auto-dismiss after ms; paused while the pointer is over the toast.
  void ArmAutoDismiss(UINT ms);
  void CancelAutoDismiss();
  bool created() const { return hwnd_ != nullptr; }

 protected:
  enum { kIdClose = 1, kTimerDismiss = 100 };
  static constexpr float kWidth = 360;
  void PaintHeader(ui::Canvas& c, float w, const std::wstring& caption);
  void OnTimer(UINT_PTR id) override;
  void OnEscape() override { Dismiss(); }
  ULONGLONG dismissAt_ = 0;
};

class ReceiveToast : public ToastBase {
 public:
  explicit ReceiveToast(int batch) : batch_(batch) {}
  int batch() const { return batch_; }
  float DesiredHeight() override;
  void Changed();

 protected:
  void Paint(ui::Canvas& c, float w, float h) override;
  void OnClick(int id) override;
  bool IsDraggable(int id) const override;
  void OnDragStart(int id) override;

 private:
  enum {
    kIdSave = 10,
    kIdOpen,
    kIdMore,
    kIdDest,
    kIdReveal,
    kIdPreview = 100,  // + index for individual thumbnails
  };
  std::vector<Item*> Items() const;
  int batch_;
  bool autoArmed_ = false;
  std::wstring dest_;  // per-notification destination override
};

class SendToast : public ToastBase {
 public:
  explicit SendToast(int job) : job_(job) {}
  int job() const { return job_; }
  float DesiredHeight() override;
  void Changed();

 protected:
  void Paint(ui::Canvas& c, float w, float h) override;
  void OnClick(int id) override;

 private:
  enum { kIdCancel = 10, kIdRetry, kIdDismiss };
  int job_;
};

class Flyout : public ui::Window {
 public:
  void Show(bool dropMode);
  void Hide();
  bool dropMode() const { return dropMode_; }
  void Changed();

 protected:
  void Paint(ui::Canvas& c, float w, float h) override;
  void OnClick(int id) override;
  void OnRightClick(int id, POINT screen) override;
  bool IsDraggable(int id) const override;
  void OnDragStart(int id) override;
  void OnActivate(bool active) override;
  void OnEscape() override { App::Get().HideFlyout(); }
  DWORD OnDragOver(int id, const std::vector<std::wstring>& files) override;
  void DropLabel(int id, DROPIMAGETYPE& type, std::wstring& message, std::wstring& insert) override;
  void OnDrop(int id, std::vector<std::wstring> files) override;
  void OnDragLeave() override;

 private:
  enum {
    kIdSettings = 1,
    kIdInbox,
    kIdOpenTailscale,
    kIdRetry,
    kIdAllDevices,
    kIdDropZone,
    kIdSendFiles,
    kIdSaveAll,
    kIdDevice = 1 << 24,     // + tile index
    kIdItem = 2 << 24,       // + item id (row, draggable)
    kIdItemSave = 3 << 24,   // + item id
    kIdItemOpen = 4 << 24,   // + item id
    kIdItemClose = 5 << 24,  // + item id
    kIdMask = 0xFFFFFF,
  };
  float Render(ui::Canvas* c, float w);
  void Reposition();
  std::vector<const Device*> TileDevices() const;
  std::vector<Item*> ListItems() const;
  bool dropMode_ = false;
  std::vector<const Device*> tiles_;
};

class PickerWindow : public ui::Window {
 public:
  void Open(std::vector<std::wstring> paths);
  void Changed() { Invalidate(); }

 protected:
  void Paint(ui::Canvas& c, float w, float h) override;
  void OnClick(int id) override;
  void OnWheel(float delta) override;
  void OnTimer(UINT_PTR id) override;

 private:
  enum { kIdCancel = 1, kIdStar = 1000, kIdRow = 2000 };
  std::vector<std::wstring> paths_;
  std::shared_ptr<ui::Image> thumb_;
  std::vector<const Device*> rows_;
  float scroll_ = 0, maxScroll_ = 0;
};

class SettingsWindow : public ui::Window {
 public:
  void Open();
  void Changed() { Invalidate(); }

 protected:
  void Paint(ui::Canvas& c, float w, float h) override;
  void OnClick(int id) override;
  void OnWheel(float delta) override;

 private:
  enum {
    kIdChangeDir = 1,
    kIdResetDir,
    kIdAutoSave,
    kIdTrayDrop,
    kIdRunAtLogin,
    kIdRepo,
    kIdOpenInbox,
    kIdStar = 1000,
  };
  std::vector<const Device*> rows_;
  float scroll_ = 0, maxScroll_ = 0;
};

class ErrorWindow : public ui::Window {
 public:
  void Open();
  void Changed() { Invalidate(); }

 protected:
  void Paint(ui::Canvas& c, float w, float h) override;
  void OnClick(int id) override;

 private:
  enum { kIdOpen = 1, kIdRetry, kIdClose };
};
