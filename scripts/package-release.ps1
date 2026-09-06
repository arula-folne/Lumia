# Package LumiaMusicView for GitHub Release (Windows x64 OBS plugin)
param(
  [string]$Version = "0.1.0",
  [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Plugin = Join-Path $Root "obs-plugin"
$Dll = Join-Path $Plugin "build\lumia-music-view.dll"
$Data = Join-Path $Plugin "data"

if (-not (Test-Path $Dll)) {
  throw "Build first: obs-plugin\build.bat (missing $Dll)"
}

if (-not $OutDir) {
  $OutDir = Join-Path $Root "dist"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Stage = Join-Path $OutDir "stage-LumiaMusicView-$Version"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }

$PluginDir = Join-Path $Stage "obs-plugins\64bit"
$DataDir = Join-Path $Stage "data\obs-plugins\lumia-music-view"
New-Item -ItemType Directory -Force -Path $PluginDir | Out-Null
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null

Copy-Item $Dll (Join-Path $PluginDir "lumia-music-view.dll")
Copy-Item (Join-Path $Data "overlay") (Join-Path $DataDir "overlay") -Recurse
Copy-Item (Join-Path $Data "locale") (Join-Path $DataDir "locale") -Recurse
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $Stage "LICENSE")
Copy-Item (Join-Path $Root "INSTALL.txt") (Join-Path $Stage "INSTALL.txt")

$ZipName = "LumiaMusicView-$Version-windows-x64.zip"
$ZipPath = Join-Path $OutDir $ZipName
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }

Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $ZipPath -Force
Remove-Item -Recurse -Force $Stage

Write-Host "Created $ZipPath"
Get-Item $ZipPath | Format-List FullName, Length
