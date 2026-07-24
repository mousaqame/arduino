@echo off
rem One-click launcher for the robot setup page.
rem Close this window (or Ctrl-C) to stop it.

cd /d "%~dp0"
title Robot Setup

python setup.py %*

echo.
echo Setup helper stopped.
pause
