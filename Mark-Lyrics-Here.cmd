@echo off
setlocal

set "TARGET=%~1"
if not defined TARGET set "TARGET=%CD%"
set "TOOL=C:\Tools\VirtualDJEmbeddedLyrics\tools\lyrics_tag_converter.py"

if not exist "%TOOL%" (
    echo Lyrics marker tool was not found:
    echo %TOOL%
    pause
    exit /b 1
)

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
