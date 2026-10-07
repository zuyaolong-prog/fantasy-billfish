@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch-billfish-layout-patch.ps1"
if errorlevel 1 (
  echo.
  pause
)
