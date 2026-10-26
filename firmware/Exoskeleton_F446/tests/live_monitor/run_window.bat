@echo off
rem One-click live window. Double-click to open, then tick the confirm box
rem and press the start button. Extra arguments are passed through, e.g.
rem   run_window.bat --duration-ms 20000
setlocal
cd /d "%~dp0..\.."
set "PYTHONPATH=%CD%\build\s2-tools\python"
set "TEMP=%CD%\build\agent-tmp\win-tmp"
set "TMP=%TEMP%"
if not exist "%TEMP%" mkdir "%TEMP%"
python tests\live_monitor\s5_live_window.py --port COM10 --duration-ms 40000 %*
endlocal
