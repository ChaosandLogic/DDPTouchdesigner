#include "DDPInputCHOP.h"
#include <cstring>
#include <algorithm>
#include <errno.h>

using namespace TD;

namespace
{

const char* kStatusReplyJson =
    "{\"status\":{\"man\":\"TouchDesigner\",\"mod\":\"DDP In\",\"ver\":\"1.0\"}}";

// Menu names are "3" / "4", matching DDP Out.
int channelsPerPixelFromInputs(const OP_Inputs* inputs)
{
    const char* text = inputs->getParString("Channelsperpixel");
    if (text && std::strcmp(text, "4") == 0)
        return 4;
    return 3;
}

} // namespace

// Plugin Info
extern "C"
{
    DLLEXPORT void FillCHOPPluginInfo(CHOP_PluginInfo *info)
    {
        info->apiVersion = CHOPCPlusPlusAPIVersion;
        info->customOPInfo.opType->setString("Ddpin");
        info->customOPInfo.opLabel->setString("DDP In");
        info->customOPInfo.authorName->setString("Glen Wilde");
        info->customOPInfo.authorEmail->setString("Glen.w.wilde@gmail.com");
        info->customOPInfo.minInputs = 0;  // No input needed
        info->customOPInfo.maxInputs = 0;  // Pure receiver
        
        info->customOPInfo.cookOnStart = true;
    }

    DLLEXPORT CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
    {
        return new DDPInputCHOP(info);
    }

    DLLEXPORT void DestroyCHOPInstance(CHOP_CPlusPlusBase* instance)
    {
        delete (DDPInputCHOP*)instance;
    }
};

DDPInputCHOP::DDPInputCHOP(const OP_NodeInfo* info) : myNodeInfo(info)
{
    m_socketInitialized = false;
    m_workDirty = false;
    m_pushSeen = false;
    m_receivedPixelCount = 0;
    m_packetsReceived = 0;
    m_bytesReceived = 0;
    m_framesReceived = 0;
    m_showStats = false;
    m_lastSourcePort = 0;
    
    #ifdef _WIN32
        m_socket = INVALID_SOCKET;
        m_wsaInitialized = false;
    #else
        m_socket = -1;
    #endif
}

DDPInputCHOP::~DDPInputCHOP()
{
    closeSocket();
}

void DDPInputCHOP::getGeneralInfo(CHOP_GeneralInfo* ginfo, const OP_Inputs* inputs, void* reserved1)
{
    ginfo->cookEveryFrame = true;
    ginfo->timeslice = false;
    ginfo->inputMatchIndex = 0;
}

bool DDPInputCHOP::getOutputInfo(CHOP_OutputInfo* info, const OP_Inputs* inputs, void* reserved1)
{
    // 4 status channels + 1 data channel with variable samples
    info->numChannels = 5;
    info->numSamples = std::max(1, static_cast<int>(m_receivedPixelData.size()));
    info->sampleRate = 60;
    return true;
}

void DDPInputCHOP::getChannelName(int32_t index, OP_String *name, const OP_Inputs* inputs, void* reserved1)
{
    switch (index)
    {
        case 0:
            name->setString("enabled");
            break;
        case 1:
            name->setString("packets_received");
            break;
        case 2:
            name->setString("kb_received");
            break;
        case 3:
            name->setString("pixel_count");
            break;
        case 4:
            name->setString("pixel_data");
            break;
        default:
            name->setString("");
            break;
    }
}

