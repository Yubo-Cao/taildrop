#include "ui.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "util.h"

namespace ui {
namespace {

constexpr wchar_t kWindowClass[] = L"YuboCao.TailDrop.Window";

template <class T>
void SafeRelease(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

Theme g_system, g_app;
bool g_themesLoaded = false;

bool ReadLightTheme(const wchar_t* value) {
  DWORD v = 1, size = sizeof(v);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", value,
               RRF_RT_REG_DWORD, nullptr, &v, &size);
  return v != 0;
}

// AccentPalette holds 8 RGBX entries: Light3, Light2, Light1, Base, Dark1, Dark2, Dark3, unused.
bool ReadAccentPalette(Color out[8]) {
  BYTE data[32];
  DWORD size = sizeof(data);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                   L"AccentPalette", RRF_RT_REG_BINARY, nullptr, data, &size) != ERROR_SUCCESS ||
      size < 32)
    return false;
  for (int i = 0; i < 8; ++i) out[i] = D2D1::ColorF(data[i * 4] / 255.f, data[i * 4 + 1] / 255.f, data[i * 4 + 2] / 255.f);
  return true;
}

Theme BuildTheme(bool dark) {
  Color pal[8];
  if (!ReadAccentPalette(pal)) {
    const uint32_t def[8] = {0x99EBFF, 0x4CC2FF, 0x0091F8, 0x0078D4, 0x0067C0, 0x003E92, 0x001A68, 0xF7630C};
    for (int i = 0; i < 8; ++i) pal[i] = Hex(def[i]);
  }
  Theme t;
  t.dark = dark;
  if (!dark) {
    t.text = Hex(0x000000, 0.894f);
    t.text2 = Hex(0x000000, 0.62f);
    t.text3 = Hex(0x000000, 0.447f);
    t.textDisabled = Hex(0x000000, 0.36f);
    t.accent = pal[4];
    t.accentText = pal[5];
    t.onAccent = Hex(0xFFFFFF);
    t.ctrl = Hex(0xFFFFFF, 0.70f);
    t.ctrlHover = Hex(0xF9F9F9, 0.50f);
    t.ctrlPressed = Hex(0xF9F9F9, 0.30f);
    t.ctrlDisabled = Hex(0xF9F9F9, 0.30f);
    t.ctrlStroke = Hex(0x000000, 0.0578f);
    t.ctrlStrokeBottom = Hex(0x000000, 0.1622f);
    t.strokeStrong = Hex(0x000000, 0.4458f);
    t.subtleHover = Hex(0x000000, 0.0373f);
    t.subtlePressed = Hex(0x000000, 0.0241f);
    t.card = Hex(0xFFFFFF, 0.70f);
    t.cardHover = Hex(0xF9F9F9, 0.50f);
    t.cardStroke = Hex(0x000000, 0.0578f);
    t.divider = Hex(0x000000, 0.0803f);
    t.solidBase = Hex(0xF3F3F3);
    t.solidFlyout = Hex(0xF9F9F9);
    t.success = Hex(0x0F7B0F);
    t.caution = Hex(0x9D5D00);
    t.critical = Hex(0xC42B1C);
    t.layer = Hex(0xFFFFFF, 0.5f);
  } else {
    t.text = Hex(0xFFFFFF);
    t.text2 = Hex(0xFFFFFF, 0.786f);
    t.text3 = Hex(0xFFFFFF, 0.544f);
    t.textDisabled = Hex(0xFFFFFF, 0.363f);
    t.accent = pal[1];
    t.accentText = pal[0];
    t.onAccent = Hex(0x000000);
    t.ctrl = Hex(0xFFFFFF, 0.0605f);
    t.ctrlHover = Hex(0xFFFFFF, 0.0837f);
    t.ctrlPressed = Hex(0xFFFFFF, 0.0326f);
    t.ctrlDisabled = Hex(0xFFFFFF, 0.0419f);
    t.ctrlStroke = Hex(0xFFFFFF, 0.0698f);
    t.ctrlStrokeBottom = Hex(0xFFFFFF, 0.0930f);
    t.strokeStrong = Hex(0xFFFFFF, 0.5442f);
    t.subtleHover = Hex(0xFFFFFF, 0.0605f);
    t.subtlePressed = Hex(0xFFFFFF, 0.0419f);
    t.card = Hex(0xFFFFFF, 0.0512f);
    t.cardHover = Hex(0xFFFFFF, 0.0837f);
    t.cardStroke = Hex(0x000000, 0.10f);
    t.divider = Hex(0xFFFFFF, 0.0837f);
    t.solidBase = Hex(0x202020);
    t.solidFlyout = Hex(0x2C2C2C);
    t.success = Hex(0x6CCB5F);
    t.caution = Hex(0xFCE100);
    t.critical = Hex(0xFF99A4);
    t.layer = Hex(0x3A3A3A, 0.3f);
  }
  t.accentHover = WithAlpha(t.accent, 0.9f);
  t.accentPressed = WithAlpha(t.accent, 0.8f);
  return t;
}

