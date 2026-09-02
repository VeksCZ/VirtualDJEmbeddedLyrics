@echo off
setlocal

rem The single launcher for the complete MP3 & Lyrics Tools GUI.

set "LYRICS_TARGET="
if not "%~1"=="" (
    set "LYRICS_TARGET=%~f1"
)
if not defined LYRICS_TARGET if /I not "%CD%\"=="%~dp0" set "LYRICS_TARGET=%CD%"
set "LYRICS_TAB=%~2"
set "LYRICS_GUI=%~dp0tools\lyrics_tools_gui.py"
set "LYRICS_REQUIREMENTS=%~dp0requirements.txt"
if not exist "%LYRICS_REQUIREMENTS%" set "LYRICS_REQUIREMENTS=%~dp0tools\requirements.txt"

if not exist "%LYRICS_GUI%" (
    echo The complete GUI was not found: "%LYRICS_GUI%"
    echo Extract the complete release ZIP or restore the tools folder.
    pause
    exit /b 1
)

where python >nul 2>nul
if errorlevel 1 (
    echo Python was not found in PATH.
    echo Install Python 3.10 or newer, then install the requirements.
    pause
    exit /b 1
)

call python -c "import tkinter, mutagen, tidalapi" >nul 2>nul
if errorlevel 1 (
    echo Required Python packages are missing.
    if not exist "%LYRICS_REQUIREMENTS%" (
        echo requirements.txt was not found. Extract the complete release ZIP.
        pause
        exit /b 1
    )
    choice /C YN /N /M "Install the required packages for the current Windows user now? [Y/N] "
    if errorlevel 2 exit /b 1
    call python -m pip install --user -r "%LYRICS_REQUIREMENTS%"
    if errorlevel 1 (
        echo Dependency installation failed.
        pause
        exit /b 1
    )
    call python -c "import tkinter, mutagen, tidalapi" >nul 2>nul
    if errorlevel 1 (
        echo Python still cannot import the required packages.
        pause
        exit /b 1
    )
)

if not defined LYRICS_TARGET goto launch_without_target
if defined LYRICS_TAB (
    call python "%LYRICS_GUI%" "%LYRICS_TARGET%" --tab "%LYRICS_TAB%"
) else (
    call python "%LYRICS_GUI%" "%LYRICS_TARGET%"
)
goto launch_finished

:launch_without_target
if defined LYRICS_TAB (
    call python "%LYRICS_GUI%" --tab "%LYRICS_TAB%"
) else (
    call python "%LYRICS_GUI%"
)

:launch_finished
if errorlevel 1 (
    echo.
    echo MP3 ^& Lyrics Tools exited with an error. Review the message above.
    pause
    exit /b 1
)

exit /b 0
