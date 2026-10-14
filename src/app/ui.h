// Tiny immediate-mode UI on Direct2D (software) + DWM system backdrops,
// styled after Windows 11 Fluent controls.
#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <oleidl.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

using Rect = D2D1_RECT_F;
using Color = D2D1_COLOR_F;

inline Rect R(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
inline float Wd(const Rect& r) { return r.right - r.left; }
inline float Ht(const Rect& r) { return r.bottom - r.top; }
inline bool Contains(const Rect& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline Rect Inset(const Rect& r, float dx, float dy) { return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }
Color Hex(uint32_t rgb, float alpha = 1.0f);
Color WithAlpha(Color c, float a);

namespace glyph {
inline constexpr wchar_t kPC = 0xE7F4, kLaptop = 0xE7F8, kPhone = 0xE8EA, kTablet = 0xE70A, kServer = 0xE968,
                         kDevices = 0xE8CC, kSettings = 0xE713, kClose = 0xE711, kSend = 0xE724,
                         kDownload = 0xE896, kUpload = 0xE898, kFolderOpen = 0xE838, kFolder = 0xE8B7,
                         kOpenFile = 0xE8E5, kDelete = 0xE74D, kStar = 0xE734, kStarFill = 0xE735,
                         kWarning = 0xE7BA, kError = 0xE783, kCheck = 0xE73E, kMore = 0xE712,
                         kChevronRight = 0xE76C, kChevronDown = 0xE70D, kOpenInNew = 0xE8A7, kAdd = 0xE710,
                         kRefresh = 0xE72C, kInfo = 0xE946, kDocument = 0xE8A5, kFolderGo = 0xE8DA;
}

struct Theme {
  bool dark = false;
  Color text, text2, text3, textDisabled;
  Color accent, accentHover, accentPressed, onAccent, accentText;
  Color ctrl, ctrlHover, ctrlPressed, ctrlDisabled, ctrlStroke, ctrlStrokeBottom, strokeStrong;
  Color subtleHover, subtlePressed;
  Color card, cardHover, cardStroke, divider;
  Color solidBase, solidFlyout;
  Color success, caution, critical;
  Color layer;  // translucent layer used to separate regions on acrylic
};

// "system" follows the taskbar/Windows mode (tray flyouts, toasts);
// the app theme follows the "app mode" setting (regular windows).
const Theme& SystemTheme();
const Theme& AppTheme();
void ReloadThemes();
bool HasSystemBackdrop();  // Windows 11 22H2+

enum class Font { Text, Display, Icon };

// Process-wide drawing resources. Created lazily on first paint.
class Gfx {
 public:
  static Gfx& Get();
  ID2D1Factory* factory() { return factory_; }
  IDWriteFactory* dwrite() { return dwrite_; }
  ID2D1DCRenderTarget* rt() { return rt_; }
  ID2D1SolidColorBrush* brush() { return brush_; }
  IDWriteTextFormat* Format(float size, int weight, Font font, IDWriteInlineObject** trimming = nullptr);
  bool Begin(int w, int h, float scale);
  void End(HDC target, int w, int h);
  // Frees every D2D/DWrite object (and WARP's device memory with them);
  // called when nothing is on screen. The next paint re-creates them.
  void ReleaseIdle();
  unsigned generation() const { return generation_; }

 private:
  Gfx() = default;
  bool Init();
  bool initialized_ = false;
  ID2D1Factory* factory_ = nullptr;
  IDWriteFactory* dwrite_ = nullptr;
  ID2D1DCRenderTarget* rt_ = nullptr;
  ID2D1SolidColorBrush* brush_ = nullptr;
  HDC memdc_ = nullptr;
  HBITMAP dib_ = nullptr;
  HGDIOBJ oldbmp_ = nullptr;
  int dibW_ = 0, dibH_ = 0;
  unsigned generation_ = 1;
  std::wstring textFamily_, displayFamily_, iconFamily_;
  struct Fmt {
    float size;
    int weight;
    Font font;
    IDWriteTextFormat* fmt;
    IDWriteInlineObject* trim;
  };
  std::vector<Fmt> formats_;
};

// Premultiplied BGRA image (thumbnails) plus a lazily created D2D bitmap.
struct Image {
  int w = 0, h = 0;
  bool isIcon = false;
  std::vector<uint32_t> px;
  ID2D1Bitmap* bmp = nullptr;
  unsigned gen = 0;
  ~Image();
  ID2D1Bitmap* Bitmap();
  HBITMAP CreateDragBitmap(int maxSize, SIZE* outSize) const;
};
// Loads a shell thumbnail (or the file icon). Safe on worker threads.
std::shared_ptr<Image> LoadThumbnail(const std::wstring& path, int sizePx);

enum TextFlags : unsigned {
  kAlignLeft = 0,
  kAlignCenter = 1,
  kAlignRight = 2,
  kVCenter = 0,
  kVTop = 4,
  kVBottom = 8,
  kWrap = 16,
};

enum class ButtonStyle { Standard, Accent, Subtle };

class Window;

class Canvas {
 public:
  Canvas(Window* w, const Theme& th);
  const Theme& th;
  ID2D1RenderTarget* rt;

  void Fill(const Rect& r, Color c, float radius = 0);
  void Stroke(const Rect& r, Color c, float radius = 0, float widthPx = 1);
  void Line(float x1, float y1, float x2, float y2, Color c, float widthPx = 1);
  void Circle(float cx, float cy, float radius, Color c);
  void Text(std::wstring_view s, const Rect& r, float size, Color c, int weight = 400, unsigned flags = 0);
  void Glyph(wchar_t g, const Rect& r, float size, Color c);
  float TextWidth(std::wstring_view s, float size, int weight = 400);
  float TextHeight(std::wstring_view s, float width, float size, int weight = 400);
  // cover = crop to fill; otherwise fit inside (icons are never upscaled past 1x).
  void DrawImage(Image* img, const Rect& r, float radius, bool cover);

  void Hit(int id, const Rect& r);
  bool Hover(int id) const;
  bool Pressed(int id) const;
  bool DropHover(int id) const;

  void Button(int id, const Rect& r, std::wstring_view label, ButtonStyle style = ButtonStyle::Standard,
              wchar_t glyph = 0, bool enabled = true);
  void IconButton(int id, const Rect& r, wchar_t glyph, float size = 16, const Color* color = nullptr);
  void Toggle(int id, const Rect& r, bool on);
  void Progress(const Rect& r, double fraction);
  void PushClip(const Rect& r);
  void PopClip();

 private:
  Window* w_;
  ID2D1SolidColorBrush* brush_;
  float px_;  // one physical pixel in DIPs
};

enum class WindowKind { Popup, ToastPopup, Normal };

class DropTarget;

class Window {
 public:
  virtual ~Window();
  HWND hwnd() const { return hwnd_; }
  bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
  float scale() const { return scale_; }
  void Invalidate() const;
  void Close();
  void ApplyTheme();
  const Theme& theme() const { return transientTheme_ ? SystemTheme() : AppTheme(); }
  std::function<void(Window*)> onDestroyed;

  // Used by DropTarget.
  int HitTest(float x, float y) const;
  virtual DWORD OnDragOver(int id, const std::vector<std::wstring>& files) { return DROPEFFECT_NONE; }
  virtual void DropLabel(int id, DROPIMAGETYPE& type, std::wstring& message, std::wstring& insert) {}
  virtual void OnDragEnter() {}
  virtual void OnDragLeave() {}
  virtual void OnDrop(int id, std::vector<std::wstring> files) {}
  int dropHover_ = 0;
  bool dragActive_ = false;
  int hover_ = 0, pressed_ = 0;
  std::vector<std::pair<int, Rect>> hits_;

 protected:
  bool CreateWin(WindowKind kind, const wchar_t* title, const RECT& px, bool transientTheme);
  virtual void Paint(Canvas& c, float w, float h) = 0;
  virtual void OnClick(int id) {}
  virtual void OnRightClick(int id, POINT screen) {}
  virtual bool IsDraggable(int id) const { return false; }
  virtual void OnDragStart(int id) {}
  virtual void OnWheel(float delta) {}
  virtual void OnTimer(UINT_PTR id) {}
  virtual void OnActivate(bool active) {}
  virtual void OnEscape() { Close(); }
  virtual void OnHoverChanged() {}
  virtual void OnDpiChanged() {}
  virtual LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp, bool& handled) { return 0; }
  void EnableDrop();
  // Converts DIPs to physical pixels for this window's DPI.
  int Px(float dips) const { return static_cast<int>(dips * scale_ + 0.5f); }

  HWND hwnd_ = nullptr;
  float scale_ = 1.0f;
  WindowKind kind_ = WindowKind::Popup;
  bool transientTheme_ = true;
  bool tracking_ = false;
  bool backdrop_ = false;

 private:
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT WndProc(UINT, WPARAM, LPARAM);
  void DoPaint();
  DropTarget* drop_ = nullptr;
};

// Starts a real shell drag of files (same data object Explorer uses).
// Returns the effect reported by the drop target.
DWORD StartFileDrag(HWND hwnd, const std::vector<std::wstring>& paths, const Image* preview);

struct MenuItem {
  int id = 0;  // 0 = separator
  std::wstring text;
  bool checked = false;
  bool disabled = false;
};
int ShowMenu(HWND owner, POINT screen, const std::vector<MenuItem>& items);

// Applies uxtheme's (undocumented) dark-mode switch so native menus follow
// the system theme.
void InitDarkMenus();
void AllowDarkFor(HWND hwnd);

float DpiScaleForPoint(POINT pt);
RECT WorkAreaForPoint(POINT pt);

}  // namespace ui
