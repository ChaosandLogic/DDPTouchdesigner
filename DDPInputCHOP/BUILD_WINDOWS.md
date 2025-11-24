# Building DDP Plugins for Windows

## Prerequisites

1. **Visual Studio 2019 or 2022** (Community Edition is free)
   - Download from: https://visualstudio.microsoft.com/downloads/
   - During installation, select "Desktop development with C++"

2. **CMake** (optional but recommended)
   - Download from: https://cmake.org/download/
   - Or install via: `winget install Kitware.CMake`

3. **TouchDesigner SDK Headers**
   - Download from: https://github.com/TouchDesigner/CustomOperatorSamples
   - Extract and copy these files to both plugin directories:
     - `CHOP_CPlusPlusBase.h`
     - `CPlusPlus_Common.h`
     - (GL_Extensions.h not needed for CHOPs)

## Quick Build (Using Batch Script)

### Step 1: Copy SDK Headers
```cmd
REM Navigate to where you downloaded CustomOperatorSamples
cd CustomOperatorSamples\CPlusPlus\CHOP

REM Copy headers to DDPOutputCHOP
copy CHOP_CPlusPlusBase.h "C:\Path\To\DDPOutputCHOP\"
copy CPlusPlus_Common.h "C:\Path\To\DDPOutputCHOP\"

REM Copy headers to DDPInputCHOP
copy CHOP_CPlusPlusBase.h "C:\Path\To\DDPInputCHOP\"
copy CPlusPlus_Common.h "C:\Path\To\DDPInputCHOP\"
```

### Step 2: Run Build Script
Double-click `build_windows.bat` in each plugin directory, or run from command prompt:

```cmd
cd DDPOutputCHOP
build_windows.bat

cd ..\DDPInputCHOP
build_windows.bat
```

## Manual Build (Using CMake)

### Build DDP Out

```cmd
cd DDPOutputCHOP
mkdir build
cd build

REM Configure (Visual Studio 2022)
cmake .. -G "Visual Studio 17 2022" -A x64

REM Build Release
cmake --build . --config Release

REM Install (copies to TouchDesigner plugins folder)
cmake --install . --config Release
```

### Build DDP In

```cmd
cd DDPInputCHOP
mkdir build
cd build

REM Configure
cmake .. -G "Visual Studio 17 2022" -A x64

REM Build
cmake --build . --config Release

REM Install
cmake --install . --config Release
```

## Manual Build (Using Visual Studio Directly)

### Method 1: Using CMake-Generated Solution

1. Open Command Prompt or PowerShell
2. Navigate to plugin directory: `cd DDPOutputCHOP`
3. Generate Visual Studio solution:
   ```cmd
   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2022"
   ```
4. Open `DDPOutputCHOP.sln` in Visual Studio
5. Set build configuration to **Release**
6. Build → Build Solution (F7)
7. The DLL will be in `build\Release\DDPOutputCHOP.dll`

### Method 2: Creating VS Project Manually

1. Create new Visual Studio Project: **Dynamic-Link Library (DLL)**
2. Add source files:
   - `DDPOutputCHOP.cpp`
   - `DDPOutputCHOP.h`
   - `CHOP_CPlusPlusBase.h`
   - `CPlusPlus_Common.h`
3. Project Properties:
   - Configuration: **Release**
   - Platform: **x64**
   - C++ → Language → C++ Language Standard: **C++17**
   - Linker → Input → Additional Dependencies: Add `ws2_32.lib`
4. Build Solution
5. Copy `Release\DDPOutputCHOP.dll` to:
   `%USERPROFILE%\Documents\Derivative\Plugins\`

## Installation Location

The plugins will be installed to:
```
%USERPROFILE%\Documents\Derivative\Plugins\
```

Which is typically:
```
C:\Users\YourUsername\Documents\Derivative\Plugins\
```

## Troubleshooting

### "CMake not found"
- Install CMake from https://cmake.org/download/
- Or use Visual Studio's built-in CMake support

### "Visual Studio generator not found"
- Make sure Visual Studio is installed with C++ tools
- Use the correct generator name:
  - VS 2022: `"Visual Studio 17 2022"`
  - VS 2019: `"Visual Studio 16 2019"`

### "ws2_32.lib not found"
- Make sure you installed "Desktop development with C++" workload
- Verify Windows SDK is installed

### "SDK headers not found"
- Make sure you copied the SDK headers to the plugin directories
- Files must be in the same directory as the .cpp files

### "Plugin not showing in TouchDesigner"
- Verify the .dll is in the correct plugins folder
- Restart TouchDesigner
- Check if Windows blocked the DLL (right-click → Properties → Unblock)

## Expected Output

After successful build and install, you should have:
- `DDPOutputCHOP.dll` in the Plugins folder
- `DDPInputCHOP.dll` in the Plugins folder

In TouchDesigner, you'll see:
- **DDP Out** in the Custom tab
- **DDP In** in the Custom tab

## Notes

- Build in **Release** mode for best performance
- The build process creates 64-bit (x64) plugins by default
- TouchDesigner requires 64-bit plugins
- SDK headers must be present in each plugin directory

