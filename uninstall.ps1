<#
.SYNOPSIS
  Removes TailDrop for the current user.
.DESCRIPTION
  Files still waiting in the inbox are kept (%LOCALAPPDATA%\YuboCao\TailDrop\Inbox)
  unless -RemoveInbox is given; they go to the Recycle Bin in that case.
#>
param([switch]$RemoveInbox)
$ErrorActionPreference = 'Continue'
$dest = Join-Path $env:LOCALAPPDATA 'Programs\TailDrop'
$data = Join-Path $env:LOCALAPPDATA 'YuboCao\TailDrop'
$appId = 'YuboCao.TailDrop'
$clsid = '{9639F786-0C93-4CE7-BB89-6F0E749C036D}'

$p = Get-Process TailDrop -ErrorAction SilentlyContinue
if ($p) {
  $exe = ($p | Select-Object -First 1).Path
  if ($exe) { & $exe --exit }
  $p | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
  Get-Process TailDrop -ErrorAction SilentlyContinue | Stop-Process -Force
}

Get-AppxPackage -Name $appId | Remove-AppxPackage
foreach ($type in '*', 'Directory') { Remove-Item -LiteralPath "HKCU:\Software\Classes\$type\shell\$appId" -Recurse -Force -ErrorAction SilentlyContinue }
Remove-Item "HKCU:\Software\Classes\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name $appId -ErrorAction SilentlyContinue
Remove-Item "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$appId" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item 'HKCU:\Software\YuboCao\TailDrop' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path ([Environment]::GetFolderPath('Programs')) 'TailDrop (native).lnk') -Force -ErrorAction SilentlyContinue

Remove-Item (Join-Path $data 'taildrop.log') -Force -ErrorAction SilentlyContinue
$inbox = Join-Path $data 'Inbox'
$waiting = @(Get-ChildItem $inbox -File -ErrorAction SilentlyContinue)
if ($waiting.Count -and $RemoveInbox) {
  Add-Type -AssemblyName Microsoft.VisualBasic
  foreach ($f in $waiting) {
    [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($f.FullName, 'OnlyErrorDialogs', 'SendToRecycleBin')
  }
} elseif ($waiting.Count) {
  Write-Host "Kept $($waiting.Count) received file(s) in $inbox"
}

# Explorer may still hold the shell extension; a locked DLL is left behind
# (harmless once unregistered) and can be deleted after signing out.
Remove-Item (Join-Path $dest 'package') -Recurse -Force -ErrorAction SilentlyContinue
Get-ChildItem $dest -File -ErrorAction SilentlyContinue | ForEach-Object {
  try { Remove-Item $_.FullName -Force -ErrorAction Stop }
  catch { Write-Host "In use, will be left behind: $($_.Name)" }
}
Remove-Item $dest -Force -ErrorAction SilentlyContinue
Write-Host 'TailDrop was removed.'
