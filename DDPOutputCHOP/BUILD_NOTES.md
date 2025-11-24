# Build Notes - DDP Output CHOP

## Key Implementation Details

### DDP Protocol Corrections

This implementation is based on the **official DDP specification** from [3waylabs.com/ddp](http://www.3waylabs.com/ddp/), with important corrections compared to common implementations:

#### ✅ Correct Implementation (This Plugin)

- **Max payload**: 1440 bytes (480 pixels)
- **Header size**: 10 bytes
- **Efficiency**: 94.9%
- **PUSH flag**: Properly implemented for multi-device sync
- **Packet format**: Exact match to official spec

#### ❌ Common Mistakes in Other Implementations

- Max payload: 1490 bytes (overruns MTU)
- Missing PUSH flag support
- Incorrect sequence number handling
- No multi-device synchronization

### Protocol Efficiency Comparison

From official DDP specification:

| Protocol | Header | Max Data | Efficiency | Pixels@45fps |
|----------|--------|----------|------------|--------------|
| E1.31    | 126 B  | 512 B    | 72.7%      | 67,340       |
| Art-Net  | 18 B   | 512 B    | 85.9%      | 79,542       |
| **DDP**  | **10 B**   | **1440 B**   | **94.9%**      | **87,950**       |

### PUSH Flag Implementation

The PUSH flag is critical for synchronization:

```
Single Device:
  ┌─────────┐
  │ Packet 1│ (no PUSH)
  │ Packet 2│ (no PUSH)  
  │ Packet 3│ (PUSH) ← Device displays entire frame
  └─────────┘

Multi-Device:
  Device A: Packet 1 (no PUSH)
  Device A: Packet 2 (no PUSH)
  Device B: Packet 1 (no PUSH)
  Device B: Packet 2 (no PUSH)
  Device C: Packet 1 (no PUSH)
  Device C: Packet 2 (no PUSH)
  ─────────────────────────
  BROADCAST: PUSH packet ← ALL devices display simultaneously
```

**Auto Push Parameter:**
- `ON` = PUSH on last packet (single device mode)
- `OFF` = No PUSH sent, manual control (multi-device mode)

### Packet Structure

```c
Byte 0:     flags1
            ┌─ VER1 (0x40)
            └─ PUSH (0x01) if last packet

Byte 1:     sequence number (0-15, wraps)

Byte 2:     data_type (0x01 = RGB)

Byte 3:     id (0x01 = DISPLAY)

Bytes 4-7:  offset (big-endian 32-bit)
            Byte offset into frame buffer

Bytes 8-9:  length (big-endian 16-bit)
            Payload length in bytes

Bytes 10+:  RGB data (up to 1440 bytes)
```

### Data Processing Pipeline

```
Input CHOP
    ↓
Layout Detection (Interleaved vs Sequential)
    ↓
Apply Brightness
    ↓
Apply Gamma Correction
    ↓
Convert to 8-bit (0-255)
    ↓
Split into packets (480 pixels max)
    ↓
Create DDP packets with headers
    ↓
Send via UDP
    ↓
Optional: Send PUSH packet
```

### Gamma Correction

Applied per-channel before 8-bit conversion:

```cpp
output = pow(input, 1.0 / gamma)
```

Typical values:
- `1.0` = No correction (linear)
- `2.2` = Standard sRGB (recommended for LEDs)
- `2.8` = Brighter appearance
- `1.8` = Dimmer appearance

### Channel Layout Modes

#### Interleaved (r0,g0,b0,r1,g1,b1...)

```cpp
For 3 pixels = 9 channels:
channels[0] = r0  ┐
channels[1] = g0  ├─ Pixel 0
channels[2] = b0  ┘
channels[3] = r1  ┐
channels[4] = g1  ├─ Pixel 1
channels[5] = b1  ┘
channels[6] = r2  ┐
channels[7] = g2  ├─ Pixel 2
channels[8] = b2  ┘
```

**Use case:** 
- Individual pixel control
- Pixel-based effects
- Each LED has 3 dedicated channels

#### Sequential (separate R,G,B channels)

```cpp
For 3 pixels = 3 channels × 3 samples:
channel[0] = [r0, r1, r2]  ← All reds
channel[1] = [g0, g1, g2]  ← All greens
channel[2] = [b0, b1, b2]  ← All blues
```

**Use case:**
- TOP to CHOP conversions
- Array-based processing
- More memory efficient for large arrays

### Performance Characteristics

#### Network Bandwidth

```
Bandwidth = (header + payload) × packets × FPS

Example: 1000 LEDs @ 60 FPS
  = (10 + 1440) × 3 packets × 60 FPS
  = 1450 × 3 × 60
  = 261,000 bytes/sec
  = 255 KB/s
  = 2 Mbit/s
```

#### Packet Splitting

```cpp
Total bytes = pixels × 3
Packets needed = ceil(Total bytes / 1440)

Examples:
  100 pixels  = 300 bytes  = 1 packet
  480 pixels  = 1440 bytes = 1 packet  ← Max per packet
  500 pixels  = 1500 bytes = 2 packets
  1000 pixels = 3000 bytes = 3 packets
  5000 pixels = 15000 bytes = 11 packets
```

### Socket Configuration

**UDP Socket Settings:**
- Protocol: `SOCK_DGRAM` (UDP)
- Family: `AF_INET` (IPv4)
- Options: `SO_BROADCAST` enabled (for PUSH packets)
- Non-blocking: No (blocking sends)

**Address Configuration:**
```cpp
struct sockaddr_in dest;
dest.sin_family = AF_INET;
dest.sin_port = htons(4048);
inet_pton(AF_INET, ip_address, &dest.sin_addr);
```

### Error Handling

**Socket Errors:**
- `INVALID_SOCKET` / `-1` : Socket creation failed
- `WSANOTINITIALISED` (Windows): WSAStartup not called
- `ECONNREFUSED`: Device not responding
- `ENETUNREACH`: Network unreachable

**DDP Errors:**
- No device response (normal for UDP)
- Packet too large (shouldn't happen with 480 pixel limit)
- Invalid IP format

### Cross-Platform Differences

#### Windows
```cpp
#include <winsock2.h>
SOCKET socket;
WSAStartup() required
closesocket() to close
ws2_32.lib link required
```

#### macOS/Linux
```cpp
#include <sys/socket.h>
int socket;
No WSAStartup
close() to close
No extra libs needed
```

### Memory Management

**Stack Allocations:**
- DDP header: 10 bytes
- Address structures: ~16 bytes

**Heap Allocations:**
- RGB data buffer: `pixels × 3` bytes
- Packet buffer: `1450` bytes (reused)

**Example: 1000 pixels**
- Input: ~3000 bytes (RGB)
- Output: ~4350 bytes (3 packets)
- Peak: ~7500 bytes total

### TouchDesigner Integration

**Cook Every Frame:**
```cpp
ginfo->cookEveryFrameIfAsked = true;
```
Ensures plugin executes each frame for real-time control.

**Output Channels:**
- Always 1 sample per channel
- 4 status channels (enabled, packets, bytes, pixels)
- Float type (converted from int64)

**Parameter Updates:**
- Read every cook
- IP/Port change triggers socket reinit
- Enable toggle controls transmission

### Build Configuration

**C++ Standard:** C++17
- Required for `<algorithm>`, `<vector>`, `<string>`
- Optional: C++20 for better ranges

**Optimization Flags:**
```
Release: -O2/-O3 (or /O2 on MSVC)
Debug: -g/-Od (full debug info)
```

**Platform Detection:**
```cpp
#ifdef _WIN32
  // Windows code
#else
  // macOS/Linux code
#endif
```

### Testing Strategy

1. **Loopback Test**: IP = 127.0.0.1
   - Tests plugin without hardware
   - Verify packet counters increment

2. **Single Device**: Real IP address
   - Test basic functionality
   - Verify LEDs respond

3. **Performance Test**: Large pixel counts
   - 500, 1000, 5000, 10000 pixels
   - Monitor frame rate and latency

4. **Multi-Device**: Multiple controllers
   - Test synchronization
   - Verify PUSH flag behavior

### Future Enhancements

**High Priority:**
- [ ] Device discovery (DDP STATUS query)
- [ ] Broadcast PUSH mode
- [ ] FPS limiting

**Medium Priority:**
- [ ] HSL color space (type 0x02)
- [ ] RGBW support (type 0x03)
- [ ] Multiple destinations

**Low Priority:**
- [ ] Timecode support (FLAG 0x10)
- [ ] Storage mode (FLAG 0x08)
- [ ] Configuration mode (ID 250)

### Known Limitations

- **Single destination**: Only one IP address supported
- **No discovery**: Manual IP configuration required
- **RGB only**: HSL/RGBW not implemented
- **No broadcast PUSH**: Auto-push uses unicast

### Debugging Tips

**Enable verbose logging:**
```cpp
// Add to execute()
myNodeInfo->cookEveryFrameIfAsked = true;
// Log to console
printf("Sent %d packets\n", packetsSent);
```

**Check packet contents:**
```cpp
// Hexdump DDP packet
for (int i = 0; i < packet.size(); i++) {
    printf("%02X ", packet[i]);
}
printf("\n");
```

**Monitor network traffic:**
```bash
# macOS/Linux
sudo tcpdump -i any -n udp port 4048 -X

# Windows (Wireshark)
# Filter: udp.port == 4048
```

### References

- **DDP Spec**: http://www.3waylabs.com/ddp/
- **TouchDesigner SDK**: https://github.com/TouchDesigner/CustomOperatorSamples
- **WLED DDP**: https://github.com/Aircoookie/WLED
- **Network Programming**: Beej's Guide to Network Programming

---

**Version:** 1.0  
**Last Updated:** 2024  
**Protocol:** DDP v1  
**SDK Version:** TouchDesigner 099  

