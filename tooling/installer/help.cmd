@echo off
if "%~1"=="" (
    "%~dp0verscript.exe" help
) else (
    "%~dp0verscript.exe" help %*
)
