@echo off
setlocal
if not exist "%~dp0build\Release\cs2_loader.exe" (
    echo Build the Release configuration first. See README.md for build instructions.
    pause
    exit /b 1
)
start "" "%~dp0build\Release\cs2_loader.exe"
