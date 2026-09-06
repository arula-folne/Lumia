@echo off
setlocal EnableExtensions
REM Install LumiaMusicView plugin + overlay data into OBS

set "ROOT=%~dp0"
set "DLL=%ROOT%build\lumia-music-view.dll"
set "DATA=%ROOT%data"
set "OBS=C:\Program Files\obs-studio"

if not exist "%DLL%" (
  echo Build first: build.bat
  exit /b 1
)

copy /Y "%DLL%" "%OBS%\obs-plugins\64bit\lumia-music-view.dll"
if errorlevel 1 (
  echo Copy failed — run as Administrator
  exit /b 1
)

mkdir "%OBS%\data\obs-plugins\lumia-music-view\overlay" 2>nul
mkdir "%OBS%\data\obs-plugins\lumia-music-view\locale" 2>nul
xcopy /Y /I /Q "%DATA%\overlay\*" "%OBS%\data\obs-plugins\lumia-music-view\overlay\"
xcopy /Y /I /Q "%DATA%\locale\*" "%OBS%\data\obs-plugins\lumia-music-view\locale\"
if errorlevel 1 (
  echo Data copy failed
  exit /b 1
)

echo Installed DLL + overlay data.
echo Restart OBS, then Add Source -^> LumiaMusicView
echo Select Music folder + Custom CSS only. No npm/server needed.
exit /b 0
