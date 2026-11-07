@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Always rebuild so source changes cannot silently launch stale classes.
call build.bat --no-pause
if errorlevel 1 goto :build_failed

if not exist "out\ticketslots\TicketClientApp.class" goto :main_missing

echo.
echo Starting ticket client...
java -cp out ticketslots.TicketClientApp
set "APP_EXIT=%ERRORLEVEL%"

if "%APP_EXIT%"=="0" exit /b 0
echo.
echo [ERROR] Client exited with code %APP_EXIT%.
pause
exit /b %APP_EXIT%

:build_failed
echo.
echo [ERROR] Client build failed. The application was not started.
pause
exit /b 1

:main_missing
echo.
echo [ERROR] Main class was not generated:
echo out\ticketslots\TicketClientApp.class
pause
exit /b 1
