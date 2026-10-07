@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0restore-video-trim-patch.ps1" %*
exit /b %errorlevel%
