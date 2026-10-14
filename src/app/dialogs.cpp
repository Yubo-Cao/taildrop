// Regular (Mica) windows: device picker, settings and the error screen.
#include "views.h"

#include <shellapi.h>

#include <algorithm>

#include "resource.h"
#include "util.h"
#include "views.h"

using namespace ui;

namespace {

// Centers a client area of w x h DIPs on the monitor under the cursor.
RECT CenteredClientRect(float w, float h) {
  POINT pt;
  GetCursorPos(&pt);
  RECT work = WorkAreaForPoint(pt);
  float s = DpiScaleForPoint(pt);
  int pw = static_cast<int>(w * s), ph = static_cast<int>(h * s);
  int x = (work.left + work.right - pw) / 2, y = (work.top + work.bottom - ph) / 2;
  return {x, y, x + pw, y + ph};
}

void BringToFront(HWND hwnd) {
  ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(hwnd);
  if (GetForegroundWindow() != hwnd) {
    // Foreground lock: flash politely instead of stealing focus.
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    FLASHWINFO fi{sizeof(fi), hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0};
    FlashWindowEx(&fi);
  }
}

std::wstring DeviceStatus(const Device* d) {
  std::wstring os = d->os.empty() ? L"" : L" · " + d->os;
  return (d->online ? L"Online" : L"Offline") + os;
}

void DeviceRow(Canvas& c, const Device* d, const Rect& row, int rowId, int starId, bool clickable) {
  const Theme& th = c.th;
  if (clickable) {
    c.Hit(rowId, row);
    if (c.Hover(rowId)) c.Fill(row, c.Pressed(rowId) ? th.subtlePressed : th.subtleHover, 6);
  }
  Color fg = d->online ? th.text : th.textDisabled;
  c.Glyph(KindGlyph(d->kind), R(row.left + 8, row.top, 36, Ht(row)), 20, fg);
  c.Text(d->name, R(row.left + 52, row.top + 8, Wd(row) - 110, 20), 14, d->online ? th.text : th.text2);
  c.Circle(row.left + 56, row.top + 37, 3, d->online ? th.success : th.textDisabled);
  c.Text(DeviceStatus(d), R(row.left + 64, row.top + 28, Wd(row) - 122, 18), 12, th.text2);
  bool fav = App::Get().IsFavorite(d->stableId);
  Color starColor = fav ? th.accentText : th.text2;
  c.IconButton(starId, R(row.right - 44, row.top + (Ht(row) - 32) / 2, 36, 32),
               fav ? glyph::kStarFill : glyph::kStar, 14, &starColor);
}

}  // namespace

// ---------------------------------------------------------------------------
// PickerWindow

void PickerWindow::Open(std::vector<std::wstring> paths) {
  paths_ = std::move(paths);
  scroll_ = 0;
  thumb_.reset();
  if (!hwnd_) {
    if (!CreateWin(WindowKind::Normal, L"Send with TailDrop", CenteredClientRect(420, 520), false)) return;
    onDestroyed = [](Window*) { App::Get().ScheduleTrim(); };
  }
  thumb_ = LoadThumbnail(paths_[0], Px(40));
  SetTimer(hwnd_, 1, 3000, nullptr);  // keep online status fresh while open
  BringToFront(hwnd_);
  Invalidate();
}

void PickerWindow::OnTimer(UINT_PTR) { App::Get().RefreshDevices(); }

