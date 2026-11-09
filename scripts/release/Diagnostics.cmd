@echo off
setlocal
rem Default: no save. Asks once (default No) to include your newest save when started without arguments.
rem Switches: -IncludeLatestSave adds the newest save; -IncludeSave picks one save in a file dialog.
set "diagnostics_extra="
set "diagnostics_save="
if "%~1"=="" echo Include my latest save ^(helps reproduce the problem; contains your character and progress^)?
if "%~1"=="" set /p "diagnostics_save=Type Y and press Enter to include it, or just press Enter for no: "
if /i "%diagnostics_save%"=="y" set "diagnostics_extra=-IncludeLatestSave"
if /i "%diagnostics_save%"=="yes" set "diagnostics_extra=-IncludeLatestSave"
powershell -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0diagnostics.ps1" %diagnostics_extra% %*
set "diagnostics_exit=%errorlevel%"
pause
exit /b %diagnostics_exit%
