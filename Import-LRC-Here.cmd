@echo off
setlocal

set "TARGET=%~dp0."
set "TOOL=C:\Tools\VirtualDJEmbeddedLyrics\tools\lyrics_tag_converter.py"

if not exist "%TOOL%" (
    echo Import tool was not found:
    echo %TOOL%
    echo.
    echo Keep the project in C:\Tools\VirtualDJEmbeddedLyrics
    pause
    exit /b 1
)

echo Folder:
echo %TARGET%
echo.
echo === Files that can be imported ===
py "%TOOL%" "%TARGET%"
if errorlevel 1 goto :failed

echo.
choice /C YN /N /M "Write LRC to synced tags and TXT to unsynced tags? [Y/N] "
if errorlevel 2 goto :done

choice /C YN /N /M "Delete successfully imported LRC and TXT source files? [Y/N] "
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
echo Import finished with errors. No unverified LRC file was deleted.
pause
exit /b 1
