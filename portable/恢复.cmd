@echo off
rem lan-file-hub portable restore - overwrites current data, asks for confirmation.
rem Prefers the new PowerShell (pwsh 7) and falls back to Windows PowerShell 5.1.
setlocal
title lan-file-hub restore
set "LFH_PS=powershell"
where pwsh >nul 2>nul && set "LFH_PS=pwsh"
%LFH_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\portable-restore.ps1" %*
pause
endlocal