void EnsureThemes() {
  if (!g_themesLoaded) ReloadThemes();
}

// ---------------------------------------------------------------------------
// OLE drop target forwarding to a Window.

std::vector<std::wstring> FilesFromDataObject(IDataObject* obj) {
  std::vector<std::wstring> out;
  FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM stg{};
  if (FAILED(obj->GetData(&fmt, &stg))) return out;
  if (auto drop = static_cast<HDROP>(GlobalLock(stg.hGlobal))) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
      UINT len = DragQueryFileW(drop, i, nullptr, 0);
      std::wstring s(len, L'\0');
      DragQueryFileW(drop, i, s.data(), len + 1);
      out.push_back(std::move(s));
    }
    GlobalUnlock(stg.hGlobal);
  }
  ReleaseStgMedium(&stg);
  return out;
}

void SetDropDescription(IDataObject* obj, DROPIMAGETYPE type, const std::wstring& msg, const std::wstring& insert) {
  static CLIPFORMAT cf = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_DROPDESCRIPTION));
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPDESCRIPTION));
  if (!h) return;
  auto* d = static_cast<DROPDESCRIPTION*>(GlobalLock(h));
  d->type = type;
  wcsncpy_s(d->szMessage, msg.c_str(), _TRUNCATE);
  wcsncpy_s(d->szInsert, insert.c_str(), _TRUNCATE);
  GlobalUnlock(h);
  FORMATETC fmt{cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM stg{TYMED_HGLOBAL};
  stg.hGlobal = h;
  if (FAILED(obj->SetData(&fmt, &stg, TRUE))) GlobalFree(h);
}

}  // namespace

class DropTarget : public IDropTarget {
 public:
  explicit DropTarget(Window* w) : w_(w) {
    CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper_));
  }
  ~DropTarget() {
    SafeRelease(helper_);
    SafeRelease(obj_);
  }
  void Detach() { w_ = nullptr; }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *ppv = static_cast<IDropTarget*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG r = InterlockedDecrement(&ref_);
    if (!r) delete this;
    return r;
  }

  STDMETHODIMP DragEnter(IDataObject* obj, DWORD, POINTL pt, DWORD* effect) override {
    SafeRelease(obj_);
    obj_ = obj;
    obj_->AddRef();
    files_ = FilesFromDataObject(obj);
    if (w_) {
      w_->dragActive_ = true;
      w_->OnDragEnter();
    }
    *effect = Update(pt);
    POINT p{pt.x, pt.y};
    if (helper_ && w_) helper_->DragEnter(w_->hwnd(), obj, &p, *effect);
    return S_OK;
  }
  STDMETHODIMP DragOver(DWORD, POINTL pt, DWORD* effect) override {
    *effect = Update(pt);
    POINT p{pt.x, pt.y};
    if (helper_) helper_->DragOver(&p, *effect);
    return S_OK;
  }
  STDMETHODIMP DragLeave() override {
    if (helper_) helper_->DragLeave();
    if (obj_) SetDropDescription(obj_, DROPIMAGE_INVALID, L"", L"");
    Reset();
    if (w_) w_->OnDragLeave();
    return S_OK;
  }
  STDMETHODIMP Drop(IDataObject* obj, DWORD, POINTL pt, DWORD* effect) override {
    *effect = Update(pt);
    POINT p{pt.x, pt.y};
    if (helper_) helper_->Drop(obj, &p, *effect);
    int id = w_ ? w_->dropHover_ : 0;
    std::vector<std::wstring> files = std::move(files_);
    Window* w = w_;
    Reset();
    if (w && *effect != DROPEFFECT_NONE) w->OnDrop(id, std::move(files));
    else if (w) w->OnDragLeave();
    return S_OK;
  }

 private:
  DWORD Update(POINTL pt) {
    if (!w_ || files_.empty()) return DROPEFFECT_NONE;
    POINT p{pt.x, pt.y};
    ScreenToClient(w_->hwnd(), &p);
    int id = w_->HitTest(p.x / w_->scale(), p.y / w_->scale());
    DWORD eff = w_->OnDragOver(id, files_);
    if (id != w_->dropHover_) {
      w_->dropHover_ = id;
      w_->Invalidate();
    }
    DROPIMAGETYPE type = eff == DROPEFFECT_NONE ? DROPIMAGE_NONE : DROPIMAGE_COPY;
    std::wstring msg, insert;
    w_->DropLabel(id, type, msg, insert);
    if (obj_ && (type != lastType_ || msg != lastMsg_ || insert != lastInsert_)) {
      SetDropDescription(obj_, msg.empty() ? DROPIMAGE_INVALID : type, msg, insert);
      lastType_ = type;
      lastMsg_ = msg;
      lastInsert_ = insert;
    }
    return eff;
  }
  void Reset() {
    SafeRelease(obj_);
    files_.clear();
    lastMsg_.clear();
    lastInsert_.clear();
    lastType_ = DROPIMAGE_INVALID;
    if (w_) {
      w_->dragActive_ = false;
      w_->dropHover_ = 0;
      w_->Invalidate();
    }
  }

  LONG ref_ = 1;
  Window* w_;
  IDropTargetHelper* helper_ = nullptr;
  IDataObject* obj_ = nullptr;
  std::vector<std::wstring> files_;
  DROPIMAGETYPE lastType_ = DROPIMAGE_INVALID;
  std::wstring lastMsg_, lastInsert_;
};