void DDPInputCHOP::setupParameters(OP_ParameterManager* manager, void* reserved1)
{
    // Enable
    {
        OP_NumericParameter np;
        np.name = "Enable";
        np.label = "Enable";
        np.defaultValues[0] = 1;
        OP_ParAppendResult res = manager->appendToggle(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Show Stats Toggle
    {
        OP_NumericParameter np;
        np.name = "Showstats";
        np.label = "Show Stats";
        np.defaultValues[0] = 0;
        OP_ParAppendResult res = manager->appendToggle(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Value Range (0-1 or 0-255)
    {
        OP_StringParameter sp;
        sp.name = "Valuerange";
        sp.label = "Value Range";
        sp.defaultValue = "0-1";
        
        const char* names[] = {"0-1", "0-255"};
        const char* labels[] = {"0-1 (Normalized)", "0-255 (8-bit)"};
        
        OP_ParAppendResult res = manager->appendMenu(sp, 2, names, labels);
        assert(res == OP_ParAppendResult::Success);
    }

    // Channels Per Pixel (only affects pixel_count; pixel_data is raw bytes)
    {
        OP_StringParameter sp;
        sp.name = "Channelsperpixel";
        sp.label = "Channels Per Pixel";
        sp.defaultValue = "3";

        const char* names[] = {"3", "4"};
        const char* labels[] = {"3 (RGB)", "4 (RGBW)"};

        OP_ParAppendResult res = manager->appendMenu(sp, 2, names, labels);
        assert(res == OP_ParAppendResult::Success);
    }

    // Discoverable: answer DDP status queries so DDP Out's discovery lists this machine.
    // Off by default so DDP Out doesn't add your own receiver to its routing table.
    {
        OP_NumericParameter np;
        np.name = "Discoverable";
        np.label = "Discoverable";
        np.defaultValues[0] = 0;
        OP_ParAppendResult res = manager->appendToggle(np);
        assert(res == OP_ParAppendResult::Success);
    }
}

void DDPInputCHOP::execute(CHOP_Output* output, const OP_Inputs* inputs, void* reserved1)
{
    // Get parameters
    bool enabled = inputs->getParInt("Enable") != 0;
    bool showStats = inputs->getParInt("Showstats") != 0;
    bool discoverable = inputs->getParInt("Discoverable") != 0;
    int channelsPerPixel = channelsPerPixelFromInputs(inputs);
    const char* valueRange = inputs->getParString("Valuerange");
    bool normalizedOutput = (strcmp(valueRange, "0-1") == 0);
    
    // Reset stats when toggling
    if (showStats != m_showStats)
    {
        m_packetsReceived = 0;
        m_bytesReceived = 0;
        m_framesReceived = 0;
    }
    m_showStats = showStats;
    
    if (!enabled)
    {
        if (m_socketInitialized)
        {
            closeSocket();
        }
        // Clear output channels
        output->channels[0][0] = 0.0f;
        output->channels[1][0] = 0.0f;
        output->channels[2][0] = 0.0f;
        output->channels[3][0] = 0.0f;
        return;
    }
    
    // Initialize socket if needed
    if (!m_socketInitialized)
    {
        initializeSocket();
        
        // Bind to receive port
        struct sockaddr_in bindAddr;
        memset(&bindAddr, 0, sizeof(bindAddr));
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(DDP_PORT);
        bindAddr.sin_addr.s_addr = INADDR_ANY;
        
        if (bind(m_socket, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0)
        {
            #ifdef _WIN32
                m_lastError = "bind to port 4048 failed (another DDP In or app using it?): " +
                              std::to_string(WSAGetLastError());
            #else
                m_lastError = "bind to port 4048 failed (another DDP In or app using it?): " +
                              std::string(strerror(errno));
            #endif
            closeSocket();
            output->channels[0][0] = 0.0f;
            return;
        }
        
        // Set socket to non-blocking
        #ifdef _WIN32
            u_long mode = 1;
            ioctlsocket(m_socket, FIONBIO, &mode);
        #else
            int flags = fcntl(m_socket, F_GETFL, 0);
            fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
        #endif
    }
    
    // Receive and parse DDP packets
    receiveData(discoverable);
    m_receivedPixelCount = static_cast<int32_t>(m_receivedPixelData.size() / channelsPerPixel);
    
    // Output status channels
    output->channels[0][0] = enabled ? 1.0f : 0.0f;
    output->channels[1][0] = static_cast<float>(m_packetsReceived);
    output->channels[2][0] = static_cast<float>(m_bytesReceived / 1024.0);
    output->channels[3][0] = static_cast<float>(m_receivedPixelCount);
    
    // Output received pixel data
    if (m_receivedPixelData.size() > 0)
    {
        int numSamples = static_cast<int>(m_receivedPixelData.size());
        for (int i = 0; i < numSamples && i < output->numSamples; i++)
        {
            if (normalizedOutput)
            {
                // Output as 0-1 range
                output->channels[4][i] = m_receivedPixelData[i] / 255.0f;
            }
            else
            {
                // Output as 0-255 range (default)
                output->channels[4][i] = static_cast<float>(m_receivedPixelData[i]);
            }
        }
    }
}

int32_t DDPInputCHOP::getNumInfoCHOPChans(void* reserved1)
{
    return 3;
}

void DDPInputCHOP::getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan, void* reserved1)
{
    switch (index)
    {
        case 0:
            chan->name->setString("packets_received");
            chan->value = static_cast<float>(m_packetsReceived);
            break;
        case 1:
            chan->name->setString("kb_received");
            chan->value = static_cast<float>(m_bytesReceived / 1024.0);
            break;
        case 2:
            chan->name->setString("pixel_count");
            chan->value = static_cast<float>(m_receivedPixelCount);
            break;
    }
}

bool DDPInputCHOP::getInfoDATSize(OP_InfoDATSize* infoSize, void* reserved1)
{
    infoSize->rows = 7;
    infoSize->cols = 2;
    infoSize->byColumn = false;
    return true;
}

void DDPInputCHOP::getInfoDATEntries(int32_t index, int32_t nEntries, 
                                      OP_InfoDATEntries* entries, void* reserved1)
{
    if (index == 0)
    {
        entries->values[0]->setString("Packets Received");
        entries->values[1]->setString(std::to_string(m_packetsReceived).c_str());
    }
    else if (index == 1)
    {
        entries->values[0]->setString("Bytes Received");
        entries->values[1]->setString(std::to_string(m_bytesReceived).c_str());
    }
    else if (index == 2)
    {
        entries->values[0]->setString("Pixel Count");
        entries->values[1]->setString(std::to_string(m_receivedPixelCount).c_str());
    }
    else if (index == 3)
    {
        entries->values[0]->setString("Listen Port");
        entries->values[1]->setString(std::to_string(DDP_PORT).c_str());
    }
    else if (index == 4)
    {
        entries->values[0]->setString("Last Source");
        std::string source = m_lastSourceIP;
        if (!source.empty())
            source += ":" + std::to_string(m_lastSourcePort);
        entries->values[1]->setString(source.c_str());
    }
    else if (index == 5)
    {
        entries->values[0]->setString("Last Error");
        entries->values[1]->setString(m_lastError.c_str());
    }
    else if (index == 6)
    {
        entries->values[0]->setString("Frames Received");
        entries->values[1]->setString(std::to_string(m_framesReceived).c_str());
    }
}

void DDPInputCHOP::initializeSocket()
{
    #ifdef _WIN32
        if (!m_wsaInitialized)
        {
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0)
            {
                m_lastError = "WSAStartup failed: " + std::to_string(result);
                return;
            }
            m_wsaInitialized = true;
        }
        
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET)
        {
            m_lastError = "socket creation failed: " + std::to_string(WSAGetLastError());
            return;
        }
    #else
        m_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_socket < 0)
        {
            m_lastError = "socket creation failed: " + std::string(strerror(errno));
            return;
        }
    #endif
    
    // Packets are only drained once per cook, so a full frame must fit in the OS buffer
    int rcvBufSize = DDP_RECV_BUFFER_BYTES;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBufSize), sizeof(rcvBufSize));
    
    m_socketInitialized = true;
    m_lastError = "";
}

