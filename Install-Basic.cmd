@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-plugin.ps1" -Edition Basic
if errorlevel 1 pause
