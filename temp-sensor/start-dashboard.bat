@echo off
rem One-click launcher for the thermometer dashboard.
rem Close this window (or Ctrl-C) to stop it and free the COM port.

cd /d "%~dp0"
title Thermometer Dashboard

python dashboard.py %*

echo.
echo Dashboard stopped.
pause
