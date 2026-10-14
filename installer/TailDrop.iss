; Inno Setup 6 script for TailDrop's per-user installer.
;
;   iscc installer\TailDrop.iss                       (binaries from build\)
;   iscc /DSourceDir=..\dist\app installer\TailDrop.iss

#ifndef SourceDir
  #define SourceDir "..\build"
#endif
#define AppExe AddBackslash(SourcePath) + SourceDir + "\TailDrop.exe"
#define AppVersion GetStringFileInfo(AppExe, "ProductVersion")

[Setup]
AppId={{B70F3C5A-FF1C-49AB-96B0-17C52F18D3DA}
AppName=TailDrop
AppVersion={#AppVersion}
AppVerName=TailDrop {#AppVersion}
AppPublisher=Yubo Cao
AppPublisherURL=https://github.com/Yubo-Cao/taildrop
AppSupportURL=https://github.com/Yubo-Cao/taildrop/issues
AppUpdatesURL=https://github.com/Yubo-Cao/taildrop/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName=TailDrop
; Per-user install, no administrator rights: %LOCALAPPDATA%\Programs\TailDrop.
PrivilegesRequired=lowest
DefaultDirName={userpf}\TailDrop
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir=..\dist
OutputBaseFilename=TailDrop-{#AppVersion}-Setup
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\TailDrop.exe
UninstallDisplayName=TailDrop
WizardStyle=modern
WizardSizePercent=100
Compression=lzma2/max
SolidCompression=yes
; TailDrop is stopped explicitly in PrepareToInstall.
CloseApplications=no
RestartApplications=no

[Messages]
SetupWindowTitle=Install %1

[Tasks]
Name: autostart; Description: "Start TailDrop when I sign in (recommended)"
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#SourceDir}\TailDrop.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\TailDropShell.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\TailDrop.msix"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\package\*"; DestDir: "{app}\package"; Flags: ignoreversion recursesubdirs
Source: "..\menu.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
; Not "TailDrop.lnk": an unrelated app called TailDrop may already own that name.
Name: "{userprograms}\TailDrop (native)"; Filename: "{app}\TailDrop.exe"; Comment: "Send and receive files with Taildrop"
Name: "{userdesktop}\TailDrop (native)"; Filename: "{app}\TailDrop.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "YuboCao.TailDrop"; ValueData: """{app}\TailDrop.exe"" --background"; Flags: uninsdeletevalue; Tasks: autostart
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "YuboCao.TailDrop"; Flags: deletevalue dontcreatekey; Tasks: not autostart
Root: HKCU; Subkey: "Software\YuboCao\TailDrop"; ValueType: dword; ValueName: "RunAtLogin"; ValueData: "1"; Tasks: autostart
Root: HKCU; Subkey: "Software\YuboCao\TailDrop"; ValueType: dword; ValueName: "RunAtLogin"; ValueData: "0"; Tasks: not autostart
Root: HKCU; Subkey: "Software\YuboCao\TailDrop"; ValueType: dword; ValueName: "Configured"; ValueData: "1"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\YuboCao"; Flags: uninsdeletekeyifempty
; Replaces an install made with install.ps1 (its own Apps entry).
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\YuboCao.TailDrop"; Flags: deletekey dontcreatekey

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\menu.ps1"" -Register"; Flags: runhidden waituntilterminated; StatusMsg: "Adding ""Send with TailDrop"" to File Explorer..."
Filename: "{app}\TailDrop.exe"; Description: "Start TailDrop"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\TailDrop.exe"; Parameters: "--exit"; Flags: runhidden waituntilterminated; RunOnceId: "StopTailDrop"
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\menu.ps1"" -Unregister"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveExplorerMenu"

[InstallDelete]
; Left by an earlier install made with install.ps1.
Type: files; Name: "{app}\uninstall.ps1"

[UninstallDelete]
; Windows adds package metadata there when it registers the package in place.
Type: filesandordirs; Name: "{app}\package"
Type: files; Name: "{app}\*.old"
Type: dirifempty; Name: "{app}"
Type: files; Name: "{localappdata}\YuboCao\TailDrop\taildrop.log"

[Code]
procedure StopTailDrop();
var
  Code: Integer;
  Exe: String;
begin
  Exe := ExpandConstant('{app}\TailDrop.exe');
  if FileExists(Exe) then begin
    Exec(Exe, '--exit', '', SW_HIDE, ewWaitUntilTerminated, Code);
    Sleep(800);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Dll: String;
begin
  StopTailDrop();
  // File Explorer (or its COM surrogate) keeps the menu extension loaded;
  // Windows lets a loaded DLL be renamed, so move it aside.
  Dll := ExpandConstant('{app}\TailDropShell.dll');
  if FileExists(Dll) and not DeleteFile(Dll) then
    RenameFile(Dll, Dll + '.' + GetDateTimeString('yyyymmddhhnnss', #0, #0) + '.old');
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Dll: String;
begin
  if CurUninstallStep = usUninstall then begin
    Dll := ExpandConstant('{app}\TailDropShell.dll');
    if FileExists(Dll) and not DeleteFile(Dll) then
      RenameFile(Dll, Dll + '.old');
  end;
end;
