@echo off
rem lan-file-hub portable launcher - starts PostgreSQL + Redis + backend, then opens the browser
rem NOTE: do NOT add "chcp 65001" here: with a UTF-8 console ConPTY prints East Asian wide
rem       characters twice (seen as doubled Chinese text), so we keep the system code page.
title lan-file-hub
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\tray.ps1"
