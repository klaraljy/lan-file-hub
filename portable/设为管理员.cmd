@echo off
rem lan-file-hub portable: give an existing account administrator rights.
rem No password needed - this only flips the role, the password stays the same.
rem Double-click it and type the username, or run this file with the username as an argument.
title lan-file-hub - make admin
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\make-admin.ps1" -Username %1
echo.
pause
