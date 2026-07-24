@echo off
rem One-click launcher for the gas sensor dashboard.
rem Close this window (or Ctrl-C) to stop it and free the COM port.

cd /d "%~dp0"
title Gas Sensor Dashboard

python dashboard.py %*

echo.
echo Dashboard stopped.
pause
