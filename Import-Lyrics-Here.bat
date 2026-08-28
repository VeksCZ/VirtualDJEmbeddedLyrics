@echo off
call "C:\Tools\VirtualDJEmbeddedLyrics\Import-LRC-Here.cmd" "%~dp0."
set "IMPORT_RESULT=%ERRORLEVEL%"
(goto) 2>nul & del "%~f0"
