@echo off
REM ===========================================================================
REM  Palette - one-click build (CMake + C++ toolchain).
REM
REM  Requirements for building from source:
REM    * CMake  (on PATH, or set CMAKE=<path\to\cmake.exe>)
REM    * A C++ compiler on PATH: MinGW-w64 (g++.exe) or MSVC (cl.exe)
REM  A git-ignored ".build.local.bat" next to this script may pre-set CMAKE /
REM  CXX for convenience; it is never part of the release.
REM
REM  Keep this file CRLF + ASCII only.
REM ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

if exist "%~dp0.build.local.bat" call "%~dp0.build.local.bat"

if not defined CMAKE (
  where cmake >nul 2>nul && set "CMAKE=cmake"
)
if not defined CMAKE (
  echo [ERROR] CMake was not found.
  echo   Install CMake (https://cmake.org/download) and add it to PATH,
  echo   or set the CMAKE environment variable to its executable.
  exit /b 1
)

where cl.exe >nul 2>nul
if not errorlevel 1 goto :use_msvc
where g++.exe >nul 2>nul
if errorlevel 1 goto :no_toolchain
echo [INFO] Using the MinGW-w64 toolchain from PATH.
"%CMAKE%" -S . -B build -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles"
goto :configured

:use_msvc
echo [INFO] Using the MSVC toolchain from PATH.
"%CMAKE%" -S . -B build -DCMAKE_BUILD_TYPE=Release
goto :configured

:no_toolchain
echo [ERROR] No C++ compiler was found in PATH.
echo   Install MinGW-w64 (g++.exe) or MSVC (cl.exe) and add it to PATH.
exit /b 1

:configured
if errorlevel 1 exit /b 1

"%CMAKE%" --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo Build finished.
echo The executable is located in %CD%\bin\Palette.exe
exit /b 0
