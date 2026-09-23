@echo off
rem lan-file-hub 便携包：启动
rem 双击后只会出现右下角托盘图标（不弹命令行窗口），需要时右键图标操作。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\tray.ps1"