Color Hex(uint32_t rgb, float alpha) {
  return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha);
}

Color WithAlpha(Color c, float a) {
  c.a *= a;
  return c;
}

const Theme& SystemTheme() {
  EnsureThemes();
  return g_system;
}

const Theme& AppTheme() {
  EnsureThemes();
  return g_app;
}

void ReloadThemes() {
  g_system = BuildTheme(!ReadLightTheme(L"SystemUsesLightTheme"));
  g_app = BuildTheme(!ReadLightTheme(L"AppsUseLightTheme"));
  g_themesLoaded = true;
}

bool HasSystemBackdrop() {
  static int cached = -1;
  if (cached < 0) {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW v{sizeof(v)};
    cached = fn && fn(&v) == 0 && v.dwBuildNumber >= 22621;
  }
  return cached == 1;
}

// ---------------------------------------------------------------------------
// Gfx

Gfx& Gfx::Get() {
  static Gfx g;
  return g;
}

bool Gfx::Init() {
  if (initialized_) return rt_ != nullptr;
  initialized_ = true;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_))) return false;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(&dwrite_))))
    return false;
  // Software rendering keeps GPU drivers out of the process: these windows are
  // small and short-lived, and the always-on footprint matters more.
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  if (FAILED(factory_->CreateDCRenderTarget(&props, &rt_))) return false;
  rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_);

  IDWriteFontCollection* fonts = nullptr;
  dwrite_->GetSystemFontCollection(&fonts);
  auto pick = [&](std::initializer_list<const wchar_t*> names) -> std::wstring {
    for (const wchar_t* n : names) {
      UINT32 idx;
      BOOL exists = FALSE;
      if (fonts && SUCCEEDED(fonts->FindFamilyName(n, &idx, &exists)) && exists) return n;
    }
    return L"Segoe UI";
  };
  textFamily_ = pick({L"Segoe UI Variable Text", L"Segoe UI"});
  displayFamily_ = pick({L"Segoe UI Variable Display", L"Segoe UI"});
  iconFamily_ = pick({L"Segoe Fluent Icons", L"Segoe MDL2 Assets"});
  SafeRelease(fonts);
  return true;
}

IDWriteTextFormat* Gfx::Format(float size, int weight, Font font, IDWriteInlineObject** trimming) {
  if (!Init()) return nullptr;
  for (Fmt& f : formats_) {
    if (f.size == size && f.weight == weight && f.font == font) {
      if (trimming) *trimming = f.trim;
      return f.fmt;
    }
  }
  const std::wstring& family = font == Font::Icon ? iconFamily_ : font == Font::Display ? displayFamily_ : textFamily_;
  Fmt f{size, weight, font, nullptr, nullptr};
  dwrite_->CreateTextFormat(family.c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(weight),
                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f.fmt);
  if (!f.fmt) return nullptr;
  dwrite_->CreateEllipsisTrimmingSign(f.fmt, &f.trim);
  formats_.push_back(f);
  if (trimming) *trimming = f.trim;
  return f.fmt;
}

bool Gfx::Begin(int w, int h, float scale) {
  if (!Init()) return false;
  if (!memdc_ || w > dibW_ || h > dibH_) {
    if (memdc_) {
      SelectObject(memdc_, oldbmp_);
      DeleteObject(dib_);
      DeleteDC(memdc_);
    }
    dibW_ = std::max(w, dibW_);
    dibH_ = std::max(h, dibH_);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = dibW_;
    bi.bmiHeader.biHeight = -dibH_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    memdc_ = CreateCompatibleDC(nullptr);
    dib_ = CreateDIBSection(memdc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib_) return false;
    oldbmp_ = SelectObject(memdc_, dib_);
  }
  RECT rc{0, 0, w, h};
  if (FAILED(rt_->BindDC(memdc_, &rc))) return false;
  rt_->SetDpi(96.f * scale, 96.f * scale);
  rt_->BeginDraw();
  rt_->SetTransform(D2D1::Matrix3x2F::Identity());
  rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  return true;
}

void Gfx::End(HDC target, int w, int h) {
  HRESULT hr = rt_->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    SafeRelease(brush_);
    SafeRelease(rt_);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    factory_->CreateDCRenderTarget(&props, &rt_);
    if (rt_) rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_);
    ++generation_;
    return;
  }
  GdiFlush();
  BitBlt(target, 0, 0, w, h, memdc_, 0, 0, SRCCOPY);
}