void PickerWindow::Paint(Canvas& c, float w, float h) {
  App& app = App::Get();
  const Theme& th = c.th;
  const float pad = 20, inner = w - 2 * pad;

  // What is being sent.
  Rect card = R(pad, 16, inner, 68);
  c.Fill(card, th.card, 8);
  c.Stroke(card, th.cardStroke, 8);
  Rect icon = R(card.left + 14, card.top + 14, 40, 40);
  if (thumb_) c.DrawImage(thumb_.get(), thumb_->isIcon ? icon : icon, 4, !thumb_->isIcon);
  int64_t total = 0;
  bool folder = false;
  for (const auto& p : paths_) {
    if (util::IsDirectory(p)) folder = true;
    else total += std::max<int64_t>(0, util::FileSize(p));
  }
  std::wstring title = paths_.size() == 1 ? util::FileName(paths_[0])
                                          : util::FileName(paths_[0]) + L" and " +
                                                std::to_wstring(paths_.size() - 1) + L" more";
  std::wstring sub = folder ? (total ? util::FormatSize(total) + L" · " : L"") + L"Folders are sent as .zip"
                            : util::FormatSize(total);
  c.Text(title, R(card.left + 66, card.top + 14, inner - 80, 20), 14, th.text, 600);
  c.Text(sub, R(card.left + 66, card.top + 36, inner - 80, 18), 12, th.text2);

  c.Text(L"Choose a device", R(pad, 100, inner, 22), 14, th.text, 600);

  Rect list = R(pad - 8, 130, inner + 16, h - 130 - 64);
  rows_ = app.SortedDevices();
  const float rowH = 56;
  maxScroll_ = std::max(0.0f, rows_.size() * rowH - Ht(list));
  scroll_ = std::clamp(scroll_, 0.0f, maxScroll_);
  c.PushClip(list);
  if (rows_.empty())
    c.Text(L"No other devices in your tailnet can receive files.", Inset(list, 8, 0), 12, th.text3, 400, kVTop);
  for (size_t i = 0; i < rows_.size(); ++i) {
    Rect row = R(list.left, list.top + i * rowH - scroll_, Wd(list), rowH - 4);
    if (row.bottom < list.top || row.top > list.bottom) continue;
    DeviceRow(c, rows_[i], row, kIdRow + static_cast<int>(i), kIdStar + static_cast<int>(i), rows_[i]->online);
  }
  c.PopClip();
  // Hits outside the clip must not register; trim them.
  hits_.erase(std::remove_if(hits_.begin(), hits_.end(),
                             [&](const std::pair<int, Rect>& hit) {
                               return hit.first >= kIdStar && (hit.second.bottom <= list.top || hit.second.top >= list.bottom);
                             }),
              hits_.end());

  c.Fill(R(0, h - 64, w, 1.0f / scale_), th.divider);
  c.Text(L"Starred devices appear in Explorer's menu.", R(pad, h - 50, inner - 110, 36), 12, th.text3);
  c.Button(kIdCancel, R(w - pad - 96, h - 48, 96, 32), L"Cancel");
}

void PickerWindow::OnClick(int id) {
  App& app = App::Get();
  if (id == kIdCancel) {
    Close();
    return;
  }
  if (id >= kIdRow && id < kIdRow + static_cast<int>(rows_.size())) {
    const Device* d = rows_[id - kIdRow];
    if (!d->online) return;
    std::string sid = d->stableId;
    auto paths = paths_;
    Close();
    app.SendTo(sid, paths);
    return;
  }
  if (id >= kIdStar && id < kIdStar + static_cast<int>(rows_.size())) app.ToggleFavorite(*rows_[id - kIdStar]);
}

void PickerWindow::OnWheel(float delta) {
  scroll_ = std::clamp(scroll_ - delta * 56, 0.0f, maxScroll_);
  Invalidate();
}

// ---------------------------------------------------------------------------
// SettingsWindow

void SettingsWindow::Open() {
  if (!hwnd_) {
    if (!CreateWin(WindowKind::Normal, L"TailDrop Settings", CenteredClientRect(560, 680), false)) return;
    onDestroyed = [](Window*) { App::Get().ScheduleTrim(); };
  }
  BringToFront(hwnd_);
  Invalidate();
}

namespace {

// controlW: width reserved on the right for the card's button or toggle.
void SettingCard(Canvas& c, const Rect& r, wchar_t gl, const std::wstring& title, const std::wstring& desc,
                 float controlW) {
  const Theme& th = c.th;
  c.Fill(r, th.card, 6);
  c.Stroke(r, th.cardStroke, 6);
  c.Glyph(gl, R(r.left + 16, r.top, 24, Ht(r)), 18, th.text);
  float tw = Wd(r) - 56 - (controlW > 0 ? controlW + 32 : 16);
  c.Text(title, R(r.left + 56, r.top + 13, tw, 20), 14, th.text);
  c.Text(desc, R(r.left + 56, r.top + 34, tw, 20), 12, th.text2);
}

bool ClassicMenuRegistered() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\YuboCao.TailDrop", 0, KEY_READ, &k) != ERROR_SUCCESS)
    return false;
  RegCloseKey(k);
  return true;
}

}  // namespace

