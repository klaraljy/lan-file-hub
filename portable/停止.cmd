@echo off
rem lan-file-hub 便携包：停止（数据都留在 data 目录里，下次启动还在）
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\tray.ps1" -Action stop
timeout /t 2 >nul
