@echo off
setlocal

set "SCRIPT_ROOT=%~dp0"
set "TOOL=%SCRIPT_ROOT%tools\lyrics_tag_converter.py"
if not exist "%TOOL%" set "TOOL=%SCRIPT_ROOT%lyrics_tag_converter.py"

if not exist "%TOOL%" (
    echo Lyrics marker tool was not found beside this launcher.
    echo Extract the complete release ZIP before running it.
    pause
    exit /b 1
)

where py >nul 2>nul
if errorlevel 1 (
    echo Python 3 was not found.
    echo Install Python from https://www.python.org/downloads/windows/
    echo During setup, enable "Add Python to PATH", then run this tool again.
    pause
    exit /b 1
)

py -c "import mutagen" >nul 2>nul
if errorlevel 1 (
    echo The required Python package Mutagen is not installed.
    choice /C YN /N /M "Install Mutagen for the current Windows user now? [Y/N] "
    if errorlevel 2 exit /b 1
    py -m pip install --user mutagen
    if errorlevel 1 (
        echo Mutagen installation failed.
        pause
        exit /b 1
    )
)

set "TARGET=%~1"
if defined TARGET goto :target_ready
echo Drag a music folder onto this CMD file, or enter its path below.
set /p "TARGET=Music folder [current folder]: "
if not defined TARGET set "TARGET=%CD%"

:target_ready
set "TARGET=%TARGET:"=%"
if not exist "%TARGET%\" (
    echo Folder was not found:
    echo %TARGET%
    pause
    exit /b 1
)

echo.
echo Folder:
echo %TARGET%
echo.
echo === MP3 files containing embedded lyrics ===
py "%TOOL%" "%TARGET%" --mark-existing-lyrics
if errorlevel 1 goto :failed

echo.
choice /C YN /N /M "Write Lyrics: Synced/Unsynced to the Grouping tag? [Y/N] "
if errorlevel 2 goto :done
py "%TOOL%" "%TARGET%" --mark-existing-lyrics --write
if errorlevel 1 goto :failed

:done
echo.
echo Done. In VirtualDJ use Reload Tags, then show the Grouping column.
pause
exit /b 0

:failed
echo.
echo Marking finished with errors.
pause
exit /b 1
