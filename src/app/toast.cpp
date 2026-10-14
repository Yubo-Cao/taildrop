#include "views.h"

#include <knownfolders.h>

#include <algorithm>
#include <map>

#include "resource.h"
#include "util.h"

using namespace ui;

namespace {
std::map<int, std::unique_ptr<ui::Image>> g_appIcons;
}

void ReleaseAppIconImages() { g_appIcons.clear(); }

ui::Image* AppIconImage(int sizePx) {
  auto& slot = g_appIcons[sizePx];
  if (slot) return slot.get();
  HICON icon = nullptr;
  if (FAILED(LoadIconWithScaleDown(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), sizePx, sizePx, &icon)))
    return nullptr;
  ICONINFO ii{};
  GetIconInfo(icon, &ii);
  auto img = std::make_unique<ui::Image>();
  img->w = img->h = sizePx;
  img->isIcon = true;
  img->px.resize(static_cast<size_t>(sizePx) * sizePx);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = sizePx;
  bi.bmiHeader.biHeight = -sizePx;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC dc = GetDC(nullptr);
  GetDIBits(dc, ii.hbmColor, 0, sizePx, img->px.data(), &bi, DIB_RGB_COLORS);
  ReleaseDC(nullptr, dc);
  if (ii.hbmColor) DeleteObject(ii.hbmColor);
  if (ii.hbmMask) DeleteObject(ii.hbmMask);
  DestroyIcon(icon);
  for (uint32_t& p : img->px) {  // icon bitmaps carry straight alpha
    uint32_t a = p >> 24;
    p = (a << 24) | ((((p >> 16) & 0xFF) * a / 255) << 16) | ((((p >> 8) & 0xFF) * a / 255) << 8) |
        ((p & 0xFF) * a / 255);
  }
  slot = std::move(img);
  return slot.get();
}

// ---------------------------------------------------------------------------

ToastBase::~ToastBase() = default;

void ToastBase::Place(int x, int y, int w, int h, bool show) {
  if (!hwnd_) {
    RECT r{x, y, x + w, y + h};
    if (!CreateWin(WindowKind::ToastPopup, L"TailDrop", r, true)) return;
    if (dismissAt_) SetTimer(hwnd_, kTimerDismiss, 400, nullptr);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    return;
  }
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : 0));
}

void ToastBase::Dismiss() {
  if (hwnd_) {
    Close();
  } else if (onDestroyed) {
    auto cb = onDestroyed;
    onDestroyed = nullptr;
    cb(this);
  }
}

void ToastBase::ArmAutoDismiss(UINT ms) {
  dismissAt_ = GetTickCount64() + ms;
  if (hwnd_) SetTimer(hwnd_, kTimerDismiss, 400, nullptr);
}

void ToastBase::CancelAutoDismiss() {
  dismissAt_ = 0;
  if (hwnd_) KillTimer(hwnd_, kTimerDismiss);
}

void ToastBase::OnTimer(UINT_PTR id) {
  if (id != kTimerDismiss || !dismissAt_) return;
  POINT pt;
  GetCursorPos(&pt);
  RECT r;
  GetWindowRect(hwnd_, &r);
  ULONGLONG now = GetTickCount64();
  if (PtInRect(&r, pt) || !IsWindowVisible(hwnd_)) {
    dismissAt_ = std::max(dismissAt_, now + 1500);
    return;
  }
  if (now >= dismissAt_) {
    KillTimer(hwnd_, kTimerDismiss);
    Dismiss();
  }
}

void ToastBase::PaintHeader(Canvas& c, float w, const std::wstring& caption) {
  c.DrawImage(AppIconImage(Px(16)), R(16, 14, 16, 16), 0, false);
  c.Text(caption, R(40, 10, w - 100, 24), 12, c.th.text2);
  c.IconButton(kIdClose, R(w - 44, 8, 36, 28), glyph::kClose, 12);
}

// ---------------------------------------------------------------------------

