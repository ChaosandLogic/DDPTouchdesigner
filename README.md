# DDP TouchDesigner Plugins

Send and receive DDP (Distributed Display Protocol) data in TouchDesigner. Control LED strips, matrices, and controllers like WLED, xLights, Falcon, and PixLite.

## Installation

**Download prebuilt plugins from the [Releases](Release) page.**

- **macOS**: Copy `.plugin` folders to `~/Library/Application Support/Derivative/TouchDesigner099/Plugins/`
- **Windows**: Copy `.dll` files to `%USERPROFILE%/Documents/Derivative/Plugins/`

Restart TouchDesigner after installing.

## Plugins

### DDP Out
Send pixel data to LED controllers.

| Parameter | Description |
|-----------|-------------|
| IP Address | Controller IP |
| Port | DDP port (default: 4048) |
| Enable | Toggle output |
| Gamma | Gamma correction (1.0 = none) |
| Brightness | Master brightness (0-1) |
| Value Range | Input format: 0-1 (default) or 0-255 |
| Auto Push | Sync flag for multi-device setups |

### DDP In
Receive DDP data from other sources.

| Parameter | Description |
|-----------|-------------|
| Listen Port | Port to receive on (default: 4048) |
| Enable | Toggle receiver |
| Value Range | Output format: 0-1 (default) or 0-255 |

## Compatible Controllers

- WLED (ESP32/ESP8266)
- xLights
- Falcon controllers
- PixLite
- FPP (Falcon Player)
- Any DDP v1 compatible device

---

## Building from Source

For developers who want to compile the plugins themselves.

### Requirements

- CMake 3.15+
- C++17 compiler (MSVC 2019+, Clang, or GCC)
- TouchDesigner C++ SDK headers

### Setup

1. **Clone this repository**

2. **Get TouchDesigner SDK headers:**
```bash
git clone https://github.com/TouchDesigner/CustomOperatorSamples.git
```

3. **Copy required headers to each plugin folder:**
```bash
# From CustomOperatorSamples/CPlusPlus/CHOP/, copy these files:
#   - CHOP_CPlusPlusBase.h
#   - CPlusPlus_Common.h  
#   - GL_Extensions.h
# To both DDPOutputCHOP/ and DDPInputCHOP/
```

### Build Commands

**macOS (Universal Binary):**
```bash
cd DDPOutputCHOP
mkdir build && cd build
cmake .. -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
cmake --install .
```

**Windows (MSVC):**
```bash
cd DDPOutputCHOP
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
cmake --install .
```

**Linux:**
```bash
cd DDPOutputCHOP
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
cmake --install .
```

Repeat for `DDPInputCHOP`.

### Install Locations

The `cmake --install` command copies plugins to:
- **macOS**: `~/Library/Application Support/Derivative/TouchDesigner099/Plugins/`
- **Windows**: `%USERPROFILE%/Documents/Derivative/Plugins/`
- **Linux**: `~/.local/share/Derivative/TouchDesigner099/Plugins/`

### Debug Build

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .
```

---

## Resources

- [DDP Protocol Specification](http://www.3waylabs.com/ddp/)
- [TouchDesigner C++ SDK](https://github.com/TouchDesigner/CustomOperatorSamples)
- [WLED Documentation](https://kno.wled.ge/)

## License

MIT License - See LICENSE file

## Author

Glen Wilde 
