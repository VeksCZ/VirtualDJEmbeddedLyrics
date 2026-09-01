@echo off
setlocal
set "LYRICS_LAUNCHER=%~dp0tools\MP3 & Lyrics Tools.cmd"
if not exist "%LYRICS_LAUNCHER%" (
    echo MP3 ^& Lyrics Tools launcher was not found.
    pause
    exit /b 1
)
call "%LYRICS_LAUNCHER%" %*
exit /b %ERRORLEVEL%
