@echo off
setlocal

if not exist "%~dp0build\GameBoyEmulator.exe" (
    echo Run Build.bat first to compile the project.
    pause
    exit /b 1
)

start "" "%~dp0build\GameBoyEmulator.exe"
