@echo off
REM Batman Asylum VR installer: runs Install-BatmanAsylumVR.ps1 from this folder.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-BatmanAsylumVR.ps1" %*
pause
