# DDP TouchDesigner Plugins

Custom CHOP plugins for sending and receiving DDP (Distributed Display Protocol) data in TouchDesigner.

## 📦 What's Inside

### DDPOutputCHOP
Send RGB pixel data from TouchDesigner to LED controllers via DDP protocol.

**Features:**
- Full DDP v1 protocol implementation
- Support for 1000s of LEDs at 60fps
- Gamma correction and brightness control
- Multi-device synchronization via PUSH flag
- Compatible with WLED, xLights, Falcon controllers, PixLite, and more

**[📖 Full Documentation →](DDPOutputCHOP/README.md)**

### DDPInputCHOP
Receive DDP protocol data into TouchDesigner for visualization and processing.

**Features:**
- Real-time DDP packet reception
- Automatic pixel data parsing
- Network monitoring and statistics
- Multi-source support

**[📖 Documentation →](DDPInputCHOP/)**

## 🚀 Quick Start

### Prerequisites
- TouchDesigner (any recent version)
- CMake 3.15 or higher
- C++ compiler (MSVC on Windows, Clang on macOS, GCC on Linux)
- TouchDesigner C++ SDK headers

### Build Steps

1. **Get TouchDesigner SDK headers:**
```bash
git clone https://github.com/TouchDesigner/CustomOperatorSamples.git
cd CustomOperatorSamples/CPlusPlus/CHOP/
cp CHOP_CPlusPlusBase.h CPlusPlus_Common.h GL_Extensions.h "DDP Touchdesigner /DDPOutputCHOP/"
cp CHOP_CPlusPlusBase.h CPlusPlus_Common.h GL_Extensions.h "DDP Touchdesigner /DDPInputCHOP/"
```

2. **Build DDPOutputCHOP:**
```bash
cd "DDP Touchdesigner /DDPOutputCHOP"
mkdir build && cd build
cmake .. -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"  # macOS only
cmake --build . --config Release
cmake --install .
```

3. **Build DDPInputCHOP:**
```bash
cd "DDP Touchdesigner /DDPInputCHOP"
mkdir build && cd build
cmake .. -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"  # macOS only
cmake --build . --config Release
cmake --install .
```

4. **Launch TouchDesigner** - plugins will appear in the CHOP operator menu

## 📚 Resources

- **[DDP Protocol Spec](http://www.3waylabs.com/ddp/)** - Official protocol documentation
- **[TouchDesigner SDK](https://github.com/TouchDesigner/CustomOperatorSamples)** - SDK and examples
- **[WLED Project](https://kno.wled.ge/)** - Popular ESP32 LED controller with DDP support

## 🎯 Use Cases

- **Live Shows**: Control stage lighting from TouchDesigner
- **Art Installations**: Drive LED sculptures and installations
- **VJing**: Real-time video-to-LED mapping
- **Architectural Lighting**: Building facade control
- **Interactive Exhibits**: Sensor-driven lighting responses

## 🛠️ Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| macOS    | ✅ Tested | Universal binary (Intel + Apple Silicon) |
| Windows  | ✅ Ready | MSVC 2019+ required |
| Linux    | ✅ Ready | GCC/Clang supported |

## 📖 Documentation Structure

```
.
├── README.md                           # This file
├── DDPOutputCHOP/
│   ├── README.md                       # Complete user guide
│   ├── INSTALL.md                      # Quick installation
│   ├── BUILD_NOTES.md                  # Technical details
│   ├── PROJECT_SUMMARY.md              # Development overview
│   ├── DDP_PROTOCOL_REFERENCE.md       # Protocol specification
│   └── [source files]
├── DDPInputCHOP/
│   ├── BUILD_WINDOWS.md                # Windows build guide
│   └── [source files]
└── WINDOWS_BUILD_GUIDE.md              # General Windows setup
```

## 🔧 Development

### Project Structure
- `DDPOutputCHOP/` - Send DDP data (most feature-complete)
- `DDPInputCHOP/` - Receive DDP data
- Each plugin is independent with its own CMakeLists.txt
- Build artifacts are excluded via .gitignore

### Building for Development
```bash
# Clean build
rm -rf build
mkdir build && cd build

# Build with debug symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .

# Run tests (if implemented)
ctest
```

## 🤝 Contributing

Contributions welcome! Areas for improvement:
- Multiple output destinations (see Table DAT approach)
- Device discovery UI
- HSL and RGBW color space support
- Pixel mapping (serpentine, zigzag)
- Performance optimizations

## 📄 License

[Add your license here]

## ✨ Acknowledgments

- **DDP Protocol** by 3waylabs.com
- **TouchDesigner** by Derivative
- **WLED** community for testing and feedback

---

**Built with ❤️ for the LED art community**

