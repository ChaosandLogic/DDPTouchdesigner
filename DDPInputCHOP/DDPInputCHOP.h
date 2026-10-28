#ifndef __DDPInputCHOP__
#define __DDPInputCHOP__

// Must include winsock2 before Windows.h to avoid conflicts
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX  // Prevent Windows from defining min/max macros
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#endif

#include "CHOP_CPlusPlusBase.h"
#include <chrono>
#include <vector>
#include <string>

using namespace TD;

#ifndef _WIN32
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "DDPProtocol.h"

// Upper bound on the reassembled frame buffer; packets addressing beyond this are dropped
#define DDP_MAX_FRAME_BYTES (1024 * 1024)

// Requested OS socket receive buffer size
#define DDP_RECV_BUFFER_BYTES (4 * 1024 * 1024)

// Without a PUSH from any sender for this long, frames are shown as packets
// arrive (senders that never set PUSH)
#define DDP_PUSH_TIMEOUT_SEC 1.0

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
    void receiveData(bool discoverable);
    bool parseDDPPacket(const uint8_t* buffer, size_t length, 
                        uint32_t& offset, uint16_t& dataLen, 
                        const uint8_t*& pixelData);
    bool isStatusQuery(const uint8_t* buffer, size_t length) const;
    void sendStatusReply(const sockaddr_in& to);
    void publishFrame();
    
    // Socket members
    #ifdef _WIN32
        SOCKET m_socket;
        bool m_wsaInitialized;
    #else
        int m_socket;
    #endif
    
    bool m_socketInitialized;
    
    // Packets are assembled in m_workPixelData and copied to m_receivedPixelData
    // (what the CHOP outputs) on PUSH, so a frame never shows half old, half new.
    std::vector<uint8_t> m_workPixelData;
    std::vector<uint8_t> m_receivedPixelData;
    bool m_workDirty;
    std::chrono::steady_clock::time_point m_lastPushTime;
    bool m_pushSeen;
    int32_t m_receivedPixelCount;
    int64_t m_packetsReceived;
    int64_t m_bytesReceived;
    int64_t m_framesReceived;
    bool m_showStats;
    
    // Source tracking
    std::string m_lastSourceIP;
    uint16_t m_lastSourcePort;
    
    // Error tracking
    std::string m_lastError;
    
    const OP_NodeInfo* myNodeInfo;
};

#endif