void SettingsWindow::Paint(Canvas& c, float w, float h) {
  App& app = App::Get();
  Settings& s = app.settings();
  const Theme& th = c.th;
  const float pad = 24, inner = w - 2 * pad;
  float y = 20 - scroll_;
  const float cardH = 68, gap = 4;

  c.Text(L"Settings", R(pad, y, inner, 36), 24, th.text, 600);
  y += 52;

  c.Text(L"Receiving", R(pad, y, inner, 20), 14, th.text, 600);
  y += 28;
  Rect r = R(pad, y, inner, cardH);
  std::wstring dir = s.EffectiveDownloadDir();
  bool custom = !s.downloadDir.empty();
  SettingCard(c, r, glyph::kDownload, L"Save received files to", dir, custom ? 184.0f : 100.0f);
  c.Button(kIdChangeDir, R(r.right - 16 - 100, r.top + 18, 100, 32), L"Change…");
  if (custom) c.Button(kIdResetDir, R(r.right - 16 - 100 - 8 - 76, r.top + 18, 76, 32), L"Reset");
  y += cardH + gap;
  r = R(pad, y, inner, cardH);
  SettingCard(c, r, glyph::kCheck, L"Save files automatically",
              L"Skip the Save button; files go straight to the folder above.", 40);
  c.Toggle(kIdAutoSave, R(r.right - 16 - 40, r.top + 24, 40, 20), s.autoSave);
  y += cardH + gap;
  r = R(pad, y, inner, cardH);
  SettingCard(c, r, glyph::kFolderOpen, L"Received files",
              L"Files wait here until you save them or drag them away.", 100);
  c.Button(kIdOpenInbox, R(r.right - 16 - 100, r.top + 18, 100, 32), L"Open");
  y += cardH + 20;

  c.Text(L"Sending", R(pad, y, inner, 20), 14, th.text, 600);
  y += 28;
  r = R(pad, y, inner, cardH);
  SettingCard(c, r, glyph::kUpload, L"Drop files on the tray icon",
              L"Dragging files onto the TailDrop icon opens a drop shelf.", 40);
  c.Toggle(kIdTrayDrop, R(r.right - 16 - 40, r.top + 24, 40, 20), s.trayDrop);
  y += cardH + gap;
  r = R(pad, y, inner, cardH);
  bool win11 = td::RegGetDword(L"Win11Menu", 0) != 0;
  SettingCard(c, r, glyph::kOpenFile, L"Explorer menu",
              win11 ? L"“Send with TailDrop” is in the Windows 11 menu."
                    : (ClassicMenuRegistered()
                           ? L"Under “Show more options”. Turn on Developer Mode and reinstall for the Windows 11 menu."
                           : L"Not registered. Reinstall TailDrop to add it."),
              0);
  y += cardH + 20;

  c.Text(L"Favorite devices", R(pad, y, inner, 20), 14, th.text, 600);
  y += 22;
  c.Text(L"Favorites come first in the flyout and appear directly in the Explorer menu (up to 3).",
         R(pad, y, inner, 20), 12, th.text2);
  y += 28;
  rows_ = app.SortedDevices();
  float listH = std::max<float>(56, rows_.size() * 56.0f + 8);
  r = R(pad, y, inner, listH);
  c.Fill(r, th.card, 6);
  c.Stroke(r, th.cardStroke, 6);
  if (rows_.empty()) c.Text(L"No devices yet.", Inset(r, 16, 0), 12, th.text3);
  for (size_t i = 0; i < rows_.size(); ++i) {
    Rect row = R(r.left + 4, r.top + 4 + i * 56.0f, Wd(r) - 8, 52);
    DeviceRow(c, rows_[i], row, 0, kIdStar + static_cast<int>(i), false);
    if (i + 1 < rows_.size()) c.Fill(R(row.left + 48, row.bottom + 1.5f, Wd(row) - 56, 1.0f / scale_), th.divider);
  }
  y += listH + 20;

  c.Text(L"General", R(pad, y, inner, 20), 14, th.text, 600);
  y += 28;
  r = R(pad, y, inner, cardH);
  SettingCard(c, r, 0xE7E8, L"Start with Windows", L"Keep TailDrop running in the background after you sign in.", 40);
  c.Toggle(kIdRunAtLogin, R(r.right - 16 - 40, r.top + 24, 40, 20), s.runAtLogin);
  y += cardH + gap;
  r = R(pad, y, inner, cardH);
  SettingCard(c, r, glyph::kInfo, L"TailDrop " TD_VERSION_W,
              L"Unofficial; not affiliated with Tailscale Inc.", 100);
  c.Button(kIdRepo, R(r.right - 16 - 100, r.top + 18, 100, 32), L"GitHub", ButtonStyle::Standard, glyph::kOpenInNew);
  y += cardH + 24;

  maxScroll_ = std::max(0.0f, y + scroll_ - h);
}