namespace {

struct ReceiveLayout {
  bool hero = false, multi = false, receiving = false, pending = false, saved = false, failed = false, landing = false;
  Rect preview{}, icon{}, title{}, sub{}, dest{}, progress{}, buttons{};
  float height = 0;
};

ReceiveLayout LayoutReceive(const std::vector<Item*>& items, float w, bool autoSave) {
  ReceiveLayout L;
  const float pad = 16;
  for (Item* it : items) {
    if (it->state == ItemState::Receiving) L.receiving = true;
    if (it->state == ItemState::Landing) L.landing = true;
    if (it->state == ItemState::Pending || it->state == ItemState::Saving) L.pending = true;
  }
  L.saved = !items.empty() && std::all_of(items.begin(), items.end(), [](Item* i) { return i->state == ItemState::Saved; });
  L.failed = !items.empty() && std::all_of(items.begin(), items.end(), [](Item* i) { return i->state == ItemState::Failed; });
  float y = 42;
  L.multi = items.size() > 1;
  L.hero = !L.multi && !items.empty() && items[0]->thumb && !items[0]->thumb->isIcon;
  float inner = w - 2 * pad;
  if (L.hero) {
    const Image* t = items[0]->thumb.get();
    float ph = std::clamp(inner * t->h / std::max(1, t->w), 120.0f, 200.0f);
    L.preview = R(pad, y, inner, ph);
    y += ph + 12;
    L.title = R(pad, y, inner, 20);
    L.sub = R(pad, y + 21, inner, 18);
    y += 44;
  } else if (L.multi) {
    int n = static_cast<int>(std::min<size_t>(4, items.size()));
    float size = std::min(76.0f, (inner - (n - 1) * 6) / n);
    L.preview = R(pad, y, inner, size);
    y += size + 12;
    L.title = R(pad, y, inner, 20);
    L.sub = R(pad, y + 21, inner, 18);
    y += 44;
  } else {
    L.icon = R(pad, y, 44, 44);
    L.title = R(pad + 56, y + 2, inner - 56, 20);
    L.sub = R(pad + 56, y + 23, inner - 56, 18);
    y += 52;
  }
  if ((L.receiving || L.landing) && !L.pending) {
    L.progress = R(pad, y, inner, 6);
    y += 14;
  }
  if (L.pending && !autoSave) {
    L.dest = R(pad - 6, y, inner + 12, 28);
    y += 32;
  }
  if (L.pending || L.saved || L.failed) {
    L.buttons = R(pad, y + 2, inner, 32);
    y += 34 + pad;
  } else {
    y += 8;
  }
  L.height = y;
  return L;
}

std::wstring BatchTitle(const std::vector<Item*>& items) {
  if (items.size() == 1) return items[0]->name;
  return std::to_wstring(items.size()) + L" files";
}

}  // namespace

std::vector<Item*> ReceiveToast::Items() const {
  std::vector<Item*> out;
  if (Batch* b = App::Get().FindBatch(batch_))
    for (int id : b->items)
      if (Item* it = App::Get().FindItem(id)) out.push_back(it);
  return out;
}

float ReceiveToast::DesiredHeight() {
  return LayoutReceive(Items(), kWidth, App::Get().settings().autoSave).height;
}

void ReceiveToast::Changed() {
  auto items = Items();
  ReceiveLayout L = LayoutReceive(items, kWidth, App::Get().settings().autoSave);
  if (L.saved) ArmAutoDismiss(6000);
  else if (L.failed) ArmAutoDismiss(15000);
  else if (L.pending && !autoArmed_) {
    ArmAutoDismiss(30000);
    autoArmed_ = true;
  }
  for (Item* it : items)
    if (!it->thumb) App::Get().RequestThumb(it);
  Invalidate();
}

