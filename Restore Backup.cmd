@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0restore-backup.ps1" %*
set "RESTORE_RESULT=%ERRORLEVEL%"
echo.
if "%RESTORE_RESULT%"=="0" (
    echo Backup restoration completed successfully.
) else (
    echo Backup restoration failed. Review the message above.
)
if not "%LRC_NO_PAUSE%"=="1" pause
exit /b %RESTORE_RESULT%
