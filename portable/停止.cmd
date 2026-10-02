@echo off
rem lan-file-hub portable stopper - stops backend + Redis + PostgreSQL (data stays in data\)
chcp 65001 >nul
title lan-file-hub - stop
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\tray.ps1" -Action stop
timeout /t 3 >nul
