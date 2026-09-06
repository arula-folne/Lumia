@echo off
setlocal EnableExtensions
REM Build self-contained LumiaMusicView OBS plugin (x64)

set "ROOT=%~dp0"
set "OUTDIR=%ROOT%build"
set "OBS_ROOT=C:\Program Files\obs-studio"
set "OBS_HEADERS=%ROOT%third_party\obs-studio"
set "VENDOR=%ROOT%vendor"
set "CMAKE_BIN=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "PATH=%CMAKE_BIN%;%PATH%"

if not exist "%OBS_HEADERS%\libobs\obs-module.h" (
  echo Missing OBS headers at %OBS_HEADERS%
  exit /b 1
)
if not exist "%VENDOR%\httplib.h" (
  echo Missing vendor\httplib.h
  exit /b 1
)

call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 (
  echo Failed to load VsDevCmd
  exit /b 1
)

mkdir "%OUTDIR%" 2>nul
mkdir "%OUTDIR%\lib" 2>nul

echo Generating import libs from OBS DLLs...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\make-import-lib.ps1" -DllPath "%OBS_ROOT%\bin\64bit\obs.dll" -OutDir "%OUTDIR%\lib" -Name obs
if errorlevel 1 exit /b 1

echo Compiling lumia-music-view.dll...
cl /nologo /LD /O2 /MD /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN ^
  /I"%OBS_HEADERS%\libobs" ^
  /I"%OBS_HEADERS%" ^
  /I"%VENDOR%" ^
  /I"%ROOT%src" ^
  "%ROOT%src\plugin-main.cpp" ^
  "%ROOT%src\lumia_engine.cpp" ^
  "%ROOT%src\lumia_http.cpp" ^
  /Fe"%OUTDIR%\lumia-music-view.dll" ^
  /Fo"%OUTDIR%\\" ^
  /link /DLL /MACHINE:X64 ^
  "%OUTDIR%\lib\obs.lib" ws2_32.lib crypt32.lib propsys.lib ole32.lib shlwapi.lib shell32.lib

if errorlevel 1 (
  echo Build failed
  exit /b 1
)

echo.
echo Built: %OUTDIR%\lumia-music-view.dll
echo Run install-to-obs.bat as Administrator to install.
exit /b 0
