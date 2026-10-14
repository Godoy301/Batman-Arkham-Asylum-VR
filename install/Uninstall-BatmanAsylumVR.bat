@echo off
REM Batman Asylum VR uninstaller (lives in the game folder after installing).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-BatmanAsylumVR.ps1" %*
pause
if not exist "%~dp0Uninstall-BatmanAsylumVR.ps1" (goto) 2>nul & del "%~f0"
