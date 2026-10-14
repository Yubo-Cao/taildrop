#include "views.h"

#include <algorithm>

#include "util.h"

using namespace ui;

namespace {
constexpr float kFlyoutWidth = 360;
constexpr int kMaxTiles = 9;
constexpr int kMaxRows = 4;

std::wstring ItemStatus(const Item* it) {
  switch (it->state) {
    case ItemState::Receiving: {
      int pct = it->size > 0 ? static_cast<int>(it->received * 100 / it->size) : 0;
      return L"Receiving… " + std::to_wstring(pct) + L"%";
    }
    case ItemState::Landing: return L"Finishing up…";
    case ItemState::Pending: return util::FormatSize(it->size) + L" · Waiting to be saved";
    case ItemState::Saving: return L"Saving…";
    case ItemState::Saved: return L"Saved to " + util::FolderDisplayName(util::ParentDir(it->path));
    case ItemState::Failed: return it->note.empty() ? L"Failed" : it->note;
  }
  return L"";
}
}  // namespace

std::vector<const Device*> Flyout::TileDevices() const { return App::Get().SortedDevices(); }

std::vector<Item*> Flyout::ListItems() const {
  std::vector<Item*> out;
  const auto& items = App::Get().items();
  for (auto i = items.rbegin(); i != items.rend() && out.size() < kMaxRows; ++i) out.push_back(i->get());
  return out;
}

