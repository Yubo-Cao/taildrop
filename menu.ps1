<#
.SYNOPSIS
  Adds or removes "Send with TailDrop" in the File Explorer context menu.
.DESCRIPTION
  Used by install.ps1, uninstall.ps1 and the setup program. Run from the
  install folder (it must contain TailDrop.exe, TailDropShell.dll,
  TailDrop.msix and package\). Works in Windows PowerShell 5.1 and 7.

  Windows 11's own menu needs package identity, from a sparse package that
  Windows accepts only when signed or, with Developer Mode on, registered in
  place. When neither works (or with -Classic), the command goes into the
  classic menu ("Show more options") through a per-user COM registration.

  Problems are logged to %TEMP%\TailDrop-menu.log.
#>
param([switch]$Register, [switch]$Unregister, [switch]$Classic)
$ErrorActionPreference = 'Stop'

# 32-bit PowerShell (e.g. started by a 32-bit installer) sees the WOW64
# registry view and can't manage packages: rerun as 64-bit.
if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
  $ps64 = Join-Path $env:SystemRoot 'Sysnative\WindowsPowerShell\v1.0\powershell.exe'
  $argv = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath)
  if ($Register) { $argv += '-Register' }
  if ($Unregister) { $argv += '-Unregister' }
  if ($Classic) { $argv += '-Classic' }
  & $ps64 @argv
  exit $LASTEXITCODE
}
# Started from PowerShell 7, Windows PowerShell inherits 7's module paths and
# then fails to load its own built-in modules.
if ($PSVersionTable.PSEdition -eq 'Desktop') {
  $env:PSModulePath = ($env:PSModulePath -split ';' |
      Where-Object { $_ -and $_ -notmatch '\\PowerShell\\7' -and $_ -notmatch '\\Documents\\PowerShell\\' }) -join ';'
}
$log = Join-Path $env:TEMP 'TailDrop-menu.log'
trap {
  "$(Get-Date -Format s) $($_.InvocationInfo.PositionMessage)`n$_" | Out-File $log -Append -Encoding utf8
  exit 1
}

$dir = $PSScriptRoot
$appId = 'YuboCao.TailDrop'
$clsid = '{9639F786-0C93-4CE7-BB89-6F0E749C036D}'
$settingsKey = 'Software\YuboCao\TailDrop'
$clsKey = "Software\Classes\CLSID\$clsid"
# Registry paths go through .NET: PowerShell's provider treats the "*" class as a wildcard.
$verbKeys = foreach ($type in '*', 'Directory') { "Software\Classes\$type\shell\$appId" }
$hkcu = [Microsoft.Win32.Registry]::CurrentUser

function Remove-Classic {
  foreach ($k in $verbKeys + $clsKey) { $hkcu.DeleteSubKeyTree($k, $false) }
}

function Set-Win11Flag([int]$value) {
  $k = $hkcu.CreateSubKey($settingsKey)
  if ($value -lt 0) { $k.DeleteValue('Win11Menu', $false) } else { $k.SetValue('Win11Menu', $value, 'DWord') }
  $k.Close()
}

if ($Unregister) {
  Get-AppxPackage -Name $appId -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue
  Remove-Classic
  if ($hkcu.OpenSubKey($settingsKey)) { Set-Win11Flag -1 }
  return
}
if (-not $Register) { throw 'Pass -Register or -Unregister.' }

$win11 = $false
if (-not $Classic -and [Environment]::OSVersion.Version.Build -ge 22000) {
  $unlock = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock')
  $devMode = $unlock -and $unlock.GetValue('AllowDevelopmentWithoutDevLicense', 0) -eq 1
  try {
    Get-AppxPackage -Name $appId | Remove-AppxPackage -ErrorAction SilentlyContinue
    if ($devMode) {
      Add-AppxPackage -Register (Join-Path $dir 'package\AppxManifest.xml') -ExternalLocation $dir
    } else {
      # Only succeeds when TailDrop.msix is signed by a trusted certificate.
      Add-AppxPackage -Path (Join-Path $dir 'TailDrop.msix') -ExternalLocation $dir
    }
    $win11 = $true
  } catch {
    "$(Get-Date -Format s) Windows 11 menu not registered: $($_.Exception.Message)" | Out-File $log -Append -Encoding utf8
  }
}

if ($win11) {
  Remove-Classic  # the package's command already shows in the classic menu
} else {
  $k = $hkcu.CreateSubKey($clsKey)
  $k.SetValue('', 'TailDrop Explorer menu')
  $k.Close()
  $k = $hkcu.CreateSubKey("$clsKey\InprocServer32")
  $k.SetValue('', (Join-Path $dir 'TailDropShell.dll'))
  $k.SetValue('ThreadingModel', 'Apartment')
  $k.Close()
  foreach ($v in $verbKeys) {
    $k = $hkcu.CreateSubKey($v)
    $k.SetValue('ExplorerCommandHandler', $clsid)
    $k.SetValue('MUIVerb', 'Send with TailDrop')
    $k.Close()
  }
}

Set-Win11Flag ([int]$win11)
if ($win11) { 'windows11' } else { 'classic' }
