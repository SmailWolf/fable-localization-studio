@echo off
setlocal
cd /d "%~dp0"
set "PATH=%~dp0bin;%PATH%"
start "" "%~dp0bin\fable-localization-studio.exe" %*
