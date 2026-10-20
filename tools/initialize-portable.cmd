@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0initialize-portable.ps1" -Action Initialize
set "initializeExit=%errorlevel%"
echo.
if not "%initializeExit%"=="0" echo Initialization failed. Read the message above before retrying.
pause
exit /b %initializeExit%
