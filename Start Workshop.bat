@echo off
rem ============================================================
rem  Double-click this to open the Workshop.
rem
rem  It starts the hub and opens it in your browser. Everything
rem  else is one click from there.
rem
rem  Closing this black window shuts the hub down, along with
rem  anything it started.
rem ============================================================

cd /d "%~dp0hub"
title Workshop - keep this window open

echo Starting the Workshop...
echo.
echo   Your browser opens automatically.
echo   The address for other devices is printed below - use the numbers,
echo   not the computer name (the name resolves to IPv6, which these
echo   servers do not listen on).
echo.
echo   Keep this window open. Closing it stops everything.
echo.

python hub.py --lan

echo.
echo Workshop stopped.
pause