// Lays out (and, with a canvas, draws) the flyout; returns its height.
float Flyout::Render(Canvas* c, float w) {
  App& app = App::Get();
  const Theme& th = c ? c->th : SystemTheme();
  const float pad = 16, inner = w - 2 * pad;
  float y = 0;

  // Header.
  if (c) {
    c->DrawImage(AppIconImage(Px(20)), R(pad, 18, 20, 20), 0, false);
    c->Text(L"TailDrop", R(pad + 30, 11, inner - 120, 20), 14, th.text, 600);
    bool ok = app.connected();
    c->Circle(pad + 33, 41, 3.5f, ok ? th.success : th.caution);
    std::wstring status = ok ? app.selfName() + (app.tailnet().empty() ? L"" : L" · " + app.tailnet())
                             : std::wstring(L"Tailscale isn't connected");
    c->Text(status, R(pad + 42, 32, inner - 130, 18), 12, th.text2);
    c->IconButton(kIdInbox, R(w - pad - 76, 14, 36, 32), glyph::kFolderOpen, 16);
    c->IconButton(kIdSettings, R(w - pad - 36, 14, 36, 32), glyph::kSettings, 16);
  }
  y = 62;

  if (!app.connected()) {
    Rect card = R(pad, y, inner, 172);
    if (c) {
      c->Fill(card, th.card, 8);
      c->Stroke(card, th.cardStroke, 8);
      c->Glyph(glyph::kWarning, R(card.left + 16, card.top + 16, 28, 28), 24, th.caution);
      c->Text(L"Tailscale isn't connected", R(card.left + 56, card.top + 16, inner - 72, 22), 14, th.text, 600);
      c->Text(app.ConnMessage(), R(card.left + 56, card.top + 42, inner - 72, 56), 12, th.text2, 400,
              kWrap | kVTop);
      c->Button(kIdOpenTailscale, R(card.left + 56, card.top + 120, 136, 32), L"Open Tailscale",
                ButtonStyle::Accent);
      c->Button(kIdRetry, R(card.left + 200, card.top + 120, 88, 32), L"Retry");
    }
    y += 172 + 16;
  } else {
    tiles_ = TileDevices();
    if (c) {
      c->Text(dropMode_ || dragActive_ ? L"Drop on a device to send" : L"Send to", R(pad, y, inner, 20), 12,
              dropMode_ || dragActive_ ? th.accentText : th.text2, 600);
    }
    y += 26;
    int n = static_cast<int>(std::min<size_t>(tiles_.size(), kMaxTiles));
    bool more = tiles_.size() > kMaxTiles;
    if (more) n = kMaxTiles - 1;
    int count = n + (more ? 1 : 0);
    const float gap = 8, tw = (inner - 2 * gap) / 3, tileH = 84;
    int rows = (count + 2) / 3;
    float zoneH = count ? rows * tileH + (rows - 1) * gap : 44;
    Rect zone = R(pad - 6, y - 6, inner + 12, zoneH + 12);
    if (c) {
      c->Hit(kIdDropZone, zone);
      if (c->DropHover(kIdDropZone)) {
        c->Fill(zone, WithAlpha(th.accent, 0.10f), 8);
        c->Stroke(zone, th.accent, 8);
      }
      if (!count)
        c->Text(L"No other devices in your tailnet can receive files.", R(pad, y, inner, 44), 12, th.text3);
    }
    for (int i = 0; i < count; ++i) {
      Rect r = R(pad + (i % 3) * (tw + gap), y + (i / 3) * (tileH + gap), tw, tileH);
      if (!c) continue;
      int id = kIdDevice + i;
      bool isMore = more && i == n;
      const Device* d = isMore ? nullptr : tiles_[i];
      bool online = isMore || d->online;
      bool hov = c->Hover(id), drop = c->DropHover(id) && online;
      c->Hit(id, r);
      c->Fill(r, drop ? WithAlpha(th.accent, 0.16f) : hov && online ? th.ctrlHover : th.ctrl, 6);
      if (drop) c->Stroke(r, th.accent, 6, 2);
      else c->Stroke(r, th.ctrlStroke, 6);
      Color fg = drop ? th.accentText : online ? th.text : th.textDisabled;
      c->Glyph(isMore ? glyph::kMore : KindGlyph(d->kind), R(r.left, r.top + 12, tw, 30), 24, fg);
      c->Text(isMore ? L"More" : d->name, R(r.left + 6, r.top + 46, tw - 12, 18), 12, online ? th.text : th.text3,
              400, kAlignCenter);
      if (!isMore && !d->online)
        c->Text(L"Offline", R(r.left + 6, r.top + 63, tw - 12, 14), 11, th.text3, 400, kAlignCenter);
      if (!isMore && app.IsFavorite(d->stableId))
        c->Glyph(glyph::kStarFill, R(r.right - 22, r.top + 4, 18, 18), 10, th.accentText);
    }
    y += zoneH + 16;
    if (c) c->Fill(R(0, y, w, 1.0f / scale_), th.divider);
    y += 12;

    // Received files.
    auto items = ListItems();
    int pending = 0;
    for (const auto& it : app.items())
      if (it->state == ItemState::Pending) ++pending;
    if (c) {
      c->Text(L"Received", R(pad, y, inner, 20), 12, th.text2, 600);
      if (pending > 1) {
        Rect link = R(w - pad - 72, y - 4, 78, 28);
        c->Button(kIdSaveAll, link, L"Save all", ButtonStyle::Subtle);
      }
    }
    y += 26;
    if (items.empty()) {
      if (c) c->Text(L"Files sent to this PC show up here.", R(pad, y, inner, 36), 12, th.text3, 400, kVTop);
      y += 36;
    }
    for (Item* it : items) {
      Rect row = R(pad - 8, y, inner + 16, 52);
      if (c) {
        int rid = kIdItem + it->id;
        c->Hit(rid, row);
        bool hov = c->Hover(rid) || c->Hover(kIdItemSave + it->id) || c->Hover(kIdItemOpen + it->id) ||
                   c->Hover(kIdItemClose + it->id);
        if (hov) c->Fill(row, c->Pressed(rid) ? th.subtlePressed : th.subtleHover, 6);
        Rect thumb = R(pad, y + 6, 40, 40);
        c->Fill(thumb, th.layer, 4);
        if (!it->thumb) App::Get().RequestThumb(it);
        if (it->thumb) c->DrawImage(it->thumb.get(), it->thumb->isIcon ? Inset(thumb, 4, 4) : thumb, 4,
                                    !it->thumb->isIcon);
        else c->Glyph(glyph::kDocument, thumb, 18, th.text2);
        float textRight = row.right - (hov ? 120 : 8);
        c->Text(it->name, R(pad + 52, y + 7, textRight - pad - 52, 20), 14, th.text);
        Color sc = it->state == ItemState::Failed ? th.critical : th.text2;
        c->Text(ItemStatus(it), R(pad + 52, y + 28, textRight - pad - 52, 16), 12, sc);
        if (it->state == ItemState::Receiving)
          c->Progress(R(pad + 52, y + 46, inner - 60, 4),
                      it->size > 0 ? static_cast<double>(it->received) / it->size : 0);
        if (hov) {
          float bx = row.right - 4;
          bool removable = it->state != ItemState::Receiving && it->state != ItemState::Landing;
          if (removable) {
            bx -= 36;
            c->IconButton(kIdItemClose + it->id, R(bx, y + 10, 36, 32),
                          it->state == ItemState::Pending ? glyph::kDelete : glyph::kClose, 14);
          }
          if (it->state == ItemState::Pending || it->state == ItemState::Saved) {
            bx -= 36;
            c->IconButton(kIdItemOpen + it->id, R(bx, y + 10, 36, 32), glyph::kOpenInNew, 14);
          }
          if (it->state == ItemState::Pending) {
            bx -= 36;
            c->IconButton(kIdItemSave + it->id, R(bx, y + 10, 36, 32), glyph::kDownload, 14);
          }
        }
      }
      y += 54;
    }
    y += 6;
  }

  // Footer.
  if (c) c->Fill(R(0, y, w, 1.0f / scale_), th.divider);
  y += 6;
  if (c) {
    c->Button(kIdInbox, R(pad - 8, y, 150, 36), L"Received files", ButtonStyle::Subtle, glyph::kFolder);
    std::wstring dest = L"Saves to " + util::FolderDisplayName(app.settings().EffectiveDownloadDir());
    float dw = std::min(c->TextWidth(dest, 12) + 16, inner - 150);
    c->Button(kIdSettings + 100, R(w - pad - dw + 8, y + 2, dw, 32), L"", ButtonStyle::Subtle);
    c->Text(dest, R(w - pad - dw + 16, y + 2, dw - 16, 32), 12, th.text2);
  }
  y += 36 + 8;
  return y;
}

