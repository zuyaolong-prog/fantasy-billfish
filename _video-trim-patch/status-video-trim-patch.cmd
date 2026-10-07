@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0status-video-trim-patch.ps1" %*
exit /b %errorlevel%
