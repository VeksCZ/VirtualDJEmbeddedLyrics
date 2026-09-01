@echo off
setlocal

rem Launch MP3 & Lyrics Tools for a folder supplied by Explorer, drag-and-drop,
rem or the current working directory. Runtime dependencies must be installed with:
rem   python -m pip install -r "<repository>\requirements.txt"

if "%~1"=="" (
    set "LYRICS_TARGET=%CD%"
) else (
    set "LYRICS_TARGET=%~f1"
)
set "LYRICS_TAB=%~2"
set "LYRICS_REQUIREMENTS=%~dp0requirements.txt"
if not exist "%LYRICS_REQUIREMENTS%" set "LYRICS_REQUIREMENTS=%~dp0..\requirements.txt"

where python >nul 2>nul
if errorlevel 1 (
    echo Python was not found in PATH.
    echo Install Python 3.10 or newer, then install the requirements.
    pause
    exit /b 1
)

python -c "import tkinter, mutagen, tidalapi" >nul 2>nul
if errorlevel 1 (
    echo Required Python packages are missing.
    if not exist "%LYRICS_REQUIREMENTS%" (
        echo requirements.txt was not found. Extract the complete release ZIP.
        pause
        exit /b 1
    )
    choice /C YN /N /M "Install the required packages for the current Windows user now? [Y/N] "
    if errorlevel 2 exit /b 1
    python -m pip install --user -r "%LYRICS_REQUIREMENTS%"
    if errorlevel 1 (
        echo Dependency installation failed.
        pause
        exit /b 1
    )
    python -c "import tkinter, mutagen, tidalapi" >nul 2>nul
    if errorlevel 1 (
        echo Python still cannot import the required packages.
        pause
        exit /b 1
    )
)

if defined LYRICS_TAB (
    python "%~dp0lyrics_tools_gui.py" "%LYRICS_TARGET%" --tab "%LYRICS_TAB%"
) else (
    python "%~dp0lyrics_tools_gui.py" "%LYRICS_TARGET%"
)
if errorlevel 1 (
    echo.
    echo MP3 ^& Lyrics Tools exited with an error. Review the message above.
    pause
    exit /b 1
)

exit /b 0
