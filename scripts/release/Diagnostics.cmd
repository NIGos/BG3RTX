@echo off
rem Default: no save. Use Diagnostics.cmd -IncludeSave to choose one in a file dialog.
powershell -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0diagnostics.ps1" %*
set "diagnostics_exit=%errorlevel%"
pause
exit /b %diagnostics_exit%
