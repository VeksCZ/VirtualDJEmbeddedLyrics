@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall-plugin.ps1" %*
set "UNINSTALL_RESULT=%ERRORLEVEL%"
echo.
if "%UNINSTALL_RESULT%"=="0" (
    echo Uninstall completed successfully.
) else (
    echo Uninstall failed. Review the message above.
)
if not "%LRC_NO_PAUSE%"=="1" pause
exit /b %UNINSTALL_RESULT%
