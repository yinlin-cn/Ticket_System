@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "MAIN_SOURCE=src\ticketslots\TicketClientApp.java"
set "OUT_DIR=out"

where java >nul 2>nul
if errorlevel 1 goto :missing_java

where javac >nul 2>nul
if errorlevel 1 goto :missing_javac

echo [1/3] Java runtime:
java -version
echo [2/3] Java compiler:
javac -version

if not exist "%MAIN_SOURCE%" goto :missing_source

echo [3/3] Compiling sources...
if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
if errorlevel 1 goto :clean_failed
mkdir "%OUT_DIR%"
if errorlevel 1 goto :clean_failed

rem Enumerate the four package roots explicitly. This avoids sources.txt encoding
rem issues on Windows systems whose default code page is not UTF-8.
javac --release 17 -encoding UTF-8 -d "%OUT_DIR%" ^
    src\ticketslots\*.java ^
    src\ticketslots\tasks\*.java ^
    src\ticketslots\ui\*.java ^
    src\ticketslots\model\*.java
if errorlevel 1 goto :build_failed

echo.
echo Build complete. Launch with run.bat.
exit /b 0

:missing_java
echo.
echo [ERROR] java was not found.
echo Install JDK 17 or newer and add its bin directory to PATH.
goto :pause_and_fail

:missing_javac
echo.
echo [ERROR] javac was not found. A JRE alone cannot build this client.
echo Install JDK 17 or newer and add its bin directory to PATH.
goto :pause_and_fail

:missing_source
echo.
echo [ERROR] Main source was not found:
echo %MAIN_SOURCE%
goto :pause_and_fail

:clean_failed
echo.
echo [ERROR] Could not recreate the output directory:
echo %OUT_DIR%
echo Close any program that is using it and try again.
goto :pause_and_fail

:build_failed
echo.
echo [ERROR] Compilation failed. Review the javac diagnostics above.
goto :pause_and_fail

:pause_and_fail
if /i not "%~1"=="--no-pause" pause
exit /b 1
