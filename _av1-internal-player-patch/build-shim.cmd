@echo off
setlocal
set "PATCH_DIR=%~dp0"
for %%I in ("%PATCH_DIR%..") do set "BILLFISH_ROOT=%%~fI"
if not defined FFMPEG_INCLUDE (
  echo Set FFMPEG_INCLUDE to the FFmpeg 4.2.1 source directory.
  exit /b 2
)
if not defined DAV1D_INCLUDE (
  echo Set DAV1D_INCLUDE to the dav1d 1.5.0 include directory.
  exit /b 2
)
if not exist "%FFMPEG_INCLUDE%\libavcodec\avcodec.h" (
  echo Set FFMPEG_INCLUDE to the FFmpeg 4.2.1 source directory.
  exit /b 2
)
if not exist "%DAV1D_INCLUDE%\dav1d\dav1d.h" (
  echo Set DAV1D_INCLUDE to the dav1d 1.5.0 include directory.
  exit /b 2
)
where cl >nul 2>nul
if errorlevel 1 (
  if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo MSVC x64 developer tools or vswhere are required.
    exit /b 2
  )
  for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_DIR=%%I"
)
if defined VS_DIR call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
set "ORIGINAL_DLL=%BILLFISH_ROOT%\plugins\media_runtime\libFFmpeg64_orig.dll"
if not exist "%ORIGINAL_DLL%" set "ORIGINAL_DLL=%BILLFISH_ROOT%\plugins\media_runtime\libFFmpeg64.dll"
python "%PATCH_DIR%src\generate-forward-exports.py" "%ORIGINAL_DLL%" "%PATCH_DIR%src\forward_exports.h"
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%PATCH_DIR%build" mkdir "%PATCH_DIR%build"
pushd "%PATCH_DIR%build"
cl /nologo /LD /EHsc /std:c++17 /W3 ^
  /I"%PATCH_DIR%src" ^
  /I"%FFMPEG_INCLUDE%" ^
  /I"%DAV1D_INCLUDE%" ^
  /Fe:"%PATCH_DIR%build\libFFmpeg64.dll" ^
  "%PATCH_DIR%src\libFFmpeg64_av1_shim.cpp" ^
  /link /NOLOGO
set "BUILD_EXIT=%ERRORLEVEL%"
popd
exit /b %BUILD_EXIT%