void DDPInputCHOP::closeSocket()
{
    if (m_socketInitialized)
    {
        #ifdef _WIN32
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        #else
            close(m_socket);
            m_socket = -1;
        #endif
        
        m_socketInitialized = false;
    }
    
    #ifdef _WIN32
        if (m_wsaInitialized)
        {
            WSACleanup();
            m_wsaInitialized = false;
        }
    #endif
}

void DDPInputCHOP::receiveData(bool discoverable)
{
    if (!m_socketInitialized)
        return;
    
    uint8_t buffer[2048];
    struct sockaddr_in sourceAddr;
    
    // Receive all available packets (non-blocking)
    while (true)
    {
        socklen_t sourceAddrLen = sizeof(sourceAddr);
        #ifdef _WIN32
            int bytesReceived = recvfrom(m_socket, (char*)buffer, sizeof(buffer), 0,
                                        (struct sockaddr*)&sourceAddr, &sourceAddrLen);
            if (bytesReceived == SOCKET_ERROR)
            {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK)
                    break;
                m_lastError = "recvfrom failed: " + std::to_string(err);
                break;
            }
        #else
            ssize_t bytesReceived = recvfrom(m_socket, buffer, sizeof(buffer), 0,
                                            (struct sockaddr*)&sourceAddr, &sourceAddrLen);
            if (bytesReceived < 0)
            {
                if (errno == EWOULDBLOCK || errno == EAGAIN)
                    break;
                m_lastError = "recvfrom failed: " + std::string(strerror(errno));
                break;
            }
        #endif
        
        if (bytesReceived < DDP_HEADER_SIZE)
            continue;

        if (isStatusQuery(buffer, bytesReceived))
        {
            bool fromLoopback = ntohl(sourceAddr.sin_addr.s_addr) == INADDR_LOOPBACK;
            if (discoverable || fromLoopback)
                sendStatusReply(sourceAddr);
            continue;
        }
        
        // Parse DDP packet
        uint32_t offset;
        uint16_t dataLen;
        const uint8_t* pixelData;
        
        if (parseDDPPacket(buffer, bytesReceived, offset, dataLen, pixelData))
        {
            // Update stats
            if (m_showStats)
            {
                m_packetsReceived++;
                m_bytesReceived += bytesReceived;
            }
            
            // Store source IP
            char sourceIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sourceAddr.sin_addr, sourceIP, INET_ADDRSTRLEN);
            m_lastSourceIP = sourceIP;
            m_lastSourcePort = ntohs(sourceAddr.sin_port);
            
            // Resize buffer if needed
            uint64_t requiredSize = static_cast<uint64_t>(offset) + dataLen;
            if (requiredSize > DDP_MAX_FRAME_BYTES)
            {
                m_lastError = "Dropped packet: offset " + std::to_string(offset) +
                              " exceeds max frame size of " + std::to_string(DDP_MAX_FRAME_BYTES) + " bytes";
                continue;
            }
            if (m_workPixelData.size() < requiredSize)
            {
                m_workPixelData.resize(requiredSize, 0);
            }
            
            std::memcpy(m_workPixelData.data() + offset, pixelData, dataLen);
            m_workDirty = true;

            if (buffer[0] & DDP_FLAGS1_PUSH)
            {
                m_pushSeen = true;
                m_lastPushTime = std::chrono::steady_clock::now();
                publishFrame();
            }
            
            m_lastError = "";
        }
    }

    // Senders that never set PUSH (or stopped) still get shown, once per cook.
    if (m_workDirty)
    {
        double sincePush = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_lastPushTime).count();
        if (!m_pushSeen || sincePush > DDP_PUSH_TIMEOUT_SEC)
            publishFrame();
    }
}

