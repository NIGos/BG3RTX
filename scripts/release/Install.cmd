@echo off
rem BG3RTX: install to %LOCALAPPDATA%\Programs\BG3RTX (install.ps1 -Destination <folder> for another one).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
pause
