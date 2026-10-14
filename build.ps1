<#
.SYNOPSIS
  Builds TailDrop.exe, TailDropShell.dll and the TailDrop.msix sparse package
  into build\ with MSVC + Ninja.
.PARAMETER Publisher
  Subject of the certificate that will sign TailDrop.msix. The package
  manifest's Publisher must match it exactly.
.EXAMPLE
  .\build.ps1            # Release
  .\build.ps1 -Config Debug
#>
param(
  [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
  [string]$Publisher = 'CN=YuboCao.TailDrop'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ workload was not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$build = Join-Path $root 'build'
# Prefer the CMake and Ninja that ship with Visual Studio over others on PATH.
$vsCMake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = Join-Path $vsCMake 'CMake\bin\cmake.exe'
$ninja = Join-Path $vsCMake 'Ninja\ninja.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake' }
$ninjaArg = if (Test-Path $ninja) { "-DCMAKE_MAKE_PROGRAM=`"$ninja`"" } else { '' }
# UTF-8 console so CMake reads localized /showIncludes output correctly.
$cmd = "chcp 65001 >nul && `"$vcvars`" >nul && `"$cmake`" -S `"$root`" -B `"$build`" -G Ninja $ninjaArg -DCMAKE_BUILD_TYPE=$Config -DCMAKE_CXX_COMPILER=cl -DCMAKE_RC_COMPILER=rc && `"$cmake`" --build `"$build`""
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

# Sparse package for the Windows 11 context menu (manifest + logos only).
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$makeappx = Get-ChildItem $kits -Recurse -Filter makeappx.exe -ErrorAction SilentlyContinue |
  Where-Object { $_.Directory.Name -eq 'x64' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $makeappx) { throw 'makeappx.exe (Windows SDK) was not found.' }
$stage = Join-Path $build 'msix'
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root 'package') $stage -Recurse
$manifest = Join-Path $stage 'AppxManifest.xml'
$xml = [IO.File]::ReadAllText($manifest) -replace 'Publisher="[^"]*"', "Publisher=`"$([Security.SecurityElement]::Escape($Publisher))`""
[IO.File]::WriteAllText($manifest, $xml)
& $makeappx.FullName pack /d $stage /p (Join-Path $build 'TailDrop.msix') /nv /o | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makeappx failed ($LASTEXITCODE)" }

Get-Item (Join-Path $build 'TailDrop.exe'), (Join-Path $build 'TailDropShell.dll'), (Join-Path $build 'TailDrop.msix') |
  Select-Object Name, Length
