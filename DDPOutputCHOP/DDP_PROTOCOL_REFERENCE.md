# DDP Protocol Quick Reference

Official Specification: http://www.3waylabs.com/ddp/

## Header Format (10 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Flags1     |    Flags2     |  Data Type    | Destination ID|
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Data Offset (32-bit)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Data Length (16-bit)  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                        Payload Data                           |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## Byte-by-Byte Breakdown

| Byte | Field | Description | Typical Value |
|------|-------|-------------|---------------|
| 0 | Flags1 | Protocol flags | 0x40 or 0x41 |
| 1 | Flags2 | Sequence number | 0x00-0x0F |
| 2 | Type | Data type | 0x01 (RGB) |
| 3 | ID | Destination ID | 0x01 (Display) |
| 4 | Offset MSB | Data offset byte 0 | 0x00 |
| 5 | Offset | Data offset byte 1 | 0x00 |
| 6 | Offset | Data offset byte 2 | 0x00 |
| 7 | Offset LSB | Data offset byte 3 | 0x00 |
| 8 | Length MSB | Data length byte 0 | 0x05 |
| 9 | Length LSB | Data length byte 1 | 0xA0 |
| 10+ | Data | RGB pixel data | varies |

## Flags1 (Byte 0)

```
Bit:  7   6   5   4   3   2   1   0
     ┌───┬───┬───┬───┬───┬───┬───┬───┐
     │ V │ V │ R │ R │ T │ S │ Q │ P │
     └───┴───┴───┴───┴───┴───┴───┴───┘
      │   │   │   │   │   │   │   └─ PUSH (0x01)
      │   │   │   │   │   │   └───── QUERY (0x02)
      │   │   │   │   │   └─────── STORAGE (0x08)
      │   │   │   │   └─────────── TIME (0x10)
      │   │   └───┴───────────────── Reserved
      └───┴─────────────────────────── VERSION (0xC0)

VER1 = 0x40 (01 in bits 7-6)
```

### Common Flags1 Values

| Value | Binary | Meaning |
|-------|--------|---------|
| 0x40 | 0100 0000 | VER1, no flags |
| 0x41 | 0100 0001 | VER1 + PUSH |
| 0x42 | 0100 0010 | VER1 + QUERY |
| 0x44 | 0100 0100 | VER1 + REPLY |
| 0x48 | 0100 1000 | VER1 + STORAGE |
| 0x50 | 0101 0000 | VER1 + TIME |

## Flags2 (Byte 1)

Lower 4 bits = Sequence number (0-15, wraps)

```
Bit:  7   6   5   4   3   2   1   0
     ┌───┬───┬───┬───┬───┬───┬───┬───┐
     │ R │ R │ R │ R │ S │ S │ S │ S │
     └───┴───┴───┴───┴───┴───┴───┴───┘
      Reserved       Sequence (0-15)
```

## Data Types (Byte 2)

| Value | Type | Bytes/Pixel | Description |
|-------|------|-------------|-------------|
| 0x00 | Undefined | - | Not specified |
| 0x01 | RGB | 3 | Red, Green, Blue |
| 0x02 | HSL | 3 | Hue, Saturation, Lightness |
| 0x03 | RGBW | 4 | Red, Green, Blue, White |
| 0x04 | Grayscale | 1 | Single channel |
| 0xFF | Custom | varies | Vendor-specific |

## Destination IDs (Byte 3)

| Value | Name | Purpose |
|-------|------|---------|
| 0x01 | DISPLAY | Normal pixel data |
| 0xFA (250) | CONFIG | Configuration data (JSON) |
| 0xFB (251) | STATUS | Status/discovery (JSON) |
| 0xFC (252) | DMXDATA | DMX channel data |
| 0xFD (253) | ALLDATA | All data types |

## Data Offset (Bytes 4-7)

32-bit big-endian byte offset into the frame buffer.

```c
offset = (byte[4] << 24) | (byte[5] << 16) | (byte[6] << 8) | byte[7]
```

**Examples:**
- Offset 0: `00 00 00 00` (first pixel)
- Offset 1440: `00 00 05 A0` (481st pixel)
- Offset 3000: `00 00 0B B8` (1001st pixel)

