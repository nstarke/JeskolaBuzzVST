@echo off
setlocal

:: Find cmake from VS2022
set CMAKE="%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist %CMAKE% (
    set CMAKE=cmake
)

echo ============================================
echo Building BuzzBridge VST3 Plugin
echo ============================================

:: Step 1: Build 32-bit (plugin + bridge host + tests)
echo.
echo [1/4] Configuring 32-bit build...
if not exist build32 mkdir build32
%CMAKE% -B build32 -G "Visual Studio 17 2022" -A Win32 .
if errorlevel 1 goto :error

echo.
echo [2/4] Building 32-bit targets...
%CMAKE% --build build32 --config Release --target BuzzBridge --target BuzzBridgeHost32 --target BuzzBridgeTests
if errorlevel 1 goto :error

:: Step 2: Run tests
echo.
echo [3/4] Running tests...
build32\bin\Release\BuzzBridgeTests.exe
if errorlevel 1 (
    echo TESTS FAILED
    goto :error
)

:: Step 3: Build 64-bit plugin
echo.
echo [4/4] Building 64-bit plugin...
if not exist build64 mkdir build64
%CMAKE% -B build64 -G "Visual Studio 17 2022" -A x64 .
if errorlevel 1 goto :error

%CMAKE% --build build64 --config Release --target BuzzBridge
if errorlevel 1 goto :error

:: Step 4: Assemble combined bundle
echo.
echo Assembling combined VST3 bundle...
set OUTDIR=dist\BuzzBridge.vst3
if exist dist rmdir /s /q dist
mkdir %OUTDIR%\Contents\x86-win
mkdir %OUTDIR%\Contents\x86_64-win
mkdir %OUTDIR%\Contents\Resources

:: Copy 32-bit plugin
copy build32\VST3\Release\BuzzBridge.vst3\Contents\x86-win\BuzzBridge.vst3 %OUTDIR%\Contents\x86-win\

:: Copy 64-bit plugin
copy build64\VST3\Release\BuzzBridge.vst3\Contents\x86_64-win\BuzzBridge.vst3 %OUTDIR%\Contents\x86_64-win\

:: Copy bridge host next to the 64-bit plugin (it needs to find it)
copy build32\bin\Release\BuzzBridgeHost32.exe %OUTDIR%\Contents\x86_64-win\

:: Copy moduleinfo from 64-bit build
copy build64\VST3\Release\BuzzBridge.vst3\Contents\Resources\moduleinfo.json %OUTDIR%\Contents\Resources\

:: Copy icons
if exist build64\VST3\Release\BuzzBridge.vst3\desktop.ini copy build64\VST3\Release\BuzzBridge.vst3\desktop.ini %OUTDIR%\
if exist build64\VST3\Release\BuzzBridge.vst3\PlugIn.ico copy build64\VST3\Release\BuzzBridge.vst3\PlugIn.ico %OUTDIR%\

:: Step 5: Build installer (if Inno Setup is available)
:: Get version from git tag, fall back to "dev"
set BUZZ_VERSION=dev
for /f "tokens=*" %%g in ('git describe --tags --abbrev^=0 2^>nul') do set BUZZ_VERSION=%%g

set ISCC="%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist %ISCC% (
    echo.
    echo [5/5] Building installer (version: %BUZZ_VERSION%^)...
    %ISCC% /DMyAppVersion=%BUZZ_VERSION% installer\BuzzBridge.iss
    if errorlevel 1 (
        echo Installer build failed, but plugin was built successfully.
    ) else (
        echo.
        echo Installer: dist\BuzzBridge-Setup-%BUZZ_VERSION%.exe
    )
) else (
    echo.
    echo [5/5] Skipping installer (Inno Setup not found^)
    echo   Install with: choco install innosetup
)

echo.
echo ============================================
echo Build complete!
echo.
echo   32-bit plugin: %OUTDIR%\Contents\x86-win\BuzzBridge.vst3
echo   64-bit plugin: %OUTDIR%\Contents\x86_64-win\BuzzBridge.vst3
echo   Bridge host:   %OUTDIR%\Contents\x86_64-win\BuzzBridgeHost32.exe
echo.
echo To install manually, copy dist\BuzzBridge.vst3 to:
echo   C:\Program Files\Common Files\VST3\
echo ============================================
goto :end

:error
echo.
echo BUILD FAILED
exit /b 1

:end
endlocal
