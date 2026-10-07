@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0play-hdr-sdr.ps1" %*
if errorlevel 1 (
  echo.
  pause
)
