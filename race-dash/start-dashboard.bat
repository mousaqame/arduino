@echo off
rem One-click launcher for the race dash website.
rem Close this window (or Ctrl-C) to stop it and free the COM port.
rem
rem Only one program can hold a serial port, so stop any running bridge
rem (sim_bridge.py / forza_bridge.py) before starting this.

cd /d "%~dp0"
title Race Dash

python dashboard.py %*

echo.
echo Dashboard stopped.
pause
