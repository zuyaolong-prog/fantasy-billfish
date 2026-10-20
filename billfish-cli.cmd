@echo off
if exist "%~dp0tools\python\python.exe" goto bundled
python "%~dp0automation\billfish_cli.py" %*
exit /b %errorlevel%
:bundled
"%~dp0tools\python\python.exe" -I "%~dp0automation\billfish_cli.py" %*
exit /b %errorlevel%
