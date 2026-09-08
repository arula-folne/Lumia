# Package LumiaMusicView installer for GitHub Release (Windows x64 OBS plugin)
param(
  [string]$Version = "0.2.2",
  [string]$OutDir = "",
  [switch]$AlsoZip
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Plugin = Join-Path $Root "obs-plugin"
$Dll = Join-Path $Plugin "build\lumia-music-view.dll"
$Data = Join-Path $Plugin "data"
$Iss = Join-Path $Root "installer\LumiaMusicView.iss"

function Find-ISCC {
  $candidates = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 7\ISCC.exe"
  )
  foreach ($path in $candidates) {
    if ($path -and (Test-Path $path)) { return $path }
  }
  $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  return $null
}

if (-not (Test-Path $Dll)) {
  throw "Build first: obs-plugin\build.bat (missing $Dll)"
}
if (-not (Test-Path $Iss)) {
  throw "Missing Inno script: $Iss"
}

$Iscc = Find-ISCC
if (-not $Iscc) {
  throw "Inno Setup (ISCC.exe) not found. Install: winget install JRSoftware.InnoSetup"
}

foreach ($doc in @("LICENSE.txt", "INSTALL.txt", "README.txt")) {
  $path = Join-Path $Root $doc
  if (-not (Test-Path $path)) {
    throw "Missing $doc"
  }
}

if (-not $OutDir) {
  $OutDir = Join-Path $Root "dist"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Stage = Join-Path $OutDir "stage-installer"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }

$PluginDir = Join-Path $Stage "obs-plugins\64bit"
$DataDir = Join-Path $Stage "data\obs-plugins\lumia-music-view"
New-Item -ItemType Directory -Force -Path $PluginDir | Out-Null
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null

Copy-Item $Dll (Join-Path $PluginDir "lumia-music-view.dll")
Copy-Item (Join-Path $Data "overlay") (Join-Path $DataDir "overlay") -Recurse
Copy-Item (Join-Path $Data "locale") (Join-Path $DataDir "locale") -Recurse
Copy-Item (Join-Path $Root "LICENSE.txt") (Join-Path $Stage "LICENSE.txt")
Copy-Item (Join-Path $Root "INSTALL.txt") (Join-Path $Stage "INSTALL.txt")
Copy-Item (Join-Path $Root "README.txt") (Join-Path $Stage "README.txt")

$ExeName = "LumiaMusicView-$Version-windows-x64.exe"
$ExePath = Join-Path $OutDir $ExeName
if (Test-Path $ExePath) { Remove-Item -Force $ExePath }

$stageForIss = $Stage
& $Iscc `
  "/DMyAppVersion=$Version" `
  "/DStageDir=$stageForIss" `
  "/DOutDir=$OutDir" `
  $Iss
if ($LASTEXITCODE -ne 0) {
  throw "ISCC failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path $ExePath)) {
  throw "Installer was not created: $ExePath"
}

if ($AlsoZip) {
  $ZipName = "LumiaMusicView-$Version-windows-x64.zip"
  $ZipPath = Join-Path $OutDir $ZipName
  if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
  Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $ZipPath -Force
  Write-Host "Created $ZipPath"
}

Remove-Item -Recurse -Force $Stage

Write-Host "Created $ExePath"
Get-Item $ExePath | Format-List FullName, Length






