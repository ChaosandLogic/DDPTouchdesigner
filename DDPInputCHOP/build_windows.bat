@echo off
echo ==========================================
echo DDP Input CHOP - Windows Build Script
echo Author: Glen Wilde
echo Email: Glen.w.wilde@gmail.com
echo ==========================================
echo.

REM Check if SDK headers exist
if not exist "CHOP_CPlusPlusBase.h" (
    echo ERROR: CHOP_CPlusPlusBase.h not found!
    echo Please copy SDK headers to this directory first.
    echo.
    echo Download from: https://github.com/TouchDesigner/CustomOperatorSamples
    echo Required files: CHOP_CPlusPlusBase.h, CPlusPlus_Common.h
    echo.
    pause
    exit /b 1
)

if not exist "CPlusPlus_Common.h" (
    echo ERROR: CPlusPlus_Common.h not found!
    echo Please copy SDK headers to this directory first.
    echo.
    pause
    exit /b 1
)

echo Creating build directory...
if not exist "build" mkdir build
cd build

echo.
echo Configuring with CMake...
cmake .. -G "Visual Studio 18 2026" -A x64
if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed with VS2026!
    echo.
    echo Trying Visual Studio 2022...
    cmake .. -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 (
        echo.
        echo ERROR: CMake configuration failed with VS2022!
        echo.
        echo Trying Visual Studio 2019...
        cmake .. -G "Visual Studio 16 2019" -A x64
        if errorlevel 1 (
            echo ERROR: CMake configuration failed with all Visual Studio versions!
            echo.
            echo Make sure Visual Studio 2019, 2022, or 2026 is installed with C++ tools.
            echo.
            pause
            exit /b 1
        )
    )
)

echo.
echo Building Release configuration...
cmake --build . --config Release
if errorlevel 1 (
    echo.
    echo ERROR: Build failed!
    echo Check the error messages above.
    echo.
    pause
    exit /b 1
)

echo.
echo Installing to TouchDesigner plugins folder...
cmake --install . --config Release
if errorlevel 1 (
    echo WARNING: Install failed. You may need to manually copy the DLL.
    echo DLL location: build\Release\DDPInputCHOP.dll
    echo Install to: %%USERPROFILE%%\Documents\Derivative\Plugins\
    echo.
    pause
    exit /b 1
)

cd ..

echo.
echo ==========================================
echo BUILD SUCCESSFUL!
echo ==========================================
echo.
echo Plugin installed to:
echo %USERPROFILE%\Documents\Derivative\Plugins\DDPInputCHOP.dll
echo.
echo Next steps:
echo 1. Restart TouchDesigner
echo 2. Look for "DDP In" in the Custom tab
echo.
pause



