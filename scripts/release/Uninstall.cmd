@echo off
rem BG3RTX: remove this install (uninstall.ps1 -KeepSettings keeps bg3rtx_user.ini).
rem One parenthesised block (parsed before it runs) and a temporary copy of the script, because
rem the uninstaller deletes this folder, this file included.
(
  cd /d "%TEMP%"
  copy /y "%~dp0uninstall.ps1" "%TEMP%\bg3rtx_uninstall.ps1" >nul
  powershell -NoProfile -ExecutionPolicy Bypass -File "%TEMP%\bg3rtx_uninstall.ps1" -InstallDir "%~dp0." %*
  del "%TEMP%\bg3rtx_uninstall.ps1" >nul 2>&1
  pause
)
