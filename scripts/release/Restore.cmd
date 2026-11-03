@echo off
rem Requires an explicit install path. Never launches the game.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0lifecycle.ps1" -Action Restore -Destination "%~1" -Owner inno
pause
