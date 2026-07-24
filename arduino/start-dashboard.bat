@echo off
rem One-click launcher for the parking sensor dashboard.
rem Close this window (or Ctrl-C) to stop the server and release the COM port.

cd /d "%~dp0"
title Parking Sensor Dashboard

python dashboard.py %*

echo.
echo Dashboard stopped.
pause
