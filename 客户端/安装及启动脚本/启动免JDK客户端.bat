@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "APP=%~dp0..\..\源代码\release\TicketClient\TicketClient.exe"

if not exist "%APP%" (
    echo.
    echo [ERROR] The self-contained client has not been packaged.
    echo Run:
    echo   ..\..\源代码\package.bat
    pause
    exit /b 1
)

start "" "%APP%"
exit /b 0
