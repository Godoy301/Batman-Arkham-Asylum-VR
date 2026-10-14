@echo off
REM Batman Asylum VR - launcher. Lives next to ShippingPC-BmGame.exe (the installer puts it there).
REM Needs: Steam running and your PC VR software running (it must be the active OpenXR runtime).
REM AsylumVR\Launch-VR.ps1 asks the headset for its picture size, sets the game up for VR for this one launch
REM and puts your own graphics settings back when the game closes.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0AsylumVR\Launch-VR.ps1"
if errorlevel 1 pause
