#ifndef DDP_PROTOCOL_H
#define DDP_PROTOCOL_H

// DDP v1 constants (http://www.3waylabs.com/ddp/). Shared by DDP Out and DDP In.

#define DDP_PORT 4048
#define DDP_HEADER_SIZE 10
#define DDP_HEADER_SIZE_TIMECODE 14
#define DDP_MAX_DATALEN (480 * 3)  // 1440 bytes
#define DDP_MAX_PIXELS_PER_PACKET 480

#define DDP_FLAGS1_VER     0xC0  // Version mask
#define DDP_FLAGS1_VER1    0x40  // Version 1
#define DDP_FLAGS1_PUSH    0x01  // Display the frame now
#define DDP_FLAGS1_QUERY   0x02  // Query request
#define DDP_FLAGS1_REPLY   0x04  // Reply to query
#define DDP_FLAGS1_STORAGE 0x08  // Use local storage
#define DDP_FLAGS1_TIME    0x10  // Timecode field present

#define DDP_ID_DISPLAY  1
#define DDP_ID_CONFIG   250
#define DDP_ID_STATUS   251

// Undefined/generic data type: the controller interprets the channel stream.
#define DDP_DATA_TYPE_UNDEFINED 0x00

#endif