void Gfx::ReleaseIdle() {
  if (memdc_) {
    SelectObject(memdc_, oldbmp_);
    DeleteObject(dib_);
    DeleteDC(memdc_);
    memdc_ = nullptr;
    dib_ = nullptr;
    dibW_ = dibH_ = 0;
  }
  for (Fmt& f : formats_) {
    SafeRelease(f.trim);
    SafeRelease(f.fmt);
  }
  formats_.clear();
  SafeRelease(brush_);
  SafeRelease(rt_);
  SafeRelease(dwrite_);
  SafeRelease(factory_);
  initialized_ = false;
  ++generation_;  // bitmaps created from the old target are now stale
}

// ---------------------------------------------------------------------------
// Image

Image::~Image() { SafeRelease(bmp); }

ID2D1Bitmap* Image::Bitmap() {
  Gfx& g = Gfx::Get();
  if (bmp && gen == g.generation()) return bmp;
  SafeRelease(bmp);
  if (px.empty() || !g.rt()) return nullptr;
  D2D1_BITMAP_PROPERTIES p =
      D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  g.rt()->CreateBitmap(D2D1::SizeU(w, h), px.data(), w * 4, &p, &bmp);
  gen = g.generation();
  return bmp;
}

HBITMAP Image::CreateDragBitmap(int maxSize, SIZE* outSize) const {
  if (px.empty()) return nullptr;
  float s = std::min(1.0f, static_cast<float>(maxSize) / std::max(w, h));
  int dw = std::max(1, static_cast<int>(w * s)), dh = std::max(1, static_cast<int>(h * s));
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = dw;
  bi.bmiHeader.biHeight = -dh;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!hbm) return nullptr;
  auto* dst = static_cast<uint32_t*>(bits);
  // Box filter: average all source pixels that map onto each target pixel.
  for (int y = 0; y < dh; ++y) {
    int y0 = y * h / dh, y1 = std::max(y0 + 1, (y + 1) * h / dh);
    for (int x = 0; x < dw; ++x) {
      int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw);
      uint32_t acc[4] = {0, 0, 0, 0}, n = 0;
      for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
          uint32_t p = px[yy * w + xx];
          acc[0] += p & 0xFF;
          acc[1] += (p >> 8) & 0xFF;
          acc[2] += (p >> 16) & 0xFF;
          acc[3] += p >> 24;
          ++n;
        }
      dst[y * dw + x] = (acc[0] / n) | ((acc[1] / n) << 8) | ((acc[2] / n) << 16) | ((acc[3] / n) << 24);
    }
  }
  if (outSize) *outSize = {dw, dh};
  return hbm;
}

std::shared_ptr<Image> LoadThumbnail(const std::wstring& path, int sizePx) {
  IShellItemImageFactory* f = nullptr;
  if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&f)))) return nullptr;
  HBITMAP hbm = nullptr;
  bool isIcon = false;
  SIZE sz{sizePx, sizePx};
  if (FAILED(f->GetImage(sz, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hbm))) {
    isIcon = true;
    f->GetImage(sz, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbm);
  }
  f->Release();
  if (!hbm) return nullptr;
  BITMAP bm{};
  GetObjectW(hbm, sizeof(bm), &bm);
  auto img = std::make_shared<Image>();
  img->w = bm.bmWidth;
  img->h = std::abs(bm.bmHeight);
  img->isIcon = isIcon;
  img->px.resize(static_cast<size_t>(img->w) * img->h);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = img->w;
  bi.bmiHeader.biHeight = -img->h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC dc = GetDC(nullptr);
  int got = GetDIBits(dc, hbm, 0, img->h, img->px.data(), &bi, DIB_RGB_COLORS);
  ReleaseDC(nullptr, dc);
  DeleteObject(hbm);
  if (!got) return nullptr;
  bool anyAlpha = false, straight = false;
  for (uint32_t p : img->px) {
    uint32_t a = p >> 24;
    if (a) anyAlpha = true;
    if ((p & 0xFF) > a || ((p >> 8) & 0xFF) > a || ((p >> 16) & 0xFF) > a) straight = true;
  }
  for (uint32_t& p : img->px) {
    if (!anyAlpha) {
      p |= 0xFF000000u;
    } else if (straight) {
      uint32_t a = p >> 24;
      p = (a << 24) | ((((p >> 16) & 0xFF) * a / 255) << 16) | ((((p >> 8) & 0xFF) * a / 255) << 8) |
          ((p & 0xFF) * a / 255);
    }
  }
  return img;
}

// ---------------------------------------------------------------------------
// Canvas

Canvas::Canvas(Window* w, const Theme& t)
    : th(t), rt(Gfx::Get().rt()), w_(w), brush_(Gfx::Get().brush()), px_(1.0f / w->scale()) {}

void Canvas::Fill(const Rect& r, Color c, float radius) {
  if (c.a <= 0) return;
  brush_->SetColor(c);
  if (radius > 0) rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_);
  else rt->FillRectangle(r, brush_);
}