## Data Length (Bytes 8-9)

16-bit big-endian byte count of payload.

```c
length = (byte[8] << 8) | byte[9]
```

**Examples:**
- 3 bytes (1 pixel): `00 03`
- 1440 bytes (480 pixels): `05 A0`
- 300 bytes (100 pixels): `01 2C`

## Packet Size Limits

```
Ethernet MTU:        1500 bytes
IP Header:             20 bytes
UDP Header:             8 bytes
                    ────────────
Available:          1472 bytes
DDP Header:           10 bytes
                    ────────────
Max Payload:        1462 bytes
```

**Official recommendation: 1440 bytes (480 RGB pixels)**

This leaves 22 bytes of headroom for:
- Network variations
- VLAN tags
- PPPoE overhead

## Example Packets

### Single RGB Pixel (Red)

```
Hex: 40 00 01 01 00 00 00 00 00 03 FF 00 00
     ├┘ ├┘ ├┘ ├┘ ├──────┘ ├──┘ ├──────┘
     │  │  │  │  │        │    │
     │  │  │  │  │        │    └─ RGB: 255,0,0
     │  │  │  │  │        └─ Length: 3
     │  │  │  │  └─ Offset: 0
     │  │  │  └─ ID: Display
     │  │  └─ Type: RGB
     │  └─ Seq: 0
     └─ Flags: VER1
```

### Single Pixel with PUSH

```
Hex: 41 00 01 01 00 00 00 00 00 03 00 FF 00
     ├┘ ├┘ ├┘ ├┘ ├──────┘ ├──┘ ├──────┘
     │  │  │  │  │        │    │
     │  │  │  │  │        │    └─ RGB: 0,255,0
     │  │  │  │  │        └─ Length: 3
     │  │  │  │  └─ Offset: 0
     │  │  │  └─ ID: Display
     │  │  └─ Type: RGB
     │  └─ Seq: 0
     └─ Flags: VER1 + PUSH
```

### Multi-Packet Frame (3 pixels)

**Packet 1:**
```
41 00 01 01 00 00 00 00 00 09
FF 00 00  00 FF 00  00 00 FF
```
- Offset: 0
- Length: 9 bytes (3 pixels)
- PUSH flag set (last packet)
- Data: Red, Green, Blue pixels

### Discovery Query

```
Hex: 42 00 00 FB 00 00 00 00 00 00
     ├┘ ├┘ ├┘ ├┘ ├──────┘ ├──┘
     │  │  │  │  │        └─ Length: 0
     │  │  │  │  └─ Offset: 0
     │  │  │  └─ ID: STATUS
     │  │  └─ Type: 0 (not applicable)
     │  └─ Seq: 0
     └─ Flags: VER1 + QUERY
```

**Response (example):**
```
44 00 00 FB 00 00 00 00 00 35
{"status":{"man":"WLED","mod":"ESP32","ver":"0.14"}}
```

### PUSH-Only Packet

```
Hex: 41 00 01 01 00 00 00 00 00 00
     ├┘ ├┘ ├┘ ├┘ ├──────┘ ├──┘
     │  │  │  │  │        └─ Length: 0 (no data)
     │  │  │  │  └─ Offset: 0
     │  │  │  └─ ID: Display
     │  │  └─ Type: RGB
     │  └─ Seq: 0
     └─ Flags: VER1 + PUSH
```

Used to synchronize multiple devices after sending data.

## C Code Examples

### Create DDP Header

```c
void create_ddp_header(uint8_t *packet, 
                       uint32_t offset, 
                       uint16_t length,
                       uint8_t flags,
                       uint8_t seq) {
    packet[0] = DDP_FLAGS1_VER1 | flags;
    packet[1] = seq & 0x0F;
    packet[2] = DDP_DATA_TYPE_RGB;
    packet[3] = DDP_ID_DISPLAY;
    packet[4] = (offset >> 24) & 0xFF;
    packet[5] = (offset >> 16) & 0xFF;
    packet[6] = (offset >> 8) & 0xFF;
    packet[7] = offset & 0xFF;
    packet[8] = (length >> 8) & 0xFF;
    packet[9] = length & 0xFF;
}
```