void DDPInputCHOP::publishFrame()
{
    m_receivedPixelData = m_workPixelData;
    m_workDirty = false;
    m_framesReceived++;
}

bool DDPInputCHOP::isStatusQuery(const uint8_t* buffer, size_t length) const
{
    if (length < DDP_HEADER_SIZE)
        return false;
    uint8_t flags = buffer[0];
    return (flags & DDP_FLAGS1_VER) == DDP_FLAGS1_VER1 &&
           (flags & DDP_FLAGS1_QUERY) != 0 &&
           buffer[3] == DDP_ID_STATUS;
}

void DDPInputCHOP::sendStatusReply(const sockaddr_in& to)
{
    size_t jsonLen = std::strlen(kStatusReplyJson);
    std::vector<uint8_t> packet(DDP_HEADER_SIZE + jsonLen);
    packet[0] = DDP_FLAGS1_VER1 | DDP_FLAGS1_REPLY | DDP_FLAGS1_PUSH;
    packet[1] = 0;
    packet[2] = 0;
    packet[3] = DDP_ID_STATUS;
    packet[4] = packet[5] = packet[6] = packet[7] = 0;
    packet[8] = static_cast<uint8_t>((jsonLen >> 8) & 0xFF);
    packet[9] = static_cast<uint8_t>(jsonLen & 0xFF);
    std::memcpy(packet.data() + DDP_HEADER_SIZE, kStatusReplyJson, jsonLen);

    sendto(m_socket, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
           reinterpret_cast<const sockaddr*>(&to), sizeof(to));
}

bool DDPInputCHOP::parseDDPPacket(const uint8_t* buffer, size_t length,
                                   uint32_t& offset, uint16_t& dataLen,
                                   const uint8_t*& pixelData)
{
    if (length < DDP_HEADER_SIZE)
        return false;
    
    uint8_t flags = buffer[0];
    uint8_t destID = buffer[3];
    
    // Check version
    if ((flags & DDP_FLAGS1_VER) != DDP_FLAGS1_VER1)
        return false;
    
    // Only process display data
    if (destID != DDP_ID_DISPLAY)
        return false;
    
    // Data type (byte 2) is not filtered: senders use 0x00, 0x01, 0x0B (RGB24),
    // 0x1B (RGBW32) etc., and the raw channel bytes are passed through either way.
    
    size_t headerSize = (flags & DDP_FLAGS1_TIME) ? DDP_HEADER_SIZE_TIMECODE : DDP_HEADER_SIZE;
    if (length < headerSize)
        return false;
    
    // Extract offset (big-endian)
    offset = (static_cast<uint32_t>(buffer[4]) << 24) |
             (static_cast<uint32_t>(buffer[5]) << 16) |
             (static_cast<uint32_t>(buffer[6]) << 8) |
             static_cast<uint32_t>(buffer[7]);
    
    // Extract data length (big-endian)
    dataLen = (static_cast<uint16_t>(buffer[8]) << 8) |
              static_cast<uint16_t>(buffer[9]);
    
    // Validate
    if (dataLen > length - headerSize)
        return false;
    
    if (dataLen > DDP_MAX_DATALEN)
        return false;
    
    pixelData = buffer + headerSize;
    
    return true;
}



