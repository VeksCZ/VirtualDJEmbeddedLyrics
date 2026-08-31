@echo off
setlocal

set "SCRIPT_ROOT=%~dp0"
set "TOOL=%SCRIPT_ROOT%tools\lyrics_tag_converter.py"
if not exist "%TOOL%" set "TOOL=%SCRIPT_ROOT%lyrics_tag_converter.py"

if not exist "%TOOL%" (
    echo Lyrics import tool was not found beside this launcher.
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
echo === Files that can be imported ===
py "%TOOL%" "%TARGET%"
if errorlevel 1 goto :failed

echo.
choice /C YN /N /M "Write LRC to synchronized tags and TXT to lyrics tags? [Y/N] "
if errorlevel 2 goto :done

choice /C YN /N /M "Delete only successfully imported LRC and TXT source files? [Y/N] "
if errorlevel 2 (
    py "%TOOL%" "%TARGET%" --write
) else (
    py "%TOOL%" "%TARGET%" --write --delete-sidecars
)
if errorlevel 1 goto :failed

:done
echo.
echo Done.
pause
exit /b 0

:failed
echo.
echo Import finished with errors. No unverified lyrics source file was deleted.
pause
exit /b 1
