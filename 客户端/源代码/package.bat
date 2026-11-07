@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "OUT_DIR=out"
set "STAGE_DIR=build\package-input"
set "DIST_DIR=release"
set "APP_NAME=TicketClient"

where java >nul 2>nul
if errorlevel 1 goto :missing_java

for /f "tokens=2 delims==" %%I in ('java -XshowSettings:properties -version 2^>^&1 ^| findstr /c:"java.home ="') do set "JAVA_HOME=%%I"
set "JAVA_HOME=%JAVA_HOME:~1%"

if not exist "%JAVA_HOME%\bin\javac.exe" goto :missing_jdk
if not exist "%JAVA_HOME%\bin\jar.exe" goto :missing_jdk
if not exist "%JAVA_HOME%\bin\jpackage.exe" goto :missing_jdk

echo [1/4] Compiling client...
call build.bat --no-pause
if errorlevel 1 goto :build_failed

echo [2/4] Creating application JAR...
if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
mkdir "%STAGE_DIR%"
if errorlevel 1 goto :package_failed

"%JAVA_HOME%\bin\jar.exe" --create ^
    --file "%STAGE_DIR%\ticket-client.jar" ^
    --main-class ticketslots.TicketClientApp ^
    -C "%OUT_DIR%" .
if errorlevel 1 goto :package_failed

echo [3/4] Building self-contained Windows application...
if exist "%DIST_DIR%\%APP_NAME%" rmdir /s /q "%DIST_DIR%\%APP_NAME%"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

"%JAVA_HOME%\bin\jpackage.exe" ^
    --type app-image ^
    --dest "%DIST_DIR%" ^
    --name "%APP_NAME%" ^
    --input "%STAGE_DIR%" ^
    --main-jar ticket-client.jar ^
    --main-class ticketslots.TicketClientApp ^
    --add-modules java.base,java.desktop ^
    --java-options "-Dfile.encoding=UTF-8" ^
    --jlink-options "--strip-debug --no-header-files --no-man-pages"
if errorlevel 1 goto :package_failed

echo [4/4] Packaging ZIP archive...
if exist "%DIST_DIR%\%APP_NAME%-Windows-x64.zip" del /q "%DIST_DIR%\%APP_NAME%-Windows-x64.zip"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop'; Compress-Archive -LiteralPath '%DIST_DIR%\%APP_NAME%' -DestinationPath '%DIST_DIR%\%APP_NAME%-Windows-x64.zip' -CompressionLevel Optimal"
if errorlevel 1 goto :package_failed

echo.
echo Self-contained package created:
echo   %CD%\%DIST_DIR%\%APP_NAME%\%APP_NAME%.exe
echo   %CD%\%DIST_DIR%\%APP_NAME%-Windows-x64.zip
exit /b 0

:missing_java
echo.
echo [ERROR] java was not found. Run this script on the build machine with JDK 17 or newer.
goto :fail

:missing_jdk
echo.
echo [ERROR] A full JDK is required to create the self-contained package.
echo Missing javac, jar or jpackage under:
echo %JAVA_HOME%\bin
goto :fail

:build_failed
echo.
echo [ERROR] Client compilation failed.
goto :fail

:package_failed
echo.
echo [ERROR] Client packaging failed. Review the diagnostics above.
goto :fail

:fail
if /i not "%~1"=="--no-pause" pause
exit /b 1
