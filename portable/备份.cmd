@echo off
rem lan-file-hub portable backup - database + uploaded files, no Docker needed.
rem Prefers the new PowerShell (pwsh 7) and falls back to Windows PowerShell 5.1.
setlocal
title lan-file-hub backup
set "LFH_PS=powershell"
where pwsh >nul 2>nul && set "LFH_PS=pwsh"
%LFH_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\portable-backup.ps1" %*
pause
endlocal