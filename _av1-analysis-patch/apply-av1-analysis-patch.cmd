@echo off
python "%~dp0fix-av1-analysis.py" --scan-av1 --apply %*
exit /b %ERRORLEVEL%
