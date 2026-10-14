// Definitions shared by TailDrop.exe and the Explorer extension DLL.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace td {

// Internal names are namespaced: an unrelated third-party "TailDrop" app
// exists and uses %LOCALAPPDATA%\TailDrop.
inline constexpr wchar_t kHostClass[] = L"YuboCao.TailDrop.Host";
inline constexpr wchar_t kRegKey[] = L"Software\\YuboCao\\TailDrop";
inline constexpr wchar_t kAppId[] = L"YuboCao.TailDrop";
inline constexpr wchar_t kExeName[] = L"TailDrop.exe";

// WM_COPYDATA commands understood by the host window. Payloads are UTF-16
// strings separated by L'\0' and terminated by an empty string.
inline constexpr ULONG_PTR kCopySend = 0x54440001;  // stableId, path, path...
inline constexpr ULONG_PTR kCopyPick = 0x54440002;  // path, path...
inline constexpr ULONG_PTR kCopyShow = 0x54440003;  // (empty) open the flyout

// Posted by the shell extension when a menu opens so the host refreshes the
// cached online state of favorites for the next time.
inline constexpr UINT kMsgRefreshDevices = WM_APP + 0x40;

inline constexpr int kMaxFavorites = 3;

// CLSID of the IExplorerCommand implemented by TailDropShell.dll.
// {9639F786-0C93-4CE7-BB89-6F0E749C036D}
inline constexpr CLSID kClsidSendCommand = {
    0x9639f786, 0x0c93, 0x4ce7, {0xbb, 0x89, 0x6f, 0x0e, 0x74, 0x9c, 0x03, 0x6d}};
inline constexpr wchar_t kClsidSendCommandStr[] = L"{9639F786-0C93-4CE7-BB89-6F0E749C036D}";

struct Favorite {
  std::wstring stableId;
  std::wstring name;
  std::wstring kind;  // pc, laptop, phone, tablet, server, other
  bool online = true;
};

inline std::wstring RegGetString(const wchar_t* name, const wchar_t* fallback = L"") {
  wchar_t buf[2048];
  DWORD size = sizeof(buf);
  if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
    return fallback;
  return buf;
}

inline DWORD RegGetDword(const wchar_t* name, DWORD fallback) {
  DWORD v = 0, size = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
    return fallback;
  return v;
}

inline void RegSetRaw(const wchar_t* name, DWORD type, const void* data, DWORD size) {
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS)
    return;
  RegSetValueExW(key, name, 0, type, static_cast<const BYTE*>(data), size);
  RegCloseKey(key);
}

inline void RegSetString(const wchar_t* name, const std::wstring& value) {
  RegSetRaw(name, REG_SZ, value.c_str(), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

inline void RegSetDword(const wchar_t* name, DWORD value) { RegSetRaw(name, REG_DWORD, &value, sizeof(value)); }

// Favorites are stored as REG_MULTI_SZ entries "stableId\tname\tkind\tonline".
inline std::vector<Favorite> LoadFavorites() {
  std::vector<Favorite> out;
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"Favorites", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) !=
          ERROR_SUCCESS ||
      size == 0)
    return out;
  std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
  if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"Favorites", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) !=
      ERROR_SUCCESS)
    return out;
  for (const wchar_t* p = buf.c_str(); *p && out.size() < kMaxFavorites; p += wcslen(p) + 1) {
    std::wstring line = p;
    std::vector<std::wstring> f;
    size_t start = 0;
    for (;;) {
      size_t tab = line.find(L'\t', start);
      f.push_back(line.substr(start, tab == std::wstring::npos ? std::wstring::npos : tab - start));
      if (tab == std::wstring::npos) break;
      start = tab + 1;
    }
    if (f.size() < 2 || f[0].empty()) continue;
    Favorite fav;
    fav.stableId = f[0];
    fav.name = f[1];
    fav.kind = f.size() > 2 ? f[2] : L"other";
    fav.online = f.size() > 3 ? f[3] != L"0" : true;
    out.push_back(std::move(fav));
  }
  return out;
}

inline void SaveFavorites(const std::vector<Favorite>& favs) {
  std::wstring blob;
  for (size_t i = 0; i < favs.size() && i < kMaxFavorites; ++i) {
    const Favorite& f = favs[i];
    blob += f.stableId + L"\t" + f.name + L"\t" + f.kind + L"\t" + (f.online ? L"1" : L"0");
    blob.push_back(L'\0');
  }
  blob.push_back(L'\0');
  RegSetRaw(L"Favorites", REG_MULTI_SZ, blob.data(), static_cast<DWORD>(blob.size() * sizeof(wchar_t)));
}

// Joins strings into a WM_COPYDATA payload (see kCopySend).
inline std::wstring PackStrings(const std::vector<std::wstring>& parts) {
  std::wstring out;
  for (const auto& p : parts) {
    out += p;
    out.push_back(L'\0');
  }
  out.push_back(L'\0');
  return out;
}

inline std::vector<std::wstring> UnpackStrings(const wchar_t* data, size_t chars) {
  std::vector<std::wstring> out;
  size_t i = 0;
  while (i < chars && data[i]) {
    size_t len = 0;
    while (i + len < chars && data[i + len]) ++len;
    out.emplace_back(data + i, len);
    i += len + 1;
  }
  return out;
}

}  // namespace td
