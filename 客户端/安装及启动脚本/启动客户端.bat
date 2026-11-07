@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Locate run.bat without embedding a non-ASCII directory name in this
rem batch file. cmd.exe may decode UTF-8 scripts using the system code page.
for /d %%D in ("%~dp0..\*") do (
    if exist "%%~fD\run.bat" (
        call "%%~fD\run.bat"
        exit /b
    )
)

echo [ERROR] Client source directory was not found.
pause
exit /b 1
