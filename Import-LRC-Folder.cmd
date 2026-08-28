@echo off
setlocal
cd /d "%~dp0"

set "TARGET=%~1"
if not defined TARGET (
    echo Enter or paste the music folder path:
    set /p "TARGET=> "
)
if not defined TARGET exit /b 1

echo.
echo === Files that can be imported ===
py tools\lyrics_tag_converter.py "%TARGET%"
if errorlevel 1 goto :failed

echo.
choice /C YN /N /M "Write SYLT and TXXX:SYNCEDLYRICS tags? [Y/N] "
if errorlevel 2 goto :done

choice /C YN /N /M "Delete each LRC after both tags are verified? [Y/N] "
if errorlevel 2 (
    py tools\lyrics_tag_converter.py "%TARGET%" --write
) else (
    py tools\lyrics_tag_converter.py "%TARGET%" --write --delete-lrc
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