void Canvas::Stroke(const Rect& r, Color c, float radius, float widthPx) {
  if (c.a <= 0) return;
  float sw = widthPx * px_;
  Rect rr = Inset(r, sw / 2, sw / 2);
  brush_->SetColor(c);
  if (radius > 0) {
    float rad = std::max(0.0f, radius - sw / 2);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rr, rad, rad), brush_, sw);
  } else {
    rt->DrawRectangle(rr, brush_, sw);
  }
}

void Canvas::Line(float x1, float y1, float x2, float y2, Color c, float widthPx) {
  brush_->SetColor(c);
  rt->DrawLine({x1, y1}, {x2, y2}, brush_, widthPx * px_);
}

void Canvas::Circle(float cx, float cy, float radius, Color c) {
  brush_->SetColor(c);
  rt->FillEllipse(D2D1::Ellipse({cx, cy}, radius, radius), brush_);
}

void Canvas::Text(std::wstring_view s, const Rect& r, float size, Color c, int weight, unsigned flags) {
  if (s.empty() || Wd(r) <= 0) return;
  Gfx& g = Gfx::Get();
  IDWriteInlineObject* trim = nullptr;
  IDWriteTextFormat* fmt = g.Format(size, weight, size >= 20 ? Font::Display : Font::Text, &trim);
  if (!fmt) return;
  IDWriteTextLayout* layout = nullptr;
  if (FAILED(g.dwrite()->CreateTextLayout(s.data(), static_cast<UINT32>(s.size()), fmt, Wd(r),
                                          std::max(1.0f, Ht(r)), &layout)))
    return;
  layout->SetTextAlignment((flags & kAlignCenter)  ? DWRITE_TEXT_ALIGNMENT_CENTER
                           : (flags & kAlignRight) ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                   : DWRITE_TEXT_ALIGNMENT_LEADING);
  layout->SetParagraphAlignment((flags & kVTop)      ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                : (flags & kVBottom) ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                                     : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  layout->SetWordWrapping((flags & kWrap) ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  DWRITE_TRIMMING tr{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
  layout->SetTrimming(&tr, trim);
  brush_->SetColor(c);
  rt->DrawTextLayout({r.left, r.top}, layout, brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
  layout->Release();
}

void Canvas::Glyph(wchar_t gl, const Rect& r, float size, Color c) {
  Gfx& g = Gfx::Get();
  IDWriteTextFormat* fmt = g.Format(size, 400, Font::Icon);
  if (!fmt) return;
  IDWriteTextLayout* layout = nullptr;
  if (FAILED(g.dwrite()->CreateTextLayout(&gl, 1, fmt, Wd(r), Ht(r), &layout))) return;
  layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  brush_->SetColor(c);
  rt->DrawTextLayout({r.left, r.top}, layout, brush_);
  layout->Release();
}

float Canvas::TextWidth(std::wstring_view s, float size, int weight) {
  Gfx& g = Gfx::Get();
  IDWriteTextFormat* fmt = g.Format(size, weight, size >= 20 ? Font::Display : Font::Text);
  IDWriteTextLayout* layout = nullptr;
  if (!fmt || FAILED(g.dwrite()->CreateTextLayout(s.data(), static_cast<UINT32>(s.size()), fmt, 10000, 100, &layout)))
    return 0;
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  layout->Release();
  return std::ceil(m.widthIncludingTrailingWhitespace);
}

float Canvas::TextHeight(std::wstring_view s, float width, float size, int weight) {
  Gfx& g = Gfx::Get();
  IDWriteTextFormat* fmt = g.Format(size, weight, size >= 20 ? Font::Display : Font::Text);
  IDWriteTextLayout* layout = nullptr;
  if (!fmt ||
      FAILED(g.dwrite()->CreateTextLayout(s.data(), static_cast<UINT32>(s.size()), fmt, width, 10000, &layout)))
    return 0;
  layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  layout->Release();
  return std::ceil(m.height);
}

void Canvas::DrawImage(Image* img, const Rect& r, float radius, bool cover) {
  if (!img) return;
  ID2D1Bitmap* bmp = img->Bitmap();
  if (!bmp) return;
  // Bitmaps are created at 96 DPI, so their DIP size is their pixel size.
  float bw = static_cast<float>(img->w), bh = static_cast<float>(img->h);
  float sx = Wd(r) / bw, sy = Ht(r) / bh;
  float s = cover ? std::max(sx, sy) : std::min(sx, sy);
  if (!cover && img->isIcon) s = std::min(s, px_ * 1.0f > 0 ? 1.0f / w_->scale() * std::max(1.0f, w_->scale()) : 1.0f);
  float dw = bw * s, dh = bh * s;
  float x = r.left + (Wd(r) - dw) / 2, y = r.top + (Ht(r) - dh) / 2;
  if (cover && radius > 0) {
    ID2D1BitmapBrush* bb = nullptr;
    D2D1_BRUSH_PROPERTIES bp =
        D2D1::BrushProperties(1.0f, D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(x, y));
    if (SUCCEEDED(rt->CreateBitmapBrush(bmp, D2D1::BitmapBrushProperties(), bp, &bb))) {
      bb->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
      rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), bb);
      bb->Release();
    }
    return;
  }
  if (cover) rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
  rt->DrawBitmap(bmp, {x, y, x + dw, y + dh}, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  if (cover) rt->PopAxisAlignedClip();
}

void Canvas::Hit(int id, const Rect& r) { w_->hits_.push_back({id, r}); }
bool Canvas::Hover(int id) const { return w_->hover_ == id && (w_->pressed_ == 0 || w_->pressed_ == id); }
bool Canvas::Pressed(int id) const { return w_->pressed_ == id && w_->hover_ == id; }
bool Canvas::DropHover(int id) const { return w_->dragActive_ && w_->dropHover_ == id; }

void Canvas::Button(int id, const Rect& r, std::wstring_view label, ButtonStyle style, wchar_t gl, bool enabled) {
  if (enabled) Hit(id, r);
  bool hov = enabled && Hover(id), prs = enabled && Pressed(id);
  Color fill{}, fg{};
  switch (style) {
    case ButtonStyle::Accent:
      fill = !enabled ? th.ctrlDisabled : prs ? th.accentPressed : hov ? th.accentHover : th.accent;
      fg = !enabled ? th.textDisabled : prs ? WithAlpha(th.onAccent, 0.7f) : th.onAccent;
      break;
    case ButtonStyle::Standard:
      fill = !enabled ? th.ctrlDisabled : prs ? th.ctrlPressed : hov ? th.ctrlHover : th.ctrl;
      fg = !enabled ? th.textDisabled : prs ? th.text2 : th.text;
      break;
    case ButtonStyle::Subtle:
      fill = prs ? th.subtlePressed : hov ? th.subtleHover : D2D1::ColorF(0, 0, 0, 0);
      fg = !enabled ? th.textDisabled : prs ? th.text2 : th.text;
      break;
  }
  Fill(r, fill, 4);
  if (style == ButtonStyle::Standard) {
    Stroke(r, th.ctrlStroke, 4);
    if (!prs && enabled) Line(r.left + 4, r.bottom - px_ / 2, r.right - 4, r.bottom - px_ / 2, th.ctrlStrokeBottom);
  } else if (style == ButtonStyle::Accent && enabled) {
    Line(r.left + 4, r.bottom - px_ / 2, r.right - 4, r.bottom - px_ / 2, D2D1::ColorF(0, 0, 0, 0.12f));
  }
  float gs = 16, gap = gl && !label.empty() ? 8.0f : 0.0f;
  float tw = label.empty() ? 0 : TextWidth(label, 14);
  float avail = Wd(r) - 16;
  float total = (gl ? gs : 0) + gap + tw;
  if (total > avail) tw = std::max(0.0f, avail - (gl ? gs : 0) - gap), total = avail;
  float x = r.left + (Wd(r) - total) / 2;
  if (gl) {
    Glyph(gl, R(x, r.top, gs, Ht(r)), 16, fg);
    x += gs + gap;
  }
  if (!label.empty()) Text(label, {x, r.top, x + tw + 1, r.bottom - 1}, 14, fg);
}

void Canvas::IconButton(int id, const Rect& r, wchar_t gl, float size, const Color* color) {
  Hit(id, r);
  bool hov = Hover(id), prs = Pressed(id);
  Fill(r, prs ? th.subtlePressed : hov ? th.subtleHover : D2D1::ColorF(0, 0, 0, 0), 4);
  Glyph(gl, r, size, color ? *color : (prs ? th.text2 : th.text));
}

void Canvas::Toggle(int id, const Rect& r, bool on) {
  Hit(id, r);
  bool hov = Hover(id), prs = Pressed(id);
  float rad = Ht(r) / 2, cy = (r.top + r.bottom) / 2;
  float kr = prs ? 7.0f : hov ? 7.0f : 6.0f;
  if (on) {
    Fill(r, prs ? th.accentPressed : hov ? th.accentHover : th.accent, rad);
    Circle(r.right - rad, cy, kr, th.onAccent);
  } else {
    Fill(r, prs ? th.subtlePressed : hov ? th.subtleHover : th.ctrl, rad);
    Stroke(r, th.strokeStrong, rad);
    Circle(r.left + rad, cy, kr, th.text2);
  }
}

void Canvas::Progress(const Rect& r, double fraction) {
  float cy = (r.top + r.bottom) / 2;
  Fill({r.left, cy - px_ / 2, r.right, cy + px_ / 2}, WithAlpha(th.strokeStrong, 0.8f));
  float f = static_cast<float>(std::clamp(fraction, 0.0, 1.0));
  if (f > 0) Fill({r.left, cy - 1.5f, r.left + std::max(3.0f, Wd(r) * f), cy + 1.5f}, th.accent, 1.5f);
}

void Canvas::PushClip(const Rect& r) { rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED); }
void Canvas::PopClip() { rt->PopAxisAlignedClip(); }

// ---------------------------------------------------------------------------
// Window

Window::~Window() {
  if (drop_) {
    drop_->Detach();
    drop_->Release();
  }
  if (hwnd_) {
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    DestroyWindow(hwnd_);
  }
}

bool Window::CreateWin(WindowKind kind, const wchar_t* title, const RECT& px, bool transientTheme) {
  static bool registered = false;
  HINSTANCE inst = GetModuleHandleW(nullptr);
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);
    registered = true;
  }
  kind_ = kind;
  transientTheme_ = transientTheme;
  DWORD style = 0, ex = 0;
  RECT r = px;
  switch (kind) {
    case WindowKind::Popup:
      style = WS_POPUP;
      ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
      break;
    case WindowKind::ToastPopup:
      style = WS_POPUP;
      ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
      break;
    case WindowKind::Normal: {
      style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
      POINT c{(px.left + px.right) / 2, (px.top + px.bottom) / 2};
      UINT dpi = static_cast<UINT>(DpiScaleForPoint(c) * 96);
      AdjustWindowRectExForDpi(&r, style, FALSE, ex, dpi);
      break;
    }
  }
  hwnd_ = CreateWindowExW(ex, kWindowClass, title, style, r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr,
                          nullptr, inst, this);
  if (!hwnd_) return false;
  scale_ = GetDpiForWindow(hwnd_) / 96.0f;
  ApplyTheme();
  return true;
}

