@echo off
rem lan-file-hub portable stopper - stops backend + Redis + PostgreSQL (data stays in data\)
rem NOTE: keep the system code page, see the comment in the launcher.
title lan-file-hub (stop)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\tray.ps1" -Action stop
timeout /t 4 >nul
