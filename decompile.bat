@echo off
setlocal

REM Recursively decompile every Buzz machine DLL under %USERPROFILE%\Buzz
REM into C:\Users\nick\source\repos\JeskolaBuzzVst\ref\decompiled using
REM Ghidra's headless analyzer and the HarusHeadless post-script.

if "%GHIDRA_INSTALL_DIR%"=="" (
    echo ERROR: GHIDRA_INSTALL_DIR is not set in this shell.
    echo.
    echo If you just ran "setx GHIDRA_INSTALL_DIR ...", note that setx only
    echo affects NEW shells. Either open a new PowerShell window, or set it
    echo in the current session:
    echo     PowerShell:  $env:GHIDRA_INSTALL_DIR = "C:\path\to\ghidra"
    echo     cmd.exe:     set GHIDRA_INSTALL_DIR=C:\path\to\ghidra
    exit /b 1
)

set "ANALYZE=%GHIDRA_INSTALL_DIR%\support\analyzeHeadless.bat"
if not exist "%ANALYZE%" (
    echo ERROR: analyzeHeadless.bat not found at:
    echo     %ANALYZE%
    exit /b 1
)

set "BUZZ_DIR=%USERPROFILE%\Buzz"
if not exist "%BUZZ_DIR%" (
    echo ERROR: Buzz directory not found: %BUZZ_DIR%
    exit /b 1
)

set "OUT_DIR=C:\Users\nick\source\repos\JeskolaBuzzVst\ref\decompiled"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

REM Ghidra needs a project location + project name. Use a scratch dir under
REM the output so repeated runs are idempotent.
set "PROJECT_DIR=%OUT_DIR%\.ghidra-project"
set "PROJECT_NAME=BuzzMachines"
if not exist "%PROJECT_DIR%" mkdir "%PROJECT_DIR%"

echo ============================================
echo Decompiling Buzz machines
echo   Ghidra:   %GHIDRA_INSTALL_DIR%
echo   Input:    %BUZZ_DIR%
echo   Output:   %OUT_DIR%
echo   Project:  %PROJECT_DIR%\%PROJECT_NAME%
echo ============================================

call "%ANALYZE%" "%PROJECT_DIR%" "%PROJECT_NAME%" ^
    -import "%BUZZ_DIR%" ^
    -recursive ^
    -postScript HarusHeadless.java "%OUT_DIR%" ^
    -overwrite

endlocal
