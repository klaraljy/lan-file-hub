@echo off
rem lan-file-hub portable launcher - PostgreSQL + Redis + backend, then opens the browser.
rem Prefers the new PowerShell (pwsh 7) and falls back to Windows PowerShell 5.1.
rem NOTE: do NOT add "chcp 65001" here - a UTF-8 console makes ConPTY print East Asian
rem       wide characters twice, so we keep the system code page.
setlocal
title lan-file-hub
set "LFH_PS=powershell"
where pwsh >nul 2>nul && set "LFH_PS=pwsh"
%LFH_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\tray.ps1" %*
endlocal
