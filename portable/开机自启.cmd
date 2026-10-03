@echo off
rem lan-file-hub autostart switch - no admin rights needed, fully reversible.
rem No argument = enable. Use "-Disable" or "-Status" to turn it off / inspect it.
setlocal
title lan-file-hub autostart
set "LFH_PS=powershell"
where pwsh >nul 2>nul && set "LFH_PS=pwsh"
if "%~1"=="" (
  %LFH_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\autostart.ps1" -Enable
) else (
  %LFH_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\autostart.ps1" %*
)
pause
endlocal