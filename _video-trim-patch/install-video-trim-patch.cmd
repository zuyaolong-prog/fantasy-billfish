@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-video-trim-patch.ps1" %*
exit /b %errorlevel%
