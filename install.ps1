<#
.SYNOPSIS
  Installs TailDrop for the current user. No administrator rights needed.

.DESCRIPTION
  Works from a source checkout (builds first) or from an extracted release zip.
  It makes these changes, all per-user:
    - copies TailDrop to %LOCALAPPDATA%\Programs\TailDrop
    - adds "Send with TailDrop" to the File Explorer context menu (menu.ps1)
    - adds a Start menu shortcut, an Apps entry and the sign-in autostart entry
    - starts TailDrop
  uninstall.ps1 reverts all of it. Release downloads also include a setup
  program (TailDrop-<version>-Setup.exe) that does the same with a wizard.
#>
param([switch]$NoBuild, [switch]$NoStart)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$dest = Join-Path $env:LOCALAPPDATA 'Programs\TailDrop'
$appId = 'YuboCao.TailDrop'
$settingsKey = 'HKCU:\Software\YuboCao\TailDrop'

# Release zips carry the binaries next to this script; checkouts build them.
$bin = Join-Path $root 'build'
if (Test-Path (Join-Path $root 'TailDrop.exe')) { $bin = $root }
elseif (-not $NoBuild) { & (Join-Path $root 'build.ps1') | Out-Null }
foreach ($f in 'TailDrop.exe', 'TailDropShell.dll', 'TailDrop.msix', 'ui\TailDropUI.exe') {
  if (-not (Test-Path (Join-Path $bin $f))) { throw "Missing $f. Run .\build.ps1 first." }
}

Write-Host 'Installing TailDrop for this user:'
Write-Host "  - files in $dest"
Write-Host '  - "Send with TailDrop" in the File Explorer context menu'
Write-Host '  - Start menu shortcut, Apps entry, start at sign-in'

# 1. Stop a running copy.
$running = Get-Process TailDrop -ErrorAction SilentlyContinue
if ($running) {
  $exe = ($running | Select-Object -First 1).Path
  if ($exe) { & $exe --exit; Start-Sleep -Milliseconds 300 }
  $running | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
  Get-Process TailDrop -ErrorAction SilentlyContinue | Stop-Process -Force
}
# The interface (TailDropUI.exe) exits with it; make sure before its files are replaced.
Get-Process TailDropUI -ErrorAction SilentlyContinue | Stop-Process -Force

# 2. Copy files. Explorer keeps the shell extension loaded, so an in-use DLL
#    is renamed out of the way (Windows allows that) and cleaned up next time.
New-Item -ItemType Directory -Force $dest | Out-Null
Get-ChildItem $dest -Filter '*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
$dll = Join-Path $dest 'TailDropShell.dll'
if (Test-Path $dll) {
  try { Remove-Item $dll -Force -ErrorAction Stop }
  catch { Rename-Item $dll ("TailDropShell.{0}.old" -f (Get-Date -Format 'yyyyMMddHHmmss')) }
}
foreach ($f in 'TailDrop.exe', 'TailDropShell.dll', 'TailDrop.msix') { Copy-Item (Join-Path $bin $f) $dest -Force }
# The WinUI 3 interface, replaced as a whole.
Remove-Item (Join-Path $dest 'ui') -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $bin 'ui') (Join-Path $dest 'ui') -Recurse
Copy-Item (Join-Path $root 'uninstall.ps1'), (Join-Path $root 'menu.ps1') $dest -Force
Remove-Item (Join-Path $dest 'package') -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root 'package') (Join-Path $dest 'package') -Recurse
# -Force on an existing key would wipe its values (the user's settings).
if (-not (Test-Path $settingsKey)) { New-Item -Path $settingsKey -Force | Out-Null }

# 3. Explorer context menu.
$menu = & (Join-Path $dest 'menu.ps1') -Register
if ($menu -ne 'windows11' -and [Environment]::OSVersion.Version.Build -ge 22000) {
  Write-Host '  Note: "Send with TailDrop" is under "Show more options" for now. For the Windows 11'
  Write-Host '        menu itself, turn on Settings > System > For developers > Developer Mode and run this again.'
}

# 4. Start menu shortcut. "TailDrop.lnk" is left alone: another app uses that name.
$lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'TailDrop (native).lnk'
$s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
$s.TargetPath = Join-Path $dest 'TailDrop.exe'
$s.WorkingDirectory = $dest
$s.Description = 'Send and receive files with Taildrop'
$s.Save()

# 6. Start at sign-in, unless it was turned off in Settings.
$runAtLogin = (Get-ItemProperty -Path $settingsKey -Name RunAtLogin -ErrorAction SilentlyContinue).RunAtLogin
if ($null -eq $runAtLogin -or $runAtLogin -ne 0) {
  New-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name $appId `
    -Value "`"$(Join-Path $dest 'TailDrop.exe')`" --background" -Force | Out-Null
}
New-ItemProperty -Path $settingsKey -Name Configured -Value 1 -PropertyType DWord -Force | Out-Null

# 7. Apps entry.
$un = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId"
New-Item -Path $un -Force | Out-Null
$version = (Get-Item (Join-Path $dest 'TailDrop.exe')).VersionInfo.ProductVersion
$props = @{
  DisplayName     = 'TailDrop'
  DisplayVersion  = $version
  Publisher       = 'Yubo Cao'
  DisplayIcon     = (Join-Path $dest 'TailDrop.exe')
  InstallLocation = $dest
  URLInfoAbout    = 'https://github.com/Yubo-Cao/taildrop'
  UninstallString = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $dest 'uninstall.ps1')`""
}
foreach ($k in $props.Keys) { New-ItemProperty -Path $un -Name $k -Value $props[$k] -Force | Out-Null }
foreach ($k in 'NoModify', 'NoRepair') { New-ItemProperty -Path $un -Name $k -Value 1 -PropertyType DWord -Force | Out-Null }
$size = [int]((Get-ChildItem $dest -File -Recurse | Measure-Object Length -Sum).Sum / 1KB)
New-ItemProperty -Path $un -Name EstimatedSize -Value $size -PropertyType DWord -Force | Out-Null
Write-Host 'Installed.'

if (-not $NoStart) {
  try {
    Start-Process (Join-Path $dest 'TailDrop.exe') -ErrorAction Stop
    Write-Host 'TailDrop is running: look for the droplet in the system tray.'
  } catch {
    $sac = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' -ErrorAction SilentlyContinue).VerifiedAndReputablePolicyState
    if ($sac -eq 1) {
      Write-Warning ('Smart App Control blocked TailDrop because this build is not signed by a trusted certificate. ' +
        'Turn it off in Windows Security > App & browser control > Smart App Control, or use a signed release.')
    } else {
      throw
    }
  }
}
