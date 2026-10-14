#include "settings.h"

#include <windows.h>
#include <shlobj.h>

#include "../common/shared.h"
#include "util.h"

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}

void Settings::Load() {
  downloadDir = td::RegGetString(L"DownloadDir");
  autoSave = td::RegGetDword(L"AutoSave", 0) != 0;
  trayDrop = td::RegGetDword(L"TrayDrop", 1) != 0;
  runAtLogin = td::RegGetDword(L"RunAtLogin", 1) != 0;
}

void Settings::Save() const {
  td::RegSetString(L"DownloadDir", downloadDir);
  td::RegSetDword(L"AutoSave", autoSave);
  td::RegSetDword(L"TrayDrop", trayDrop);
  td::RegSetDword(L"RunAtLogin", runAtLogin);
}

std::wstring Settings::EffectiveDownloadDir() const {
  if (!downloadDir.empty() && util::IsDirectory(downloadDir)) return downloadDir;
  return util::KnownFolder(FOLDERID_Downloads);
}

void Settings::ApplyRunAtLogin() const {
  if (runAtLogin) {
    std::wstring cmd = L"\"" + util::ExePath() + L"\" --background";
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
      RegSetValueExW(key, td::kAppId, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                     static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
      RegCloseKey(key);
    }
  } else {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, td::kAppId);
  }
}