void Window::ApplyTheme() {
  if (!hwnd_) return;
  const Theme& t = theme();
  BOOL dark = t.dark;
  DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
  if (kind_ != WindowKind::Normal) {
    int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
  }
  backdrop_ = HasSystemBackdrop();
  if (backdrop_) {
    int type = kind_ == WindowKind::Normal ? 2 /* DWMSBT_MAINWINDOW */ : 3 /* DWMSBT_TRANSIENTWINDOW */;
    DwmSetWindowAttribute(hwnd_, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &type, sizeof(type));
    MARGINS m{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &m);
  }
  AllowDarkFor(hwnd_);
  Invalidate();
}

void Window::Invalidate() const {
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Window::Close() {
  if (hwnd_) DestroyWindow(hwnd_);
}

void Window::EnableDrop() {
  if (drop_ || !hwnd_) return;
  drop_ = new DropTarget(this);
  RegisterDragDrop(hwnd_, drop_);
}

int Window::HitTest(float x, float y) const {
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
    if (Contains(it->second, x, y)) return it->first;
  return 0;
}

void Window::DoPaint() {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd_, &ps);
  RECT rc;
  GetClientRect(hwnd_, &rc);
  int w = rc.right, h = rc.bottom;
  Gfx& g = Gfx::Get();
  if (w > 0 && h > 0 && g.Begin(w, h, scale_)) {
    const Theme& t = theme();
    g.rt()->Clear(backdrop_ ? D2D1::ColorF(0, 0, 0, 0) : (kind_ == WindowKind::Normal ? t.solidBase : t.solidFlyout));
    hits_.clear();
    Canvas c(this, t);
    Paint(c, w / scale_, h / scale_);
    g.End(dc, w, h);
  }
  EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK Window::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    auto* self = static_cast<Window*>(cs->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
  return self->WndProc(msg, wp, lp);
}

LRESULT Window::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
  bool handled = false;
  LRESULT res = HandleMessage(msg, wp, lp, handled);
  if (handled) return res;
  auto dips = [&](LPARAM l, float& x, float& y) {
    x = GET_X_LPARAM(l) / scale_;
    y = GET_Y_LPARAM(l) / scale_;
  };
  switch (msg) {
    case WM_PAINT:
      DoPaint();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_MOUSEMOVE: {
      float x, y;
      dips(lp, x, y);
      int id = HitTest(x, y);
      if (id != hover_) {
        hover_ = id;
        OnHoverChanged();
        Invalidate();
      }
      if (!tracking_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        tracking_ = true;
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      tracking_ = false;
      if (hover_) {
        hover_ = 0;
        OnHoverChanged();
        Invalidate();
      }
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      float x, y;
      dips(lp, x, y);
      int id = HitTest(x, y);
      if (!id) return 0;
      if (IsDraggable(id)) {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd_, &pt);
        pressed_ = id;
        Invalidate();
        bool drag = DragDetect(hwnd_, pt);
        pressed_ = 0;
        Invalidate();
        if (drag) OnDragStart(id);
        else OnClick(id);
        return 0;
      }
      pressed_ = id;
      SetCapture(hwnd_);
      Invalidate();
      return 0;
    }
    case WM_LBUTTONUP: {
      int p = pressed_;
      if (!p) return 0;
      pressed_ = 0;  // before ReleaseCapture: WM_CAPTURECHANGED would clear it anyway
      ReleaseCapture();
      float x, y;
      dips(lp, x, y);
      int id = HitTest(x, y);
      Invalidate();
      if (id == p) OnClick(id);
      return 0;
    }
    case WM_RBUTTONUP: {
      float x, y;
      dips(lp, x, y);
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ClientToScreen(hwnd_, &pt);
      OnRightClick(HitTest(x, y), pt);
      return 0;
    }
    case WM_CAPTURECHANGED:
      if (pressed_ && reinterpret_cast<HWND>(lp) != hwnd_) {
        pressed_ = 0;
        Invalidate();
      }
      return 0;
    case WM_MOUSEWHEEL:
      OnWheel(GET_WHEEL_DELTA_WPARAM(wp) / static_cast<float>(WHEEL_DELTA));
      return 0;
    case WM_MOUSEACTIVATE:
      if (kind_ == WindowKind::ToastPopup) return MA_NOACTIVATE;
      break;
    case WM_ACTIVATE:
      OnActivate(LOWORD(wp) != WA_INACTIVE);
      break;
    case WM_KEYDOWN:
      if (wp == VK_ESCAPE) {
        OnEscape();
        return 0;
      }
      break;
    case WM_TIMER:
      OnTimer(wp);
      return 0;
    case WM_DPICHANGED: {
      scale_ = HIWORD(wp) / 96.0f;
      auto* r = reinterpret_cast<RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      OnDpiChanged();
      Invalidate();
      return 0;
    }
    case WM_DESTROY:
      if (drop_) RevokeDragDrop(hwnd_);
      break;
    case WM_NCDESTROY: {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
      hwnd_ = nullptr;
      if (onDestroyed) onDestroyed(this);
      return 0;
    }
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Drag source

DWORD StartFileDrag(HWND hwnd, const std::vector<std::wstring>& paths, const Image* preview) {
  std::vector<PIDLIST_ABSOLUTE> pidls;
  for (const auto& p : paths) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr))) pidls.push_back(pidl);
  }
  DWORD effect = DROPEFFECT_NONE;
  IShellItemArray* arr = nullptr;
  IDataObject* obj = nullptr;
  std::vector<PCIDLIST_ABSOLUTE> cpidls(pidls.begin(), pidls.end());
  if (!pidls.empty() &&
      SUCCEEDED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(cpidls.size()), cpidls.data(), &arr)) &&
      SUCCEEDED(arr->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&obj)))) {
    // Prefer move: the file leaves TailDrop's inbox like a real file would.
    static CLIPFORMAT cfPreferred = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD))) {
      *static_cast<DWORD*>(GlobalLock(h)) = DROPEFFECT_MOVE;
      GlobalUnlock(h);
      FORMATETC fmt{cfPreferred, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
      STGMEDIUM stg{TYMED_HGLOBAL};
      stg.hGlobal = h;
      if (FAILED(obj->SetData(&fmt, &stg, TRUE))) GlobalFree(h);
    }
    if (preview) {
      IDragSourceHelper* helper = nullptr;
      if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper)))) {
        SIZE sz{};
        if (HBITMAP hbm = preview->CreateDragBitmap(128, &sz)) {
          SHDRAGIMAGE img{};
          img.sizeDragImage = sz;
          img.ptOffset = {sz.cx / 2, sz.cy / 2};
          img.hbmpDragImage = hbm;
          img.crColorKey = CLR_NONE;
          if (FAILED(helper->InitializeFromBitmap(&img, obj))) DeleteObject(hbm);
        }
        helper->Release();
      }
    }
    SHDoDragDrop(hwnd, obj, nullptr, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
  }
  SafeRelease(obj);
  SafeRelease(arr);
  for (auto p : pidls) CoTaskMemFree(p);
  return effect;
}