### Parse DDP Header

```c
void parse_ddp_header(const uint8_t *packet,
                      uint8_t *flags,
                      uint8_t *seq,
                      uint8_t *type,
                      uint8_t *id,
                      uint32_t *offset,
                      uint16_t *length) {
    *flags = packet[0];
    *seq = packet[1] & 0x0F;
    *type = packet[2];
    *id = packet[3];
    *offset = ((uint32_t)packet[4] << 24) |
              ((uint32_t)packet[5] << 16) |
              ((uint32_t)packet[6] << 8) |
              packet[7];
    *length = ((uint16_t)packet[8] << 8) | packet[9];
}
```

### Send RGB Data

```c
// Send 100 RGB pixels
uint8_t pixels[300]; // 100 * 3
// ... fill with RGB data ...

uint8_t packet[1450];
create_ddp_header(packet, 0, 300, DDP_FLAGS1_PUSH, seq++);
memcpy(&packet[10], pixels, 300);

sendto(sock, packet, 310, 0, &addr, sizeof(addr));
```

## Network Configuration

### Default Settings

```
Protocol: UDP
Port: 4048
Broadcast: 255.255.255.255 (for PUSH)
MTU: 1500 bytes (Ethernet)
```

### Multicast (Optional)

```
Group: 239.255.0.1 (example)
TTL: 1 (local network)
```

## Timing and Synchronization

### Single Device

```
Frame N: Send data packets → Send PUSH
Frame N+1: Send data packets → Send PUSH
```

### Multiple Devices

```
Send data to Device A (no PUSH)
Send data to Device B (no PUSH)
Send data to Device C (no PUSH)
Broadcast PUSH → All display simultaneously
```

### Frame Rate Calculation

```
Packet overhead = 10 (DDP) + 8 (UDP) + 20 (IP) + 14 (ETH) = 52 bytes
Data per packet = 1440 bytes
Total per packet = 1492 bytes

For 1000 pixels @ 60 FPS:
  Packets per frame = ceil(3000 / 1440) = 3
  Bytes per frame = 3 × 1492 = 4476 bytes
  Bandwidth = 4476 × 60 = 268,560 bytes/sec ≈ 262 KB/s
```

## Efficiency Comparison

| Protocol | Header | Max Data | Total | Efficiency |
|----------|--------|----------|-------|------------|
| DDP      | 10     | 1440     | 1450  | 99.3%      |
| E1.31    | 126    | 512      | 638   | 80.3%      |
| Art-Net  | 18     | 512      | 530   | 96.6%      |

**Including Ethernet/IP/UDP overhead:**

| Protocol | Total Overhead | Efficiency |
|----------|---------------|------------|
| DDP      | 76 bytes      | 94.9%      |
| E1.31    | 192 bytes     | 72.7%      |
| Art-Net  | 84 bytes      | 85.9%      |

## Common Pitfalls

❌ **Wrong byte order** - Header uses big-endian!  
❌ **Packet too large** - Stay under 1440 data bytes  
❌ **Missing PUSH** - Devices won't display without it  
❌ **Wrong offset** - Must be in bytes, not pixels  
❌ **Sequence overflow** - Mask to 4 bits: `seq & 0x0F`  

## Best Practices

✅ Use 1440 bytes max payload (480 RGB pixels)  
✅ Send PUSH on last packet or separately  
✅ Track sequence numbers per connection  
✅ Enable broadcast on socket for PUSH  
✅ Calculate offset in bytes, not pixels  
✅ Use big-endian for offset and length  

## Wireshark Filter

```
udp.port == 4048
```

## Testing with netcat

```bash
# Send test packet (hex)
echo -n -e '\x41\x00\x01\x01\x00\x00\x00\x00\x00\x03\xFF\x00\x00' | \
  nc -u 192.168.1.100 4048
```

## References

- **Official Spec**: http://www.3waylabs.com/ddp/
- **WLED Implementation**: https://github.com/Aircoookie/WLED
- **TouchDesigner Plugin**: (this project)

---

**Version:** 1.0  
**Date:** 2024  
**Protocol:** DDP v1  