void ReceiveToast::Paint(Canvas& c, float w, float h) {
  App& app = App::Get();
  auto items = Items();
  ReceiveLayout L = LayoutReceive(items, w, app.settings().autoSave);
  const Theme& th = c.th;
  PaintHeader(c, w, L.receiving ? L"TailDrop · Receiving" : L"TailDrop");
  if (items.empty()) return;

  int64_t total = 0, received = 0;
  for (Item* it : items) {
    total += std::max<int64_t>(0, it->size);
    received += it->state == ItemState::Receiving ? it->received : std::max<int64_t>(0, it->size);
  }
  bool canDrag = L.pending || L.saved;
  // Body: dragging anywhere on it drags every file in the batch.
  if (canDrag) c.Hit(kIdPreview + 99, R(0, 40, w, (L.buttons.top > 0 ? L.buttons.top : h) - 44));

  if (L.hero) {
    c.Fill(L.preview, th.layer, 6);
    c.DrawImage(items[0]->thumb.get(), L.preview, 6, true);
    c.Stroke(L.preview, th.cardStroke, 6);
    if (canDrag) c.Hit(kIdPreview, L.preview);
  } else if (L.multi) {
    int n = static_cast<int>(std::min<size_t>(4, items.size()));
    float size = Ht(L.preview);
    for (int i = 0; i < n; ++i) {
      Rect r = R(L.preview.left + i * (size + 6), L.preview.top, size, size);
      c.Fill(r, th.layer, 6);
      if (items[i]->thumb) c.DrawImage(items[i]->thumb.get(), items[i]->thumb->isIcon ? Inset(r, 12, 12) : r,
                                       6, !items[i]->thumb->isIcon);
      c.Stroke(r, th.cardStroke, 6);
      if (i == n - 1 && items.size() > 4) {
        c.Fill(r, Hex(0x000000, 0.45f), 6);
        c.Text(L"+" + std::to_wstring(items.size() - 3), r, 18, Hex(0xFFFFFF), 600, kAlignCenter);
      }
      if (items[i]->state == ItemState::Receiving || items[i]->state == ItemState::Landing) {
        c.Fill(r, WithAlpha(th.solidFlyout, 0.5f), 6);
        double f = items[i]->size > 0 ? static_cast<double>(items[i]->received) / items[i]->size : 0;
        c.Progress(R(r.left + 8, r.bottom - 12, size - 16, 4), f);
      }
      if (canDrag && (items[i]->state == ItemState::Pending || items[i]->state == ItemState::Saved))
        c.Hit(kIdPreview + 1 + i, r);
    }
  } else {
    Item* it = items[0];
    c.Fill(L.icon, th.layer, 6);
    if (it->thumb) c.DrawImage(it->thumb.get(), it->thumb->isIcon ? Inset(L.icon, 6, 6) : L.icon, 6, !it->thumb->isIcon);
    else c.Glyph(glyph::kDocument, L.icon, 22, th.text2);
    if (canDrag) c.Hit(kIdPreview, L.icon);
  }

  c.Text(BatchTitle(items), L.title, 14, th.text, 600);
  std::wstring sub;
  Color subColor = th.text2;
  if (L.receiving) {
    int pct = total > 0 ? static_cast<int>(received * 100 / total) : 0;
    sub = L"Receiving… " + std::to_wstring(pct) + L"%";
    if (total > 0) sub += L" · " + util::FormatSize(received) + L" of " + util::FormatSize(total);
  } else if (L.landing && !L.pending) {
    sub = L"Finishing up…";
  } else if (L.saved) {
    std::wstring dir = util::ParentDir(items[0]->path);
    sub = L"Saved to " + util::FolderDisplayName(dir);
    subColor = th.text2;
  } else if (L.failed) {
    sub = items[0]->note.empty() ? L"Something went wrong." : items[0]->note;
    subColor = th.critical;
  } else {
    sub = util::FormatSize(total);
    if (items.size() == 1) {
      std::wstring type = util::FileTypeName(items[0]->name);
      if (!type.empty()) sub += L" · " + type;
    }
    for (Item* it : items)
      if (!it->note.empty()) sub = it->note, subColor = th.critical;
  }
  if (L.saved) {
    c.Glyph(glyph::kCheck, R(L.sub.left, L.sub.top, 14, Ht(L.sub)), 12, th.success);
    c.Text(sub, {L.sub.left + 18, L.sub.top, L.sub.right, L.sub.bottom}, 12, subColor);
  } else {
    c.Text(sub, L.sub, 12, subColor);
  }

  if (Ht(L.progress) > 0) {
    double f = total > 0 ? static_cast<double>(received) / total : 0;
    c.Progress(L.progress, L.landing && !L.receiving ? 1.0 : f);
  }

  if (Ht(L.dest) > 0) {
    std::wstring dir = dest_.empty() ? app.settings().EffectiveDownloadDir() : dest_;
    bool hov = c.Hover(kIdDest);
    c.Hit(kIdDest, L.dest);
    if (hov) c.Fill(L.dest, th.subtleHover, 4);
    float x = L.dest.left + 6;
    c.Glyph(glyph::kFolder, R(x, L.dest.top, 16, Ht(L.dest)), 14, th.text2);
    x += 22;
    std::wstring lead = L"Save to ";
    float lw = c.TextWidth(lead, 12);
    c.Text(lead, R(x, L.dest.top, lw + 1, Ht(L.dest)), 12, th.text2);
    x += lw;
    std::wstring name = util::FolderDisplayName(dir);
    float nw = std::min(c.TextWidth(name, 12, 600), L.dest.right - x - 30);
    c.Text(name, R(x, L.dest.top, nw + 1, Ht(L.dest)), 12, th.text, 600);
    c.Glyph(glyph::kChevronDown, R(x + nw + 4, L.dest.top, 14, Ht(L.dest)), 10, th.text2);
  }

  if (Ht(L.buttons) > 0) {
    float bw = (Wd(L.buttons) - 8 - 40) / 2;
    Rect b1 = R(L.buttons.left, L.buttons.top, bw, 32);
    Rect b2 = R(b1.right + 4, L.buttons.top, bw, 32);
    Rect b3 = R(L.buttons.right - 36, L.buttons.top, 36, 32);
    bool saving = std::any_of(items.begin(), items.end(), [](Item* i) { return i->state == ItemState::Saving; });
    if (L.pending) {
      c.Button(kIdSave, b1, L.multi ? L"Save all" : L"Save", ButtonStyle::Accent, 0, !saving);
      if (L.multi) c.Button(kIdReveal, b2, L"Show files", ButtonStyle::Standard);
      else c.Button(kIdOpen, b2, L"Open", ButtonStyle::Standard);
      c.Button(kIdMore, b3, L"", ButtonStyle::Standard, glyph::kMore);
    } else if (L.saved) {
      if (L.multi) {
        c.Button(kIdReveal, R(L.buttons.left, L.buttons.top, Wd(L.buttons), 32), L"Show in folder",
                 ButtonStyle::Accent);
      } else {
        float half = (Wd(L.buttons) - 4) / 2;
        c.Button(kIdOpen, R(L.buttons.left, L.buttons.top, half, 32), L"Open", ButtonStyle::Accent);
        c.Button(kIdReveal, R(L.buttons.left + half + 4, L.buttons.top, half, 32), L"Show in folder");
      }
    } else if (L.failed) {
      c.Button(kIdClose, R(L.buttons.left, L.buttons.top, Wd(L.buttons), 32), L"Dismiss");
    }
  }
}

