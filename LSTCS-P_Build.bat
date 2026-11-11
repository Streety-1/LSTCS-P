@echo off
title LSTCS-P - Builder
cd /d "%~dp0"
powershell.exe -STA -NoProfile -ExecutionPolicy Bypass -File "%~dp0compat\build.ps1" -Root "%~dp0."
echo.
pause
