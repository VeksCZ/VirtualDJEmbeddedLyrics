@echo off
setlocal
set "LYRICS_LAUNCHER=%~dp0tools\MP3 & Lyrics Tools.cmd"
if not exist "%LYRICS_LAUNCHER%" set "LYRICS_LAUNCHER=%~dp0MP3 & Lyrics Tools.cmd"
if not exist "%LYRICS_LAUNCHER%" (
    echo MP3 ^& Lyrics Tools launcher was not found. Extract the complete release ZIP.
    pause
    exit /b 1
)
set "LYRICS_TARGET=%~1"
if not defined LYRICS_TARGET set "LYRICS_TARGET=%CD%"
call "%LYRICS_LAUNCHER%" "%LYRICS_TARGET%" import
exit /b %ERRORLEVEL%
