@echo off
rem One-click launcher for the Workshop hub.
rem Close this window (or Ctrl-C) to stop the hub and any apps it started.

cd /d "%~dp0"
title Workshop Hub

python hub.py %*

echo.
echo Hub stopped.
pause
