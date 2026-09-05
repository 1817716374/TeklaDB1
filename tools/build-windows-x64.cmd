@echo off
setlocal

set "SOURCE_DIR=%~dp0.."
set "BUILD_DIR=%SOURCE_DIR%\build-vs2022-ninja-x64"
set "ZLIB_ROOT_DIR=%~1"
set "OCCT_ROOT_DIR=%~2"
set "OCCT_OPTIONS=-DTEKLADB1_WITH_OCCT=OFF"
if defined OCCT_ROOT_DIR (
    set "BUILD_DIR=%SOURCE_DIR%\build-vs2022-ninja-x64-occt"
    set "OCCT_OPTIONS=-DTEKLADB1_WITH_OCCT=ON -DTEKLADB1_OCCT_ROOT=%OCCT_ROOT_DIR%"
)
if not defined ZLIB_ROOT_DIR set "ZLIB_ROOT_DIR=%ZLIB_ROOT%"
if not defined ZLIB_ROOT_DIR exit /b 9

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -prerelease -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 10
set "VSDEVCMD=%VSROOT%\Common7\Tools\VsDevCmd.bat"

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul || exit /b 1
if /I not "%PROCESSOR_ARCHITECTURE%"=="AMD64" exit /b 2
for /f "delims=" %%I in ('where cl') do (
    echo %%I | findstr /I /C:"Hostx64\x64\cl.exe" >nul || exit /b 3
    goto compiler_verified
)
exit /b 4

:compiler_verified
if not defined INCLUDE exit /b 5
if not defined LIB exit /b 6

cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DZLIB_ROOT="%ZLIB_ROOT_DIR%" ^
    %OCCT_OPTIONS% ^
    -DTEKLADB1_BUILD_TESTS=ON || exit /b 7
cmake --build "%BUILD_DIR%" --parallel 1 || exit /b 8
