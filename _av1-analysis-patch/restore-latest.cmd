@echo off
python "%~dp0fix-av1-analysis.py" --restore-latest %*
exit /b %ERRORLEVEL%
