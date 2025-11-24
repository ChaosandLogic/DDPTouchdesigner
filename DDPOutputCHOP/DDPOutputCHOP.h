#ifndef __DDPOutputCHOP__
#define __DDPOutputCHOP__

#include "CHOP_CPlusPlusBase.h"
#include <vector>
#include <string>

using namespace TD;

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

// DDP Protocol Constants (from official spec: http://www.3waylabs.com/ddp/)
#define DDP_PORT 4048
#define DDP_HEADER_SIZE 10
#define DDP_MAX_DATALEN (480 * 3)  // 1440 bytes - official spec recommendation
#define DDP_MAX_PIXELS_PER_PACKET 480

// DDP Flags (Byte 0)
#define DDP_FLAGS1_VER     0xC0  // Version mask
#define DDP_FLAGS1_VER1    0x40  // Version 1
#define DDP_FLAGS1_PUSH    0x01  // Push flag - display data now (sync)
#define DDP_FLAGS1_QUERY   0x02  // Query request
#define DDP_FLAGS1_REPLY   0x04  // Reply to query  
#define DDP_FLAGS1_STORAGE 0x08  // Use local storage
#define DDP_FLAGS1_TIME    0x10  // Timecode field present

// DDP IDs
#define DDP_ID_DISPLAY  1    // Display data
#define DDP_ID_CONFIG   250  // Configuration
#define DDP_ID_STATUS   251  // Status/discovery

// DDP Data Types
#define DDP_DATA_TYPE_RGB  0x01
#define DDP_DATA_TYPE_HSL  0x02
#define DDP_DATA_TYPE_RGBW 0x03

class DDPOutputCHOP : public CHOP_CPlusPlusBase
{
public:
    DDPOutputCHOP(const OP_NodeInfo* info);
    virtual ~DDPOutputCHOP();

    virtual void getGeneralInfo(CHOP_GeneralInfo*, const OP_Inputs*, void*) override;
    virtual bool getOutputInfo(CHOP_OutputInfo*, const OP_Inputs*, void*) override;
    virtual void getChannelName(int32_t index, OP_String *name, const OP_Inputs*, void* reserved1) override;
    
    virtual void execute(CHOP_Output*, const OP_Inputs*, void*) override;
    
    virtual int32_t getNumInfoCHOPChans(void* reserved1) override;
    virtual void getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan, void* reserved1) override;
    
    virtual bool getInfoDATSize(OP_InfoDATSize* infoSize, void* reserved1) override;
    virtual void getInfoDATEntries(int32_t index, int32_t nEntries, OP_InfoDATEntries* entries, void* reserved1) override;
    
    virtual void setupParameters(OP_ParameterManager* manager, void* reserved1) override;
    virtual void pulsePressed(const char* name, void* reserved1) override;

private:
    // Socket management
    void initializeSocket();
    void closeSocket();
    
    // DDP packet creation and sending
    void createDDPPacket(const uint8_t* pixelData, size_t dataLength, 
                         size_t offset, bool pushFlag, std::vector<uint8_t>& packet);
    void sendDDPData(const std::vector<uint8_t>& pixelData);
    void sendPushPacket();
    
    // Data processing
    void processInterleavedChannels(const OP_CHOPInput* chopInput, 
                                     float gamma, float brightness,
                                     std::vector<uint8_t>& rgbData);
    void processSequentialChannels(const OP_CHOPInput* chopInput,
                                    float gamma, float brightness,
                                    std::vector<uint8_t>& rgbData);
    
    // Helper functions
    uint8_t floatToUint8(float value);
    float applyGamma(float value, float gamma);
    
    // Socket members
    #ifdef _WIN32
        SOCKET m_socket;
        bool m_wsaInitialized;
    #else
        int m_socket;
    #endif
    
    struct sockaddr_in m_destAddr;
    struct sockaddr_in m_broadcastAddr;
    bool m_socketInitialized;
    bool m_needsReinitialize;
    bool m_showStats;
    
    // DDP state
    uint8_t m_sequenceNumber;
    int64_t m_packetsSent;
    int64_t m_bytesSent;
    int32_t m_lastPixelCount;
    std::string m_lastError;
    std::string m_lastIPAddress;
    int m_lastPort;
    
    // Device discovery
    std::vector<std::string> m_discoveredDevices;
    bool m_isDiscovering;
    void discoverDevices();
    void sendQueryPacket();
    
    // FPS limiting
    double m_lastFrameTime;
    bool shouldSendFrame(double targetFPS);
    
    const OP_NodeInfo* myNodeInfo;
};

#endif


