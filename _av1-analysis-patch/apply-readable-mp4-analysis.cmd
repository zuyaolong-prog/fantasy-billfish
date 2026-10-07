@echo off
python "%~dp0fix-av1-analysis.py" --scan-mp4 --apply %*
exit /b %ERRORLEVEL%
