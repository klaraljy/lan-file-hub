@echo off
rem lan-file-hub auto upload - drop files into a local folder and they appear on the server.
rem Asks for the local folder and the account once; the token is kept on this machine.
setlocal
title lan-file-hub auto upload
set "LFH_PS=powershell"
where pwsh >nul 2>nul && set "LFH_PS=pwsh"
rem Initialise before "set /p": an empty answer leaves the variable undefined, and the
rem guard below then compares the literal placeholder text instead of an empty string.
set "LFH_FOLDER="
set "LFH_USER="
set "LFH_REMOTE="
set /p LFH_FOLDER=Local folder to watch (example: D:\scan):
set /p LFH_USER=Account on the computer (username or email):
set /p LFH_REMOTE=Folder on the server (empty = root):
%LFH_PS% -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\watch-folder.ps1" -Folder "%LFH_FOLDER%" -Username "%LFH_USER%" -RemoteFolder "%LFH_REMOTE%"
pause
endlocal