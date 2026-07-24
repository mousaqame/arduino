@echo off
cd /d "%~dp0"
title Knob and Servo
python dashboard.py %*
echo.
pause
