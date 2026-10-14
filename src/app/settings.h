#pragma once

#include <string>

struct Settings {
  std::wstring downloadDir;  // empty: the user's Downloads folder
  bool autoSave = false;     // save without waiting for the Save button
  bool trayDrop = true;      // drop shelf when dragging onto the tray icon
  bool runAtLogin = true;

  void Load();
  void Save() const;
  std::wstring EffectiveDownloadDir() const;
  void ApplyRunAtLogin() const;
};
