@echo off
rem Builds build\FileOrganizer.exe with MinGW-w64.
rem Usage:  build.bat [path-to-mingw-bin]      (default: D:\mingw64\bin)
setlocal
set "MINGW_BIN=%~1"
if "%MINGW_BIN%"=="" set "MINGW_BIN=D:\mingw64\bin"
if not exist "%MINGW_BIN%\g++.exe" (
    echo g++.exe not found in "%MINGW_BIN%". Run: build.bat C:\path\to\mingw64\bin
    exit /b 1
)
set "PATH=%MINGW_BIN%;%PATH%"
cd /d "%~dp0"
if not exist build mkdir build

rem Optional: embed the manifest (dark scrollbars, modern controls). The build works without it.
set "RESOBJ="
del /q build\app_res.o 2>nul
pushd resources
windres app.rc -O coff -o ..\build\app_res.o 2>nul
popd
if exist build\app_res.o (set "RESOBJ=build\app_res.o") else (echo Note: windres not available, building without the manifest.)

g++ -std=c++17 -O2 -Wall -Wextra -mwindows -static src\gui_main.cpp %RESOBJ% -o build\FileOrganizer.exe -lcomctl32 -lshell32 -lole32 -luxtheme -ldwmapi -lgdi32
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo Build OK: %~dp0build\FileOrganizer.exe
