@echo off
rem ===========================================================================
rem  wifiForce - one-click build script
rem
rem  The version bumps itself: tools\version.ps1 adds one to the last number in
rem  version.txt, and tools\build-files.ps1 writes that number into the resource
rem  block (file properties), the window title and the executable's name.  Every
rem  copy of the number therefore comes from that one line, and what lands in
rem  dist\ is wifiForce_v<version>.exe.
rem
rem  Usage:
rem    build.bat                   release build, version bumped
rem    build.bat --keep-version    rebuild the current version unchanged
rem    build.bat --version 2.0     set the version outright (nothing is bumped)
rem    build.bat --clean           remove the build tree first
rem ===========================================================================
setlocal EnableExtensions

set "ROOT=%~dp0"
rem %~dp0 always ends with a backslash, and a path ending in one would escape
rem the closing quote of every -I argument it is pasted into.
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\build"
set "DIST_DIR=%ROOT%\dist"
set "VERSION_ARGS="

:parse_arguments
if "%~1"=="" goto arguments_done
if /i "%~1"=="--clean" (
    if exist "%BUILD_DIR%" (
        echo Removing the previous build tree...
        rd /s /q "%BUILD_DIR%"
    )
    shift
    goto parse_arguments
)
if /i "%~1"=="--keep-version" (
    set "VERSION_ARGS=-KeepVersion"
    shift
    goto parse_arguments
)
if /i "%~1"=="--version" (
    if "%~2"=="" (
        echo --version needs a number, for example --version 2.0
        exit /b 2
    )
    set "VERSION_ARGS=-Version %~2"
    shift
    shift
    goto parse_arguments
)
echo Unknown option: %~1
exit /b 2
:arguments_done

rem ---------------------------------------------------------------------------
rem  The version, from the one place that holds it
rem ---------------------------------------------------------------------------
set "VERSION="
for /f "usebackq delims=" %%v in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\version.ps1" %VERSION_ARGS%`) do set "VERSION=%%v"
if not defined VERSION goto version_failed
rem Two numbers, or three for a project that carries a patch number.
echo %VERSION%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*$" >nul
if not errorlevel 1 goto version_ok
echo %VERSION%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul
if errorlevel 1 goto version_failed
:version_ok
set "EXE_NAME=wifiForce_v%VERSION%.exe"
echo Version %VERSION%  (%EXE_NAME%)

rem ---------------------------------------------------------------------------
rem  The files that carry the version, and the manifest fix
rem ---------------------------------------------------------------------------
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\build-files.ps1" -Version %VERSION% -BuildDir "%BUILD_DIR%"
if errorlevel 1 goto failed

rem ---------------------------------------------------------------------------
rem  Find a toolchain
rem ---------------------------------------------------------------------------
rem WinLibs does not always put itself on PATH, so its own bin folder is tried
rem as well before giving up.
set "WIND="
where windres.exe >nul 2>nul && set "WIND=windres.exe"
if not defined WIND if exist "%LOCALAPPDATA%\Programs\winlibs\mingw64\bin\windres.exe" set "WIND=%LOCALAPPDATA%\Programs\winlibs\mingw64\bin\windres.exe"
if not defined WIND if exist "C:\msys64\mingw64\bin\windres.exe" set "WIND=C:\msys64\mingw64\bin\windres.exe"
if not defined WIND goto no_toolchain

set "GXX=g++"
where g++.exe >nul 2>nul && goto compiler_ready
set "GXX=%LOCALAPPDATA%\Programs\winlibs\mingw64\bin\g++.exe"
if not exist "%GXX%" goto no_toolchain
:compiler_ready

rem ---------------------------------------------------------------------------
rem  Compile
rem ---------------------------------------------------------------------------
echo.
echo Building...
"%WIND%" "%ROOT%\src\res.rc" -I "%BUILD_DIR%" -I "%ROOT%\src" -O coff -o "%BUILD_DIR%\res.o"
if errorlevel 1 goto failed

set "SPECS="
if exist "%BUILD_DIR%\no-default-manifest.specs" set "SPECS=-specs=%BUILD_DIR%\no-default-manifest.specs"

"%GXX%" -O2 -std=c++17 -municode -mwindows -static -s -I "%BUILD_DIR%" %SPECS% "%ROOT%\src\main.cpp" "%BUILD_DIR%\res.o" -o "%BUILD_DIR%\%EXE_NAME%" -lwlanapi -lole32 -lwinhttp -lshell32
if errorlevel 1 goto failed

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
copy /Y "%BUILD_DIR%\%EXE_NAME%" "%DIST_DIR%\" >nul
if errorlevel 1 goto failed

echo.
echo ===========================================================================
echo  Build finished: version %VERSION%
echo ===========================================================================
for %%f in ("%DIST_DIR%\%EXE_NAME%") do echo  %%~ff  (%%~zf bytes)
echo  Single executable: the icon, the manifest and the scripts are inside.
echo.
echo  Upload: attach it to the release as wifiForce.exe (no version in the
echo  name, or the update replaces a different file).
echo ===========================================================================
exit /b 0

:version_failed
echo.
echo The version could not be read from version.txt.
echo Run tools\version.ps1 by hand to see what it reports.
exit /b 4

:no_toolchain
echo.
echo No MinGW-w64 toolchain was found (windres and g++).
echo.
echo Install it with:
echo   winget install BrechtSanders.WinLibs.POSIX.UCRT
exit /b 3

:failed
echo.
echo Build failed. See the messages above.
exit /b 1
