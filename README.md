# DDP TouchDesigner Plugins

Send and receive DDP (Distributed Display Protocol) data in TouchDesigner. Control LED strips, matrices, and controllers like WLED, xLights, Falcon, and PixLite.

**Tested so far** with an LED wall of about **20,000 pixels** split across **13 controllers**.

## Installation

**Prebuilt plugins live in the [Release](Release) folder** (updated by `build_windows.bat` on Windows).

- **macOS**: Copy `.plugin` folders from `Release/Mac/` to `~/Library/Application Support/Derivative/TouchDesigner099/Plugins/`
- **Windows**: Copy `.dll` files from `Release/` to `%USERPROFILE%/Documents/Derivative/Plugins/`

Restart TouchDesigner after installing. Or run `build_windows.bat` in `DDPOutputCHOP` / `DDPInputCHOP` to build, install to the TouchDesigner plugins folder, and refresh `Release/` in one step.

## Plugins

### DDP Out
Send pixel data to LED controllers.

Expects one CHOP channel of interleaved samples (`r0,g0,b0,r1,g1,b1…`), same idea as DMX Out. Use a Shuffle CHOP if your RGB channels are separate.

| Parameter | Description |
|-----------|-------------|
| IP Address | Controller IP. Used when Routing Table is empty |
| Routing Table | Optional table DAT. Each row sends a pixel slice to an IP |
| Enable | Toggle output |
| Gamma | Gamma correction (1.0 = none) |
| Brightness | Master brightness (0-1) |
| Value Range | Input format: 0-1 (default) or 0-255 |
| Channels Per Pixel | Dropdown: 3 (RGB) or 4 (RGBW). Used for routing and pixel counts |
| Max FPS | Cap send rate (0 = unlimited) |
| Show Stats | Track packets/bytes sent on the node's Info CHOP (off by default) |
| Discover & Update Routing Table | Broadcast a DDP JSON status query (ID 251) on the local subnets, list responders in the Info DAT, then add any found IPs to the assigned Routing Table (or create `<name>_routing` docked to the node if none is assigned). Rows with an empty ip cell are filled first, then new rows are added. IPs already in the table and the rest of your rows are left alone. Does not require Enable. Controllers that only receive DDP pixel data (many WLED builds) will not appear |

**Automatic routing table and Info DAT:** when you place a new DDP Out, it runs discovery in the background and, about a second later, creates `<name>_routing` with any found IPs, assigns it to Routing Table, and creates `<name>_info`, an Info DAT watching the node. Both are docked to the DDP Out. It only does this for a node that looks freshly placed (no Routing Table, no input wired, IP Address still `127.0.0.1`), and only once: a flag in the node's storage stops it running again when the project is reopened. To use single-IP mode instead, delete the table and clear Routing Table; it won't be recreated. A copied/pasted node inherits the flag, so use Discover & Update Routing Table for it (the button doesn't create an Info DAT). The Info DAT lists Last Status, Devices Found, Frames Dropped, and one row per discovered device.

The plugin builds and updates the table itself with a short Python script run through CPython's stable ABI (`python3.dll` on Windows).

Leave Routing Table empty to send the whole input to IP Address. Otherwise use a Table DAT:

```
ip              pixels    start    active
192.168.1.10    300
192.168.1.11    170
192.168.1.12
```

All output goes to the standard DDP port, 4048. A `port` column in older tables is ignored. `pixels` is how many pixels that row takes. Rows pack in order. A `pixels` value of 0 sends nothing to that row. A blank `pixels` cell takes the rest. Each controller gets its slice at DDP offset 0. `start` overrides the sequential index. `active` set to 0 skips the row. With no `pixels` column, every IP receives the full input.

#### Sending behaviour

- Packets go out on a **background thread**, so TouchDesigner's cook rate is not blocked by the network.
- Each controller gets a **tight burst** of its packets, with PUSH on the last data packet. With several controllers, bursts are spaced across most of the frame time so switches and receivers are less likely to drop them.
- DDP sequence numbers are set to **0 (unused)** so receivers like WLED do not apply out-of-sequence filtering that can flash larger displays.
- The socket uses a **large send buffer** so Windows can hold a full frame of UDP without silently dropping packets.
- The Info DAT includes **Frames Dropped**: frames replaced before the sender thread could send them (network or controller not keeping up).

If a WLED controller still flickers on large pixel counts, turn off **Skip out-of-sequence packets** in its Sync settings.

Discovery uses a dedicated socket (so live output is not interrupted), requests JSON status with a non-zero read length, and sends to `127.0.0.1`, `255.255.255.255`, and each local subnet broadcast. Only valid DDP status **replies** are listed. Devices that never implement status (common on WLED) need manual IPs in the routing table. A DDP In on this computer is listed once as `127.0.0.1`; turn on **Discoverable** if you want other machines to see it too.

### DDP In
Receive DDP data from other sources. Always listens on the standard DDP port, 4048, so only one DDP In per machine can receive at a time.

| Parameter | Description |
|-----------|-------------|
| Enable | Toggle receiver |
| Show Stats | Track packets/bytes received in the Info DAT (off by default) |
| Value Range | Output format: 0-1 (default) or 0-255 |
| Channels Per Pixel | Dropdown: 3 (RGB) or 4 (RGBW). Used for `pixel_count`; `pixel_data` is the raw channel bytes either way |
| Discoverable | Answer DDP status queries so DDP Out's discovery lists this machine as `TouchDesigner DDP In`. Off by default for other machines; queries from this computer (`127.0.0.1`) are always answered so a DDP Out in the same project can find it |

Incoming packets are assembled off-screen and shown when the sender's **PUSH** packet arrives, so a frame never appears half old and half new. If no PUSH has arrived for a second (senders that never set the flag), packets are shown as they arrive, once per cook. The Info DAT's **Frames Received** row counts shown frames.

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

Or run `build_windows.bat` from the `DDPOutputCHOP` folder.

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
