@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch-dpi-unaware.ps1"
if errorlevel 1 (
  echo.
  pause
)
