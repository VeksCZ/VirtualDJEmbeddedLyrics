@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-plugin.ps1" %*
set "INSTALL_RESULT=%ERRORLEVEL%"
echo.
if "%INSTALL_RESULT%"=="0" (
    echo Installation completed successfully.
) else (
    echo Installation failed. Review the message above.
)
if not "%LRC_NO_PAUSE%"=="1" pause
exit /b %INSTALL_RESULT%
