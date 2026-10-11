@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0AsylumVR\Launch-VR.ps1" -SteamVR
if errorlevel 1 pause
