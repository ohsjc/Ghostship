@echo off
rem Step 1 (one time): installs Git, Python, CMake and Visual Studio 2022 Build Tools.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup-BuildTools.ps1"
