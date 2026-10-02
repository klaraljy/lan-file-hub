@echo off
rem lan-file-hub portable launcher - starts PostgreSQL + Redis + backend, then opens the browser
chcp 65001 >nul
title lan-file-hub
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\tray.ps1"
