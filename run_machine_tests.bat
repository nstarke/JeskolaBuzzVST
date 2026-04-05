@echo off
setlocal

set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set BUILD_DIR=build32
set TARGET=BuzzMachineTests
set EXE=%BUILD_DIR%\bin\Release\%TARGET%.exe

echo === Building %TARGET% ===
%CMAKE% --build %BUILD_DIR% --config Release --target %TARGET%
if %ERRORLEVEL% neq 0 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo === Running %TARGET% ===
%EXE% %*
exit /b %ERRORLEVEL%
