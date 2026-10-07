@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-billfish.ps1"
if errorlevel 1 (
  echo.
  pause
)
