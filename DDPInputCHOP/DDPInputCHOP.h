#ifndef __DDPInputCHOP__
#define __DDPInputCHOP__

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

// DDP Protocol Constants
#define DDP_PORT 4048
#define DDP_HEADER_SIZE 10
#define DDP_MAX_DATALEN (480 * 3)

// DDP Flags
#define DDP_FLAGS1_VER     0xC0
#define DDP_FLAGS1_VER1    0x40
#define DDP_FLAGS1_PUSH    0x01

// DDP IDs
#define DDP_ID_DISPLAY  1

// DDP Data Types
#define DDP_DATA_TYPE_RGB  0x01

class DDPInputCHOP : public CHOP_CPlusPlusBase
{
public:
    DDPInputCHOP(const OP_NodeInfo* info);
    virtual ~DDPInputCHOP();

    virtual void getGeneralInfo(CHOP_GeneralInfo*, const OP_Inputs*, void*) override;
    virtual bool getOutputInfo(CHOP_OutputInfo*, const OP_Inputs*, void*) override;
    virtual void getChannelName(int32_t index, OP_String *name, const OP_Inputs*, void* reserved1) override;
    
    virtual void execute(CHOP_Output*, const OP_Inputs*, void*) override;
    
    virtual int32_t getNumInfoCHOPChans(void* reserved1) override;
    virtual void getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan, void* reserved1) override;
    
    virtual bool getInfoDATSize(OP_InfoDATSize* infoSize, void* reserved1) override;
    virtual void getInfoDATEntries(int32_t index, int32_t nEntries, OP_InfoDATEntries* entries, void* reserved1) override;
    
    virtual void setupParameters(OP_ParameterManager* manager, void* reserved1) override;

private:
    // Socket management
    void initializeSocket();
    void closeSocket();
    
    // Receive and parse
    void receiveData();
    bool parseDDPPacket(const uint8_t* buffer, size_t length, 
                        uint32_t& offset, uint16_t& dataLen, 
                        const uint8_t*& pixelData);
    
    // Socket members
    #ifdef _WIN32
        SOCKET m_socket;
        bool m_wsaInitialized;
    #else
        int m_socket;
    #endif
    
    bool m_socketInitialized;
    int m_lastPort;
    
    // Received data
    std::vector<uint8_t> m_receivedPixelData;
    int32_t m_receivedPixelCount;
    int64_t m_packetsReceived;
    int64_t m_bytesReceived;
    bool m_showStats;
    
    // Source tracking
    std::string m_lastSourceIP;
    uint16_t m_lastSourcePort;
    
    // Error tracking
    std::string m_lastError;
    
    const OP_NodeInfo* myNodeInfo;
};

#endif



