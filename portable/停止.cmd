@echo off
rem lan-file-hub portable stopper - backend + Redis + PostgreSQL (data stays in data\).
rem Prefers the new PowerShell (pwsh 7) and falls back to Windows PowerShell 5.1.
setlocal
title lan-file-hub (stop)
set "LFH_PS=powershell"
where pwsh >nul 2>nul && set "LFH_PS=pwsh"
%LFH_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\tray.ps1" -Action stop %*
timeout /t 4 >nul
endlocal