void SettingsWindow::OnClick(int id) {
  App& app = App::Get();
  Settings& s = app.settings();
  switch (id) {
    case kIdChangeDir: {
      std::wstring dir = util::PickFolder(hwnd_, s.EffectiveDownloadDir(), L"Save received files to");
      if (!dir.empty()) {
        s.downloadDir = dir;
        app.SettingsChanged();
      }
      break;
    }
    case kIdResetDir:
      s.downloadDir.clear();
      app.SettingsChanged();
      break;
    case kIdAutoSave:
      s.autoSave = !s.autoSave;
      app.SettingsChanged();
      break;
    case kIdTrayDrop:
      s.trayDrop = !s.trayDrop;
      app.SettingsChanged();
      break;
    case kIdRunAtLogin:
      s.runAtLogin = !s.runAtLogin;
      app.SettingsChanged();
      break;
    case kIdOpenInbox:
      app.OpenInbox();
      break;
    case kIdRepo:
      ShellExecuteW(nullptr, L"open", L"https://github.com/Yubo-Cao/taildrop", nullptr, nullptr, SW_SHOWNORMAL);
      break;
    default:
      if (id >= kIdStar && id < kIdStar + static_cast<int>(rows_.size())) app.ToggleFavorite(*rows_[id - kIdStar]);
  }
  Invalidate();
}

void SettingsWindow::OnWheel(float delta) {
  scroll_ = std::clamp(scroll_ - delta * 60, 0.0f, maxScroll_);
  Invalidate();
}

// ---------------------------------------------------------------------------
// ErrorWindow

void ErrorWindow::Open() {
  if (!hwnd_) {
    if (!CreateWin(WindowKind::Normal, L"TailDrop", CenteredClientRect(460, 260), false)) return;
    onDestroyed = [](Window*) { App::Get().ScheduleTrim(); };
  }
  BringToFront(hwnd_);
  Invalidate();
}

void ErrorWindow::Paint(Canvas& c, float w, float h) {
  App& app = App::Get();
  const Theme& th = c.th;
  const float pad = 28;
  c.Glyph(glyph::kWarning, R(pad, 30, 40, 40), 34, th.caution);
  c.Text(L"Tailscale isn't connected", R(pad + 60, 30, w - pad * 2 - 60, 30), 20, th.text, 600);
  c.Text(app.ConnMessage(), R(pad + 60, 66, w - pad * 2 - 60, 44), 14, th.text2, 400, kWrap | kVTop);
  c.Text(L"TailDrop sends and receives through Taildrop, so Tailscale has to be signed in on this PC.",
         R(pad + 60, 114, w - pad * 2 - 60, 40), 12, th.text3, 400, kWrap | kVTop);
  c.Fill(R(0, h - 76, w, h), th.layer);
  c.Fill(R(0, h - 76, w, 1.0f / scale_), th.divider);
  c.Button(kIdOpen, R(w - 24 - 150, h - 54, 150, 32), L"Open Tailscale", ButtonStyle::Accent);
  c.Button(kIdRetry, R(w - 24 - 150 - 8 - 96, h - 54, 96, 32), L"Retry");
}

void ErrorWindow::OnClick(int id) {
  App& app = App::Get();
  if (id == kIdOpen) app.OpenTailscale();
  if (id == kIdRetry) app.RefreshDevices();
}
