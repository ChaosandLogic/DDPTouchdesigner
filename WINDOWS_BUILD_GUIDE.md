# DDP Plugins for Windows - Complete Build Guide

## What You're Building

Two TouchDesigner plugins:
- **DDP Out** - Send pixel data to LED controllers via DDP protocol
- **DDP In** - Receive pixel data from the network (monitor/record DDP streams)

## Requirements

### 1. Visual Studio (Free)
Download **Visual Studio 2022 Community Edition**:
https://visualstudio.microsoft.com/downloads/

During installation, select:
- ✅ **Desktop development with C++**
- ✅ **Windows 10 SDK** (should be included)

### 2. CMake (Optional but Recommended)
Download from: https://cmake.org/download/

Or install with winget:
```cmd
winget install Kitware.CMake
```

### 3. TouchDesigner SDK Headers
Download: https://github.com/TouchDesigner/CustomOperatorSamples

Click "Code" → "Download ZIP" and extract it.

## Quick Build Instructions

### Step 1: Get SDK Headers

1. Extract `CustomOperatorSamples-main.zip`
2. Navigate to: `CustomOperatorSamples-main\CPlusPlus\CHOP\`
3. Copy these 2 files:
   - `CHOP_CPlusPlusBase.h`
   - `CPlusPlus_Common.h`

4. Paste them into **BOTH** plugin directories:
   - `DDPOutputCHOP\`
   - `DDPInputCHOP\`

### Step 2: Build Both Plugins

Open Command Prompt and navigate to your plugin directory, then:

**Build DDP Out:**
```cmd
cd DDPOutputCHOP
build_windows.bat
```

**Build DDP In:**
```cmd
cd ..\DDPInputCHOP
build_windows.bat
```

The batch scripts will:
- Check for SDK headers
- Configure the build
- Compile the plugins
- Install them to TouchDesigner's plugins folder

### Step 3: Verify Installation

The DLLs should be installed to:
```
C:\Users\YourUsername\Documents\Derivative\Plugins\
```

You should see:
- `DDPOutputCHOP.dll`
- `DDPInputCHOP.dll`

### Step 4: Use in TouchDesigner

1. **Restart TouchDesigner**
2. Press **Tab** in the Network Editor
3. Search for:
   - "**DDP Out**" - for sending to LEDs
   - "**DDP In**" - for receiving from network

## Troubleshooting

### ❌ "Visual Studio generator not found"
**Problem:** Visual Studio not installed or CMake can't find it

**Solution:**
1. Install Visual Studio 2022 with C++ tools
2. Restart your terminal
3. Try again

### ❌ "SDK headers not found"
**Problem:** `CHOP_CPlusPlusBase.h` or `CPlusPlus_Common.h` not in plugin directory

**Solution:**
1. Download SDK from: https://github.com/TouchDesigner/CustomOperatorSamples
2. Copy the 2 header files to **each** plugin directory
3. The files must be in the same folder as the .cpp files

### ❌ "ws2_32.lib not found"
**Problem:** Windows Sockets library not linked

**Solution:**
- This should be automatic if Visual Studio is properly installed
- Make sure you installed "Desktop development with C++" workload
- Try repairing Visual Studio installation

### ❌ "Plugin not showing in TouchDesigner"
**Problem:** DLL not loaded or blocked by Windows

**Solutions:**
1. Verify DLL is in: `%USERPROFILE%\Documents\Derivative\Plugins\`
2. **Right-click the DLL** → Properties → **Unblock** (if checkbox exists)
3. **Restart TouchDesigner**
4. Check TouchDesigner version matches plugin path

### ❌ Build errors about C++17
**Problem:** Compiler doesn't support C++17

**Solution:**
- Visual Studio 2019/2022 supports C++17 by default
- Make sure you're not using an older VS version

## Manual Build (Alternative Method)

If the batch script doesn't work, build manually:

### Using CMake Command Line:

```cmd
cd DDPOutputCHOP
mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

copy Release\DDPOutputCHOP.dll "%USERPROFILE%\Documents\Derivative\Plugins\"
```

Repeat for DDPInputCHOP.

### Using Visual Studio IDE:

1. Open Command Prompt in plugin directory
2. Run: `cmake . -G "Visual Studio 17 2022"`
3. Open the generated `.sln` file in Visual Studio
4. Set configuration to **Release**
5. Build → Build Solution (F7)
6. Copy `Release\*.dll` to Plugins folder

## What Each Plugin Does

### DDP Out (DDPOutputCHOP.dll)
- **Sends** RGB pixel data to LED controllers
- **Requires input**: Connect CHOP with RGB data
- **Features**:
  - Gamma correction
  - Brightness control
  - Device discovery
  - FPS limiter
  - Stats tracking

### DDP In (DDPInputCHOP.dll)
- **Receives** DDP packets from network
- **No input needed**: Listens on UDP port
- **Features**:
  - Outputs received pixel data
  - Tracks source IP/port
  - Stats tracking
  - Perfect for monitoring/recording

## Example Usage

### Send to LEDs:
```
[Noise CHOP] → [Shuffle CHOP] → [DDP Out]
                                   ↓
                              (to 192.168.1.100:4048)
```

### Receive/Monitor:
```
Network (port 4048) → [DDP In] → [pixel_data channel]
                        ↓
                   (Info DAT shows source)
```

### Test Loopback:
```
[Noise] → [DDP Out] ← 127.0.0.1:4048
              ↓
         (localhost)
              ↓
         [DDP In] → [pixel_data]
```

## Support

- **TouchDesigner Forum**: https://forum.derivative.ca/
- **DDP Protocol Spec**: http://www.3waylabs.com/ddp/
- **Build Issues**: Check that all prerequisites are installed

## File Structure

After building, your directories should look like:

```
DDPOutputCHOP/
├── DDPOutputCHOP.cpp
├── DDPOutputCHOP.h
├── CHOP_CPlusPlusBase.h      ← SDK header
├── CPlusPlus_Common.h         ← SDK header
├── CMakeLists.txt
├── build_windows.bat
├── BUILD_WINDOWS.md
└── build/
    ├── Release/
    │   └── DDPOutputCHOP.dll  ← Built plugin
    └── ...

DDPInputCHOP/
├── DDPInputCHOP.cpp
├── DDPInputCHOP.h
├── CHOP_CPlusPlusBase.h       ← SDK header
├── CPlusPlus_Common.h          ← SDK header
├── CMakeLists.txt
├── build_windows.bat
├── BUILD_WINDOWS.md
└── build/
    ├── Release/
    │   └── DDPInputCHOP.dll   ← Built plugin
    └── ...
```

## Tips

- Always build in **Release** mode (faster, smaller DLLs)
- Plugins are 64-bit only (TouchDesigner is 64-bit)
- Keep SDK headers updated with your TouchDesigner version
- Check Windows Defender if builds are slow (it might be scanning)

---

**Happy LED programming!** 🎨💡


