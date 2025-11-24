# Quick Installation Guide
**DDP CHOP - Send & Receive Plugin for TouchDesigner**

## Prerequisites Checklist

- [ ] TouchDesigner installed
- [ ] TouchDesigner SDK downloaded from: https://github.com/TouchDesigner/CustomOperatorSamples
- [ ] Build tools installed (Visual Studio, Xcode, or GCC)
- [ ] CMake 3.10+ installed (optional but recommended)

## Step-by-Step Installation

### 1. Get the SDK Headers (REQUIRED)

```bash
# Clone the TouchDesigner SDK
git clone https://github.com/TouchDesigner/CustomOperatorSamples.git

# Copy the required headers to this directory
cd CustomOperatorSamples/CPlusPlus/CHOP/

# Copy these 3 files to your DDPOutputCHOP folder:
cp CHOP_CPlusPlusBase.h /path/to/DDPOutputCHOP/
cp CPlusPlus_Common.h /path/to/DDPOutputCHOP/
cp GL_Extensions.h /path/to/DDPOutputCHOP/
```

### 2. Build the Plugin

#### macOS (Current System)

```bash
cd "/Users/glen/DDP Touchdesigner /DDPOutputCHOP"

# Create build directory
mkdir build && cd build

# Configure (Universal Binary - Intel + Apple Silicon)
cmake .. -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"

# Build
cmake --build . --config Release

# Install to TouchDesigner plugins folder
cmake --install .
```

**Manual install location:**
```bash
cp DDPOutputCHOP.plugin ~/Library/Application\ Support/Derivative/TouchDesigner099/Plugins/
```

#### Windows

```bash
cd DDPOutputCHOP
mkdir build && cd build

# Configure for Visual Studio
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
cmake --build . --config Release

# Install
cmake --install .
```

**Manual install location:**
```
%USERPROFILE%\Documents\Derivative\Plugins\DDPOutputCHOP.dll
```

#### Linux

```bash
cd DDPOutputCHOP
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build . --config Release

# Install
cmake --install .
```

**Manual install location:**
```
~/Documents/Derivative/Plugins/DDPOutputCHOP.so
```

### 3. Verify Installation

1. **Launch TouchDesigner**
2. **Add a CHOP** - Press `Tab` in network editor
3. **Type "ddp"** - You should see "DDP Output"
4. **If it appears** - Success! ✅

If not:
- Check the install directory
- Look for errors in TouchDesigner's textport
- Try the Debug build for more information

## Troubleshooting

### "SDK headers not found"

You MUST download the real SDK headers. The placeholder files won't work.

**Solution:**
1. Go to https://github.com/TouchDesigner/CustomOperatorSamples
2. Download or clone the repository
3. Copy the 3 header files listed above

### "CMake not found"

**macOS:**
```bash
brew install cmake
```

**Windows:**
Download from: https://cmake.org/download/

**Linux:**
```bash
sudo apt install cmake
```

### "Plugin not loading in TouchDesigner"

**Check install location:**
- macOS: `~/Library/Application Support/Derivative/TouchDesigner099/Plugins/`
- Windows: `Documents\Derivative\Plugins\`
- Linux: `~/Documents/Derivative/Plugins/`

**Check file extension:**
- macOS: `.plugin`
- Windows: `.dll`
- Linux: `.so`

**Check TouchDesigner version:**
- This plugin is for TouchDesigner 099
- Adjust path for different versions

### "Build errors"

**Missing SDK headers:**
- Make sure all 3 SDK files are in the DDPOutputCHOP folder
- Replace the placeholder files

**Compiler not found:**
- macOS: Install Xcode Command Line Tools: `xcode-select --install`
- Windows: Install Visual Studio with C++ Desktop Development
- Linux: Install build-essential: `sudo apt install build-essential`

### "Socket errors at runtime"

**macOS/Linux:**
- May need to allow network access
- Check firewall settings

**Windows:**
- Ensure ws2_32.lib is linked
- Check Windows Firewall

## Quick Test

### Test Send Mode

```
1. Create: Pattern CHOP
   - Channels: 3
   - Samples: 100
   - Pattern: Noise

2. Create: DDP CHOP
   - Mode: Send
   - Connect Pattern CHOP to input
   - Set IP: 127.0.0.1 (loopback for testing)
   - Enable: ON

3. Watch the output channels
   - packets_sent should increment
   - kb_sent should increase
   - If so, plugin is working! ✅
```

### Test Receive Mode

```
1. Create: DDP CHOP (sender)
   - Mode: Send
   - IP: 127.0.0.1
   - Enable: ON
   - Connect some RGB data

2. Create: DDP CHOP (receiver)
   - Mode: Receive
   - Port: 4048
   - Enable: ON

3. Watch the receiver's output
   - packets_received should increment
   - pixel_data channel should show received data
   - Check Info DAT for source IP
```

To test with real LEDs, change IP to your controller's address (e.g., 192.168.1.100).

## Need Help?

- **TouchDesigner Forum**: https://forum.derivative.ca/
- **DDP Protocol**: http://www.3waylabs.com/ddp/
- **SDK Issues**: https://github.com/TouchDesigner/CustomOperatorSamples/issues

## Next Steps

See `README.md` for:
- Complete parameter reference
- Usage examples
- Performance tuning
- Network configuration
- Advanced features

---

**Build Time:** Typically 30-60 seconds  
**Install Time:** Instant  
**Total Setup:** ~5 minutes (after getting SDK)

Happy LED programming! 🎨💡