bool ReceiveToast::IsDraggable(int id) const { return id >= kIdPreview && id <= kIdPreview + 99; }

void ReceiveToast::OnDragStart(int id) {
  auto items = Items();
  std::vector<int> ids;
  if (id == kIdPreview + 99 || id == kIdPreview) {
    for (Item* it : items) ids.push_back(it->id);
  } else if (size_t i = id - kIdPreview - 1; i < items.size()) {
    ids.push_back(items[i]->id);
  }
  CancelAutoDismiss();
  App::Get().DragItems(hwnd_, ids);
}

void ReceiveToast::OnClick(int id) {
  App& app = App::Get();
  auto items = Items();
  if (items.empty()) return;
  switch (id) {
    case kIdClose:
      Dismiss();
      break;
    case kIdSave:
      app.SaveBatch(batch_, dest_);
      break;
    case kIdOpen:
      app.OpenItem(items[0]->id);
      if (items[0]->state == ItemState::Saved) Dismiss();
      break;
    case kIdReveal:
      app.RevealItem(items[0]->id);
      break;
    case kIdPreview:
    case kIdPreview + 99:
      if (items.size() == 1) app.OpenItem(items[0]->id);
      break;
    case kIdDest: {
      CancelAutoDismiss();
      std::wstring current = dest_.empty() ? app.settings().EffectiveDownloadDir() : dest_;
      const GUID* known[] = {&FOLDERID_Downloads, &FOLDERID_Desktop, &FOLDERID_Documents, &FOLDERID_Pictures};
      std::vector<std::wstring> dirs;
      std::vector<MenuItem> menu;
      int n = 1;
      std::wstring def = app.settings().EffectiveDownloadDir();
      dirs.push_back(def);
      menu.push_back({n++, util::FolderDisplayName(def) + L" (default)", _wcsicmp(def.c_str(), current.c_str()) == 0});
      for (const GUID* k : known) {
        std::wstring d = util::KnownFolder(*k);
        if (d.empty() || _wcsicmp(d.c_str(), def.c_str()) == 0) continue;
        dirs.push_back(d);
        menu.push_back({n++, util::FolderDisplayName(d), _wcsicmp(d.c_str(), current.c_str()) == 0});
      }
      if (!dest_.empty() && std::none_of(dirs.begin(), dirs.end(), [&](const std::wstring& d) {
            return _wcsicmp(d.c_str(), dest_.c_str()) == 0;
          })) {
        dirs.push_back(dest_);
        menu.push_back({n++, util::FolderDisplayName(dest_), true});
      }
      menu.push_back({0});
      menu.push_back({900, L"Choose a folder…"});
      POINT pt{};
      for (auto& h : hits_)
        if (h.first == kIdDest) {
          pt = {static_cast<LONG>(h.second.left * scale_), static_cast<LONG>(h.second.bottom * scale_)};
          ClientToScreen(hwnd_, &pt);
        }
      int cmd = ShowMenu(app.host(), pt, menu);
      if (cmd == 900) {
        std::wstring picked = util::PickFolder(app.host(), current, L"Save received files to");
        if (!picked.empty()) dest_ = picked;
      } else if (cmd > 0 && cmd <= static_cast<int>(dirs.size())) {
        dest_ = cmd == 1 ? L"" : dirs[cmd - 1];
      }
      Invalidate();
      break;
    }
    case kIdMore: {
      CancelAutoDismiss();
      enum { kSaveTo = 1, kOpenInbox, kDiscard, kOpenFile };
      std::vector<MenuItem> menu = {{kSaveTo, items.size() > 1 ? L"Save all to…" : L"Save to…"}};
      if (items.size() == 1) menu.push_back({kOpenFile, L"Open"});
      menu.push_back({kOpenInbox, L"Show in received files"});
      menu.push_back({0});
      menu.push_back({kDiscard, items.size() > 1 ? L"Delete all" : L"Delete"});
      POINT pt;
      GetCursorPos(&pt);
      switch (ShowMenu(app.host(), pt, menu)) {
        case kSaveTo: {
          std::wstring dir = util::PickFolder(app.host(), app.settings().EffectiveDownloadDir(), L"Save to");
          if (!dir.empty()) app.SaveBatch(batch_, dir);
          break;
        }
        case kOpenFile: app.OpenItem(items[0]->id); break;
        case kOpenInbox: app.RevealItem(items[0]->id); break;
        case kDiscard: {
          std::vector<int> ids;
          for (Item* it : items) ids.push_back(it->id);
          for (int i : ids) app.DiscardItem(i);
          break;
        }
      }
      break;
    }
  }
}

