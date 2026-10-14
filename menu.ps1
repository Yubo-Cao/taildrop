<#
.SYNOPSIS
  Adds or removes "Send with TailDrop" in the File Explorer context menu.
.DESCRIPTION
  Used by install.ps1, uninstall.ps1 and the setup program. Run from the
  install folder (it must contain TailDrop.exe, TailDropShell.dll,
  TailDrop.msix and package\).

  Windows 11's own menu needs package identity, from a sparse package that
  Windows accepts only when signed or, with Developer Mode on, registered in
  place. When neither works, the command goes into the classic menu
  ("Show more options") through a per-user COM registration instead.
#>
param([switch]$Register, [switch]$Unregister)
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot
$appId = 'YuboCao.TailDrop'
$clsid = '{9639F786-0C93-4CE7-BB89-6F0E749C036D}'
$settingsKey = 'HKCU:\Software\YuboCao\TailDrop'
$cls = "HKCU:\Software\Classes\CLSID\$clsid"
$verbs = foreach ($type in '*', 'Directory') { "HKCU:\Software\Classes\$type\shell\$appId" }

function Remove-Classic {
  foreach ($v in $verbs) { Remove-Item -LiteralPath $v -Recurse -Force -ErrorAction SilentlyContinue }
  Remove-Item -LiteralPath $cls -Recurse -Force -ErrorAction SilentlyContinue
}

if ($Unregister) {
  Get-AppxPackage -Name $appId -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue
  Remove-Classic
  if (Test-Path $settingsKey) { Remove-ItemProperty $settingsKey -Name Win11Menu -ErrorAction SilentlyContinue }
  return
}
if (-not $Register) { throw 'Pass -Register or -Unregister.' }

$win11 = $false
if ([Environment]::OSVersion.Version.Build -ge 22000) {
  $msix = Join-Path $dir 'TailDrop.msix'
  $signed = (Get-AuthenticodeSignature $msix).Status -eq 'Valid'
  $devMode = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -ErrorAction SilentlyContinue).AllowDevelopmentWithoutDevLicense -eq 1
  if ($signed -or $devMode) {
    try {
      Get-AppxPackage -Name $appId | Remove-AppxPackage -ErrorAction SilentlyContinue
      if ($signed) { Add-AppxPackage -Path $msix -ExternalLocation $dir }
      else { Add-AppxPackage -Register (Join-Path $dir 'package\AppxManifest.xml') -ExternalLocation $dir }
      $win11 = $true
    } catch {
      Write-Warning "Windows 11 menu registration failed: $($_.Exception.Message)"
    }
  }
}

if ($win11) {
  Remove-Classic  # the package's command already shows in the classic menu
} else {
  New-Item -Path "$cls\InprocServer32" -Force | Out-Null
  Set-Item -Path $cls -Value 'TailDrop Explorer menu'
  Set-Item -Path "$cls\InprocServer32" -Value (Join-Path $dir 'TailDropShell.dll')
  New-ItemProperty -Path "$cls\InprocServer32" -Name ThreadingModel -Value Apartment -Force | Out-Null
  foreach ($v in $verbs) {
    New-Item -Path $v -Force | Out-Null
    New-ItemProperty -LiteralPath $v -Name ExplorerCommandHandler -Value $clsid -Force | Out-Null
    New-ItemProperty -LiteralPath $v -Name MUIVerb -Value 'Send with TailDrop' -Force | Out-Null
  }
}

if (-not (Test-Path $settingsKey)) { New-Item -Path $settingsKey -Force | Out-Null }
New-ItemProperty -Path $settingsKey -Name Win11Menu -Value ([int]$win11) -PropertyType DWord -Force | Out-Null
if ($win11) { 'windows11' } else { 'classic' }