int ShowMenu(HWND owner, POINT pt, const std::vector<MenuItem>& items) {
  HMENU m = CreatePopupMenu();
  for (const auto& it : items) {
    if (it.id == 0) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    else
      AppendMenuW(m, MF_STRING | (it.checked ? MF_CHECKED : 0) | (it.disabled ? MF_GRAYED : 0), it.id,
                  it.text.c_str());
  }
  SetForegroundWindow(owner);
  int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY |
                                    (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN),
                             pt.x, pt.y, owner, nullptr);
  PostMessageW(owner, WM_NULL, 0, 0);
  DestroyMenu(m);
  return cmd;
}

void InitDarkMenus() {
  HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!ux) return;
  using SetPreferredAppModeFn = int(WINAPI*)(int);
  using FlushMenuThemesFn = void(WINAPI*)();
  if (auto fn = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)))) fn(1);
  if (auto fn = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)))) fn();
}

void AllowDarkFor(HWND hwnd) {
  static auto fn = [] {
    using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    return ux ? reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133))) : nullptr;
  }();
  if (fn) fn(hwnd, true);
}

float DpiScaleForPoint(POINT pt) {
  HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
  UINT dx = 96, dy = 96;
  GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
  return dx / 96.0f;
}

RECT WorkAreaForPoint(POINT pt) {
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
  return mi.rcWork;
}

}  // namespace ui
