# DDP CHOP for TouchDesigner

A high-performance Custom CHOP plugin for TouchDesigner that **sends and receives** pixel data using the **DDP (Distributed Display Protocol)** for controlling and monitoring LED installations.

## Features

✅ **Bidirectional Operation** - Send or Receive DDP data (selectable mode)  
✅ **Official DDP Protocol Implementation** - Based on [3waylabs.com/ddp](http://www.3waylabs.com/ddp/)  
✅ **Optimized Performance** - 480 pixels per packet (1440 bytes), 94.9% efficiency  
✅ **Multi-Device Synchronization** - Proper PUSH flag implementation for frame-perfect sync  
✅ **Flexible Data Format** - Interleaved RGB channel layout (DMX Out CHOP-compatible)  
✅ **Real-time Processing** - Gamma correction and brightness control (Send mode)  
✅ **Cross-Platform** - Windows, macOS (Intel & Apple Silicon), Linux support  
✅ **Comprehensive Monitoring** - Packet counters, byte statistics, source tracking via Info DAT  
✅ **Device Discovery** - Find DDP-compatible devices on your network (Send mode)  
✅ **FPS Limiter** - Control output frame rate (Send mode)  
✅ **Network Recording** - Capture and visualize DDP streams (Receive mode)  

## Protocol Specifications

- **Port**: 4048 (UDP)
- **Header Size**: 10 bytes
- **Max Payload**: 1440 bytes (480 RGB pixels per packet)
- **Data Type**: RGB (0x01)
- **Efficiency**: 94.9% vs 72.7% (E1.31) and 85.9% (Art-Net)

## Compatible Devices

Works with any DDP-compatible LED controller:
- WLED
- xLights
- Falcon Controllers
- PixLite
- Minleon NDB
- ESPixelStick
- QuinLED

## Prerequisites

### Required Software

- **TouchDesigner** (latest version)
- **TouchDesigner Custom OP SDK** - Download from:
  ```
  https://github.com/TouchDesigner/CustomOperatorSamples
  ```

### Build Tools

**Windows:**
- Visual Studio 2019 or 2022 (with C++ Desktop Development)
- CMake 3.10+ (optional)

**macOS:**
- Xcode Command Line Tools
- CMake 3.10+
  ```bash
  brew install cmake
  ```

**Linux:**
- GCC or Clang
- CMake 3.10+
  ```bash
  sudo apt install build-essential cmake
  ```

## Installation

### Step 1: Get the TouchDesigner SDK

1. Clone or download the SDK:
   ```bash
   git clone https://github.com/TouchDesigner/CustomOperatorSamples.git
   ```

2. Copy these header files to the `DDPOutputCHOP/` directory:
   - `CHOP_CPlusPlusBase.h`
   - `CPlusPlus_Common.h`
   - `GL_Extensions.h`

   Located in: `CustomOperatorSamples/CPlusPlus/CHOP/`

### Step 2: Build the Plugin

#### Option A: Using CMake (Recommended)

```bash
cd DDPOutputCHOP
mkdir build
cd build

# Configure
cmake ..

# Build
cmake --build . --config Release

# Install (copies to TouchDesigner plugins folder)
cmake --install .
```

#### Option B: Visual Studio (Windows)

1. Open Visual Studio
2. Create a new **Dynamic Link Library (DLL)** project
3. Add all `.cpp` and `.h` files
4. Configure project settings:
   - Platform: **x64**
   - Configuration Type: **Dynamic Library (.dll)**
   - C++ Standard: **C++17**
   - Additional Libraries: `ws2_32.lib`
5. Build → Release
6. Copy `.dll` to: `Documents\Derivative\Plugins\`

#### Option C: Xcode (macOS)

1. Create a new **Bundle** project
2. Add source files
3. Build Settings:
   - Architectures: `x86_64` and `arm64`
   - C++ Standard: `C++17`
4. Build
5. Copy `.plugin` to: `~/Library/Application Support/Derivative/TouchDesigner099/Plugins/`

### Step 3: Verify Installation

1. Launch TouchDesigner
2. Create a new CHOP operator
3. Look for **"DDP Output"** in the CHOP list
4. If it appears, installation was successful!

## Usage

### Send Mode (Output to LEDs)

```
1. Add a DDP CHOP to your network
2. Set Mode: Send
3. Configure IP address (e.g., 192.168.1.100)
4. Connect RGB channel data to input (use Shuffle CHOP for interleaved RGB)
   ⚠️ Input is REQUIRED in Send mode
5. Enable output
6. Your LEDs should light up!
```

### Receive Mode (Monitor DDP Traffic)

```
1. Add a DDP CHOP to your network
2. Set Mode: Receive
3. Set Port: 4048 (or your DDP port)
4. Enable
   ⚠️ No input needed - receives data from network
5. The node will output received pixel data in the 'pixel_data' channel
6. View source IP and stats in the Info DAT
```

**Use Cases for Receive Mode:**
- Monitor DDP traffic on your network
- Record DDP streams for playback
- Create DDP bridges/routers
- Visualize incoming LED data
- Debug DDP installations

### Viewing Status (Info DAT)

To view the node's status information, create a Text DAT viewer:

**Method 1: Quick Textport Command**
1. Select your DDP Output CHOP
2. Open Textport (Alt+T or Cmd+T)
3. Paste and run:
```python
selected = ui.panes.current.owner
if selected:
    parent = selected.parent()
    info_name = selected.name + '_info'
    info = parent.create(textDAT, info_name)
    info.par.dat = selected
    info.nodeX = selected.nodeX + 150
    info.nodeY = selected.nodeY
```

**Method 2: Manual Creation**
1. Create a Text DAT next to your DDP Output CHOP
2. In the Text DAT parameters, set `DAT` parameter to your DDP Output CHOP
3. The Info DAT will display all status information

**Info Displayed:**
- **Status**: Connection state (Connected/Error)
- **Pixel Count**: Number of pixels being sent
- **Packets Sent**: Total packets transmitted (when "Show Stats" enabled)
- **KB Sent**: Total kilobytes transmitted (when "Show Stats" enabled)
- **Devices Found**: Number of discovered DDP devices
- **Device IPs**: List of discovered device addresses
- **Last Error**: Most recent error message (if any)

### Parameters

| Parameter | Type | Default | Description | Mode |
|-----------|------|---------|-------------|------|
| **Mode** | Menu | Send | Send or Receive DDP data | Both |
| **IP Address** | String | 127.0.0.1 | Target device IP address | Send only |
| **Port** | Integer | 4048 | UDP port (send destination or receive listen port) | Both |
| **Enable Output** | Toggle | On | Enable/disable operation | Both |
| **Gamma** | Float | 1.0 | Gamma correction (0.1-5.0) | Send only |
| **Brightness** | Float | 1.0 | Global brightness (0.0-1.0) | Send only |
| **Auto Push** | Toggle | On | Auto-send PUSH flag (sync) | Send only |
| **Show Stats** | Toggle | Off | Enable packet/byte counters (resets on toggle) | Both |
| **Max FPS** | Float | 0.0 | Maximum output frame rate (0 = unlimited) | Send only |
| **Discover Devices** | Pulse | - | Scan network for DDP-compatible devices | Send only |

### Output Channels

**Send Mode:**

| Channel | Description |
|---------|-------------|
| `enabled` | 1 if output is enabled, 0 otherwise |
| `packets_sent` | Total packets sent (when Show Stats enabled) |
| `kb_sent` | Total kilobytes transmitted (when Show Stats enabled) |
| `pixel_count` | Number of pixels in last frame |

**Receive Mode:**

| Channel | Description |
|---------|-------------|
| `enabled` | 1 if receiving is enabled, 0 otherwise |
| `packets_received` | Total packets received (when Show Stats enabled) |
| `kb_received` | Total kilobytes received (when Show Stats enabled) |
| `pixel_count` | Number of pixels in received data |
| `pixel_data` | Interleaved RGB pixel values (r0,g0,b0,r1,g1,b1...) |

The `pixel_data` channel contains all received pixel data as normalized float values (0.0-1.0).

### Input Data Format

**Send Mode Only** - The DDP CHOP expects interleaved RGB data in a single channel, similar to how DMX Out CHOP works:

**Format:** One channel with samples arranged as `r0, g0, b0, r1, g1, b1, r2, g2, b2, ...`

**Receive Mode** - No input needed. Data comes from the network and is output in the `pixel_data` channel.

**Example Setup:**
```
[Noise TOP] → [TOP to CHOP] → [Shuffle CHOP] → [DDP Output CHOP]
                               (arrange R,G,B 
                                into 1 channel)
```

**In Shuffle CHOP:**
- Method: "Append CHOPs"
- This combines R, G, B channels into one long channel with samples: r0, g0, b0, r1, g1, b1...

**For 3 pixels, you'll have:**
- 1 channel with 9 samples (3 pixels × 3 colors)
- Sample order: r0, g0, b0, r1, g1, b1, r2, g2, b2

## Examples

### Example 1: Simple LED Strip (100 LEDs)

```
[Pattern CHOP]
  - Channels: 3 (R, G, B)
  - Samples: 100
  - Pattern: Sine wave
  ↓
[DDP Output CHOP]
  - IP: 192.168.1.100
  - Layout: Sequential
  - Enable: On
```

### Example 2: Interleaved Channels

```
[CHOP Execute]
  - Generate: r0, g0, b0, r1, g1, b1, ...
  ↓
[DDP Output CHOP]
  - Layout: Interleaved
  - Gamma: 2.2 (for better LED color)
  - Brightness: 0.8
```

### Example 3: Video to LEDs

```
[Movie File In TOP]
  ↓
[Resize TOP] (to LED count)
  ↓
[TOP to CHOP]
  - RGB Channels
  ↓
[Reorder CHOP] (if needed)
  ↓
[DDP Output CHOP]
  - Layout: Sequential
  - IP: 192.168.1.100
```

### Example 4: Audio Reactive LEDs

```
[Audio File In CHOP]
  ↓
[Audio Spectrum CHOP]
  ↓
[Lookup CHOP] (map to colors)
  ↓
[Math CHOP] (create RGB values)
  ↓
[DDP Output CHOP]
  - Layout: Sequential
```

### Example 5: Multi-Device Synchronized Display

For multiple LED controllers that need to display synchronized frames:

```python
# In a Script CHOP or Python:
# Send data to multiple devices, then broadcast PUSH

# Device 1: 192.168.1.101
# Device 2: 192.168.1.102
# Device 3: 192.168.1.103

# Set Auto Push = OFF on all DDP Output CHOPs
# This buffers data without displaying

# Then use a separate DDP Output CHOP to broadcast PUSH:
# IP: 255.255.255.255 (broadcast)
# Send empty packet with PUSH flag
# All devices display simultaneously!
```

## Network Configuration

### Finding Your LED Controller IP

**WLED:**
1. Connect to WLED WiFi or check your router
2. Default: Check WLED display or WiFi SSID
3. Access web interface: `http://[IP-ADDRESS]`

**General:**
```bash
# Scan network for DDP devices
nmap -p 4048 192.168.1.0/24

# Or use a network scanner app
# Look for devices on port 4048
```

### Network Tips

- Use **wired Ethernet** for best performance
- Avoid WiFi for >500 LEDs (bandwidth + packet loss)
- Use a dedicated network for LED control
- Ensure firewall allows UDP port 4048
- Set static IPs for controllers
- Use a good quality network switch (not a hub)

## Troubleshooting

### LEDs not responding

**Check IP Address:**
```bash
ping 192.168.1.100
# Should get replies
```

**Verify DDP port is open:**
- Check firewall settings
- Ensure UDP port 4048 is not blocked

**Confirm device is DDP-compatible:**
- Check device documentation
- Try WLED web interface first

### Wrong Colors

- Try different **Gamma** values (2.2 is typical)
- Check if device expects **GRB** instead of **RGB**
- Verify input data is properly interleaved (use Shuffle CHOP)
- Check **Brightness** isn't too low

### Choppy Animation

- **Reduce LED count** - Try fewer LEDs first
- **Lower frame rate** - Not all devices handle 60fps
- **Check network bandwidth** - Use wired connection
- **Optimize TOP resolution** - Match LED count exactly

### Plugin Not Loading

**Windows:**
- Ensure `.dll` is in correct folder
- Check Visual Studio Runtime installed
- Try Debug build for more info

**macOS:**
- Check both architectures built (Intel + ARM)
- Verify code signing if needed
- Check Console.app for errors

**Both:**
- Confirm SDK headers are correct version
- Rebuild with proper SDK files
- Check TouchDesigner version compatibility

## Performance

### Benchmarks

| LED Count | Packets/Frame | Bandwidth @60fps | Recommended Network |
|-----------|---------------|------------------|---------------------|
| 100 | 1 | 18 KB/s | WiFi OK |
| 500 | 2 | 90 KB/s | WiFi OK |
| 1000 | 3 | 180 KB/s | WiFi OK |
| 5000 | 11 | 900 KB/s | Ethernet |
| 10000 | 21 | 1.8 MB/s | Ethernet |
| 50000 | 105 | 9 MB/s | Gigabit Ethernet |

### Optimization Tips

1. **Match Resolution** - Don't send more pixels than you have LEDs
2. **Frame Rate** - 30fps is often sufficient, try before going to 60fps
3. **Gamma = 2.2** - Looks better and saves processing
4. **Use Sequential Mode** - Slightly more efficient for large arrays
5. **Disable When Not Needed** - Use Enable toggle to stop sending

## Advanced Features

### DDP Protocol Details

This plugin implements DDP v1 according to the official specification:

**Header Format (10 bytes):**
```
Byte 0:   Flags (0x40 = VER1, 0x01 = PUSH)
Byte 1:   Sequence number (0-15)
Byte 2:   Data type (0x01 = RGB)
Byte 3:   Destination ID (1 = Display)
Bytes 4-7: Offset (big-endian 32-bit)
Bytes 8-9: Length (big-endian 16-bit)
```

**PUSH Flag Behavior:**
- `Auto Push = ON`: Each packet gets PUSH flag (single device)
- `Auto Push = OFF`: No PUSH sent, manual sync needed (multi-device)

### Protocol Advantages Over Art-Net/E1.31

| Feature | DDP | E1.31 | Art-Net |
|---------|-----|-------|---------|
| Efficiency | 94.9% | 72.7% | 85.9% |
| Header Size | 10 bytes | 126 bytes | 18 bytes |
| Max Payload | 1440 bytes | 512 bytes | 512 bytes |
| Pixels/Packet | 480 | 170 | 170 |
| Built-in Sync | ✅ PUSH flag | ❌ | ⚠️ v4 only |
| Universes | Unlimited | 63,999 | 32,768 |

### Future Enhancements

Potential additions for future versions:
- Device discovery (DDP STATUS query)
- Configuration via DDP CONFIG
- HSL color space support
- Multiple output destinations
- Serpentine/zigzag pixel mapping
- FPS limiting
- Broadcast PUSH mode UI

## Technical Reference

### Source Code Structure

```
DDPOutputCHOP/
├── DDPOutputCHOP.h          # Main header with DDP protocol
├── DDPOutputCHOP.cpp        # Implementation
├── CHOP_CPlusPlusBase.h     # TouchDesigner SDK (you provide)
├── CPlusPlus_Common.h       # TouchDesigner SDK (you provide)
├── GL_Extensions.h          # TouchDesigner SDK (you provide)
├── CMakeLists.txt           # Build configuration
└── README.md                # This file
```

### Key Functions

- `initializeSocket()` - Creates UDP socket
- `createDDPPacket()` - Builds DDP packet with header
- `sendDDPData()` - Splits and sends data in packets
- `sendPushPacket()` - Broadcasts sync signal
- `processInterleavedChannels()` - Handles interleaved RGB
- `processSequentialChannels()` - Handles sequential RGB

## Resources

### Official Documentation

- **DDP Protocol Specification**: http://www.3waylabs.com/ddp/
- **TouchDesigner Custom OP SDK**: https://github.com/TouchDesigner/CustomOperatorSamples
- **WLED Documentation**: https://kno.wled.ge/interfaces/udp-realtime/
- **TouchDesigner Forum**: https://forum.derivative.ca/

### Example Projects

Check the repository for example TouchDesigner `.toe` files showing:
- Basic LED strip control
- Matrix displays
- Audio reactive setups
- Multi-device sync

## Contributing

Contributions welcome! Areas for improvement:
- Device discovery implementation
- Additional color spaces (HSL, RGBW)
- Pixel mapping modes
- Performance optimizations

## License

This plugin is released as open source. DDP protocol is an open standard and may be freely used and implemented by anyone.

## Credits

- **DDP Protocol**: 3waylabs (http://www.3waylabs.com/ddp/)
- **TouchDesigner**: Derivative
- **Plugin Development**: Based on TouchDesigner Custom OP SDK

## Support

For issues, questions, or feature requests:
- TouchDesigner Forum: https://forum.derivative.ca/
- DDP Protocol: mark@3waylabs.com

---

**Happy LED programming!** 🎨💡✨

