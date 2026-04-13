@echo off
setlocal
pushd "%~dp0.."

set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set BUILD_DIR=build32
set TARGET=BuzzMachineTests
set EXE=%BUILD_DIR%\bin\Release\%TARGET%.exe

echo === Building %TARGET% ===
%CMAKE% --build %BUILD_DIR% --config Release --target %TARGET%
if %ERRORLEVEL% neq 0 (
    echo BUILD FAILED
    popd
    exit /b 1
)

echo.
echo === Running %TARGET% ===
set TMP_OUT=%TEMP%\buzz_machine_tests_%RANDOM%.txt
%EXE% %* > "%TMP_OUT%"
set RC=%ERRORLEVEL%
type "%TMP_OUT%"
powershell -NoProfile -Command "$lines = Get-Content -LiteralPath $env:TMP_OUT; $match = $lines | Select-String -Pattern 'Found \d+ machines total' | Select-Object -First 1; if ($match) { $lines[($match.LineNumber - 1)..($lines.Count - 1)] | Set-Content -LiteralPath 'machine-support.txt' }"
del "%TMP_OUT%"
popd
exit /b %RC%
