@echo off
setlocal
title Batman Asylum VR - Joker DLC compatibility installer
cd /d "%~dp0"

if not exist "%~dp0Install-Joker-Compatibility.ps1" (
    echo ERROR: Install-Joker-Compatibility.ps1 is missing.
    echo Please extract the entire ZIP before running this installer.
    pause
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Joker-Compatibility.ps1"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo.
    echo The compatibility installer reported an error. See the message above.
)
pause
exit /b %RC%
