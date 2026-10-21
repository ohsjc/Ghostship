@echo off
rem Step 3: same as step 2, then launches the game.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Ghostship.ps1" -Run %*
pause