// ---------------------------------------------------------------------------

namespace {
struct SendLayout {
  Rect icon{}, title{}, sub{}, progress{}, buttons{};
  float height = 0;
};

SendLayout LayoutSend(const SendJob* j, float w) {
  SendLayout L;
  const float pad = 16;
  float y = 42, inner = w - 2 * pad;
  L.icon = R(pad, y, 44, 44);
  L.title = R(pad + 56, y + 2, inner - 56, 20);
  bool failed = j && j->state == JobState::Failed;
  L.sub = R(pad + 56, y + 23, inner - 56, failed ? 36.0f : 18.0f);
  y += failed ? 64.0f : 52.0f;
  if (j && (j->state == JobState::Preparing || j->state == JobState::Sending)) {
    L.progress = R(pad, y, inner, 6);
    y += 14;
    L.buttons = R(pad, y + 2, inner, 32);
    y += 34 + pad;
  } else if (failed) {
    L.buttons = R(pad, y + 2, inner, 32);
    y += 34 + pad;
  } else {
    y += 8;
  }
  L.height = y;
  return L;
}
}  // namespace

float SendToast::DesiredHeight() { return LayoutSend(App::Get().FindJob(job_), kWidth).height; }

void SendToast::Changed() { Invalidate(); }

void SendToast::Paint(Canvas& c, float w, float h) {
  SendJob* j = App::Get().FindJob(job_);
  const Theme& th = c.th;
  PaintHeader(c, w, L"TailDrop");
  if (!j) return;
  SendLayout L = LayoutSend(j, w);
  c.Fill(L.icon, th.layer, 6);
  if (j->thumb && !j->thumb->isIcon) c.DrawImage(j->thumb.get(), L.icon, 6, true);
  else if (j->thumb) c.DrawImage(j->thumb.get(), Inset(L.icon, 6, 6), 0, false);
  else c.Glyph(KindGlyph(j->device.kind), L.icon, 22, th.accentText);
  // Device badge in the corner of the thumbnail.
  Rect badge = R(L.icon.right - 18, L.icon.bottom - 18, 22, 22);
  c.Circle((badge.left + badge.right) / 2, (badge.top + badge.bottom) / 2, 11, th.accent);
  c.Glyph(j->state == JobState::Done ? glyph::kCheck : glyph::kSend, badge, 11, th.onAccent);

  std::wstring title, sub;
  Color subColor = th.text2;
  std::wstring what = j->fileCount > 1 ? std::to_wstring(j->fileCount) + L" items" : j->current;
  switch (j->state) {
    case JobState::Preparing:
      title = L"Preparing to send to " + j->device.name;
      sub = j->current;
      break;
    case JobState::Sending: {
      title = L"Sending to " + j->device.name;
      int pct = j->total > 0 ? static_cast<int>(j->sent * 100 / j->total) : 0;
      sub = what + L" · " + std::to_wstring(pct) + L"%";
      if (j->rate > 0) sub += L" · " + util::FormatRate(j->rate);
      break;
    }
    case JobState::Done:
      title = L"Sent to " + j->device.name;
      sub = what + L" · " + util::FormatSize(j->total);
      break;
    case JobState::Failed:
      title = L"Couldn't send to " + j->device.name;
      sub = j->error;
      subColor = th.critical;
      break;
    case JobState::Cancelled:
      title = L"Cancelled";
      break;
  }
  c.Text(title, L.title, 14, th.text, 600);
  c.Text(sub, L.sub, 12, subColor, 400, j->state == JobState::Failed ? kWrap | kVTop : 0);
  if (Ht(L.progress) > 0)
    c.Progress(L.progress, j->total > 0 ? static_cast<double>(j->sent) / j->total : 0);
  if (Ht(L.buttons) > 0) {
    if (j->state == JobState::Failed) {
      float half = (Wd(L.buttons) - 4) / 2;
      c.Button(kIdRetry, R(L.buttons.left, L.buttons.top, half, 32), L"Try again", ButtonStyle::Accent);
      c.Button(kIdDismiss, R(L.buttons.left + half + 4, L.buttons.top, half, 32), L"Dismiss");
    } else {
      c.Button(kIdCancel, L.buttons, L"Cancel");
    }
  }
}

void SendToast::OnClick(int id) {
  App& app = App::Get();
  switch (id) {
    case kIdClose:
    case kIdDismiss:
      Dismiss();
      break;
    case kIdCancel:
      app.CancelJob(job_);
      break;
    case kIdRetry:
      app.RetryJob(job_);
      break;
  }
}
