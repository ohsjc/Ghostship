@echo off
rem Step 2: builds Ghostship.exe and installs it into the game folder. Extra flags are passed through.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Ghostship.ps1" %*
pause
