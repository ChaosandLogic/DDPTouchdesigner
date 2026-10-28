#ifndef __DDPOutputCHOP__
#define __DDPOutputCHOP__

// Must include winsock2 before Windows.h to avoid conflicts
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX  // Prevent Windows from defining min/max macros
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <mmsystem.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "winmm.lib")
#endif

#include "CHOP_CPlusPlusBase.h"
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace TD;

#ifndef _WIN32
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "DDPProtocol.h"

// Requested socket send buffer. The OS default is too small for a whole frame of packets.
#define DDP_SEND_BUFFER_BYTES (4 * 1024 * 1024)

// Packet pacing: spread a frame's packets over this fraction of the frame interval,
// but never longer than DDP_PACING_MAX_WINDOW_SEC, so latency stays low at low frame rates.
#define DDP_PACING_WINDOW_FRACTION 0.75
#define DDP_PACING_MAX_WINDOW_SEC 0.030

// Discovery: how many JSON status bytes to request, and how long to wait for replies
// (spec suggests ~500 ms because devices delay by MAC address).
#define DDP_STATUS_QUERY_LEN 512
#define DDP_DISCOVERY_WAIT_MS 500

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
    
    // One row of the routing table: a slice of the input sent to one controller.
    // DDP offset for each device starts at 0 (that controller's first pixel).
    struct DeviceRoute
    {
        sockaddr_in addr;
        size_t byteOffset;
        size_t byteCount;
    };

    // One frame handed from execute() to the sender thread.
    struct SendFrame
    {
        std::vector<uint8_t> pixels;
        std::vector<DeviceRoute> routes;
        double pacingWindowSec = 0.0;  // 0 = send as one burst
    };

    // DDP packet creation and sending (sender thread only, socket mutex held)
    void createDDPPacket(const uint8_t* pixelData, size_t dataLength, 
                         size_t offset, bool pushFlag, std::vector<uint8_t>& packet);
    void sendPacket(const std::vector<uint8_t>& packet, const sockaddr_in& dest);
    void sendPushPacketTo(const sockaddr_in& dest);
    void sendFrame(const SendFrame& frame);

    // Sender thread
    void senderLoop();
    void submitFrame(std::vector<DeviceRoute>& routes);

    // Routing table. Returns false when no table is assigned (single-IP mode).
    bool parseRoutingTable(const OP_Inputs* inputs, int channelsPerPixel, size_t dataBytes,
                           std::vector<DeviceRoute>& routes);
    
    // Data processing
    void processInterleavedChannels(const OP_CHOPInput* chopInput, 
                                     float gamma, float brightness,
                                     bool normalizedInput,
                                     std::vector<uint8_t>& pixelData);
    void processSequentialChannels(const OP_CHOPInput* chopInput,
                                    float gamma, float brightness,
                                    bool normalizedInput,
                                    std::vector<uint8_t>& pixelData);
    
    // Helper functions
    uint8_t floatToUint8(float value, bool normalizedInput);
    float applyGamma(float value, float gamma, bool normalizedInput);
    
    // Socket members
    #ifdef _WIN32
        SOCKET m_socket;
        bool m_wsaInitialized;
    #else
        int m_socket;
    #endif
    
    struct sockaddr_in m_destAddr;
    struct sockaddr_in m_broadcastAddr;
    std::atomic<bool> m_socketInitialized;
    bool m_needsReinitialize;
    std::atomic<bool> m_showStats;

    // Guards m_socket while the sender thread or discovery is using it.
    std::mutex m_socketMutex;
    
    // DDP state
    uint8_t m_sequenceNumber;
    std::atomic<uint8_t> m_dataType;
    std::atomic<int64_t> m_packetsSent;
    std::atomic<int64_t> m_bytesSent;
    std::atomic<int> m_lastSendError;
    std::vector<uint8_t> m_packetBuffer;

    // Sender thread hand-off. Only the newest pending frame is kept.
    std::thread m_senderThread;
    std::mutex m_frameMutex;
    std::condition_variable m_frameCv;
    SendFrame m_pendingFrame;
    bool m_hasPendingFrame;
    bool m_stopSender;
    int64_t m_framesDropped;
    std::vector<uint8_t> m_workPixels;
    std::chrono::steady_clock::time_point m_lastSubmitTime;
    double m_frameIntervalSec;
    int32_t m_lastChannelCount;
    int32_t m_lastPixelCount;
    int32_t m_routeCount;
    std::vector<DeviceRoute> m_routes;
    std::string m_lastError;
    std::string m_lastIPAddress;
    
    // Device discovery (uses its own socket so the sender thread is undisturbed)
    std::vector<std::string> m_discoveredDevices;
    bool m_isDiscovering;
    void discoverDevices();
    void startAsyncDiscovery();
    bool collectAsyncDiscovery();
    // Touches only its arguments, so it can run on m_discoveryThread.
    void runDiscovery(const sockaddr_in& configuredDest, std::vector<std::string>& devices, std::string& status);
    bool ensureNetworking();
#ifdef _WIN32
    void sendDiscoveryQuery(SOCKET sock, const sockaddr_in& dest);
#else
    void sendDiscoveryQuery(int sock, const sockaddr_in& dest);
#endif
    void collectBroadcastTargets(const sockaddr_in& configuredDest, std::vector<sockaddr_in>& targets,
                                 std::vector<in_addr>& localUnicasts);
    bool parseStatusReply(const char* buffer, int length, const sockaddr_in& from, std::string& outInfo);
    void createRoutingTable();
    void finishDiscoveryIfReady();
    // Dock <name>_routing and <name>_info onto this CHOP, once they exist.
    void dockCompanionDATs();
    bool m_companionsDocked;
    // Discover & Update pressed: apply results when background discovery finishes.
    bool m_pendingTableUpdate;

    // Auto-create the routing table once, when a new node is placed.
    enum class AutoCreateState { Start, Discovering, BuildTable, Done };
    AutoCreateState m_autoCreateState;
    std::string m_autoCreateStatus;
    void updateAutoCreate(const OP_Inputs* inputs);

    // Background discovery hand-off
    std::thread m_discoveryThread;
    std::mutex m_discoveryMutex;
    std::atomic<bool> m_discoveryDone;
    std::vector<std::string> m_pendingDevices;
    std::string m_pendingDiscoveryStatus;
    
    // FPS limiting
    double m_lastFrameTime;
    bool shouldSendFrame(double targetFPS);
    
    const OP_NodeInfo* myNodeInfo;
};

#endif


