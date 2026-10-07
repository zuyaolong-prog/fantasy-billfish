@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0status-layout-dpi-patch.ps1"
echo.
pause