void Flyout::Paint(Canvas& c, float w, float h) { Render(&c, w); }

void Flyout::Reposition() {
  App& app = App::Get();
  RECT ir = app.TrayIconRect();
  POINT anchor{(ir.left + ir.right) / 2, (ir.top + ir.bottom) / 2};
  if (ir.right <= ir.left) {
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    anchor = {wa.right - 1, wa.bottom - 1};
  }
  RECT work = WorkAreaForPoint(anchor);
  float s = hwnd_ ? scale_ : DpiScaleForPoint(anchor);
  int w = static_cast<int>(kFlyoutWidth * s), h = static_cast<int>(Render(nullptr, kFlyoutWidth) * s);
  int m = static_cast<int>(12 * s);
  int x = std::clamp(static_cast<int>(anchor.x - w / 2), static_cast<int>(work.left + m),
                     static_cast<int>(work.right - m - w));
  bool top = ir.right > ir.left && ir.bottom <= work.top + 4;
  int y = top ? work.top + m : work.bottom - m - h;
  if (!hwnd_) {
    RECT r{x, y, x + w, y + h};
    if (!CreateWin(WindowKind::Popup, L"TailDrop", r, true)) return;
    EnableDrop();
    onDestroyed = [](Window*) {};
    return;
  }
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void Flyout::Show(bool dropMode) {
  dropMode_ = dropMode;
  hover_ = pressed_ = 0;
  Reposition();
  if (!hwnd_) return;
  Reposition();  // again with the window's own DPI
  if (dropMode) {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  } else {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
  }
  Invalidate();
}

void Flyout::Hide() {
  if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
  dropMode_ = false;
}

void Flyout::Changed() {
  if (!visible()) return;
  RECT r;
  GetWindowRect(hwnd_, &r);
  int h = Px(Render(nullptr, kFlyoutWidth));
  if (h != r.bottom - r.top) Reposition();
  Invalidate();
}

void Flyout::OnActivate(bool active) {
  if (active) {
    dropMode_ = false;
    return;
  }
  if (visible() && !dropMode_) App::Get().HideFlyout();
}

bool Flyout::IsDraggable(int id) const { return (id & ~kIdMask) == kIdItem; }

void Flyout::OnDragStart(int id) { App::Get().DragItems(hwnd_, {id & kIdMask}); }

void Flyout::OnClick(int id) {
  App& app = App::Get();
  int kind = id & ~kIdMask, arg = id & kIdMask;
  if (kind == kIdDevice) {
    bool more = tiles_.size() > kMaxTiles;
    if (more && arg == kMaxTiles - 1) {
      app.HideFlyout();
      auto files = util::PickFiles(app.host(), L"Choose files to send with TailDrop");
      if (!files.empty()) app.ShowPicker(files);
      return;
    }
    if (arg < static_cast<int>(tiles_.size()) && tiles_[arg]->online) {
      std::string sid = tiles_[arg]->stableId;
      app.HideFlyout();
      app.PickAndSend(sid);
    }
    return;
  }
  if (kind == kIdItem) {
    app.OpenItem(arg);
    return;
  }
  if (kind == kIdItemSave) {
    app.SaveItem(arg);
    return;
  }
  if (kind == kIdItemOpen) {
    app.OpenItem(arg);
    return;
  }
  if (kind == kIdItemClose) {
    Item* it = app.FindItem(arg);
    if (it && it->state == ItemState::Pending) app.DiscardItem(arg);
    else app.ForgetItem(arg);
    return;
  }
  switch (id) {
    case kIdSettings:
    case kIdSettings + 100:
      app.ShowSettings();
      break;
    case kIdInbox:
      app.HideFlyout();
      app.OpenInbox();
      break;
    case kIdOpenTailscale:
      app.HideFlyout();
      app.OpenTailscale();
      break;
    case kIdRetry:
      app.RefreshDevices();
      break;
    case kIdSaveAll: {
      std::vector<int> ids;
      for (const auto& it : app.items())
        if (it->state == ItemState::Pending) ids.push_back(it->id);
      for (int i : ids) app.SaveItem(i);
      break;
    }
  }
}

void Flyout::OnRightClick(int id, POINT screen) {
  App& app = App::Get();
  if ((id & ~kIdMask) != kIdItem && (id & ~kIdMask) != kIdItemSave && (id & ~kIdMask) != kIdItemOpen &&
      (id & ~kIdMask) != kIdItemClose)
    return;
  int itemId = id & kIdMask;
  Item* it = app.FindItem(itemId);
  if (!it) return;
  enum { kSave = 1, kSaveTo, kOpen, kReveal, kDelete, kForget };
  bool pending = it->state == ItemState::Pending, saved = it->state == ItemState::Saved;
  std::vector<MenuItem> menu;
  if (pending) {
    menu.push_back({kSave, L"Save to " + util::FolderDisplayName(app.settings().EffectiveDownloadDir())});
    menu.push_back({kSaveTo, L"Save to…"});
    menu.push_back({0});
  }
  menu.push_back({kOpen, L"Open", false, !(pending || saved)});
  menu.push_back({kReveal, L"Show in folder", false, !(pending || saved)});
  menu.push_back({0});
  if (pending) menu.push_back({kDelete, L"Delete"});
  else menu.push_back({kForget, L"Remove from list"});
  switch (ShowMenu(hwnd_, screen, menu)) {
    case kSave: app.SaveItem(itemId); break;
    case kSaveTo: {
      std::wstring dir = util::PickFolder(hwnd_, app.settings().EffectiveDownloadDir(), L"Save to");
      if (!dir.empty()) app.SaveItem(itemId, dir);
      break;
    }
    case kOpen: app.OpenItem(itemId); break;
    case kReveal: app.RevealItem(itemId); break;
    case kDelete: app.DiscardItem(itemId); break;
    case kForget: app.ForgetItem(itemId); break;
  }
}

DWORD Flyout::OnDragOver(int id, const std::vector<std::wstring>& files) {
  if (!App::Get().connected() || files.empty()) return DROPEFFECT_NONE;
  if ((id & ~kIdMask) == kIdDevice) {
    int i = id & kIdMask;
    bool more = tiles_.size() > kMaxTiles && i == kMaxTiles - 1;
    if (more) return DROPEFFECT_COPY;
    return i < static_cast<int>(tiles_.size()) && tiles_[i]->online ? DROPEFFECT_COPY : DROPEFFECT_NONE;
  }
  return id == kIdDropZone ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

void Flyout::DropLabel(int id, DROPIMAGETYPE& type, std::wstring& message, std::wstring& insert) {
  if ((id & ~kIdMask) == kIdDevice) {
    int i = id & kIdMask;
    if (tiles_.size() > kMaxTiles && i == kMaxTiles - 1) {
      message = L"Choose a device";
      return;
    }
    if (i < static_cast<int>(tiles_.size())) {
      insert = tiles_[i]->name;
      message = tiles_[i]->online ? L"Send to %1" : L"%1 is offline";
    }
  } else if (id == kIdDropZone) {
    message = L"Choose a device";
  }
}

void Flyout::OnDrop(int id, std::vector<std::wstring> files) {
  App& app = App::Get();
  std::string target;
  if ((id & ~kIdMask) == kIdDevice) {
    int i = id & kIdMask;
    if (i < static_cast<int>(tiles_.size()) && !(tiles_.size() > kMaxTiles && i == kMaxTiles - 1))
      target = tiles_[i]->stableId;
  }
  app.HideFlyout();
  if (!target.empty()) app.SendTo(target, std::move(files));
  else app.ShowPicker(std::move(files));
}

void Flyout::OnDragLeave() { Invalidate(); }
