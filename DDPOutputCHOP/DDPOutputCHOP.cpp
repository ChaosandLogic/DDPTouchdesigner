#include "DDPOutputCHOP.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <errno.h>

using namespace TD;

// Plugin Info
extern "C"
{
    DLLEXPORT void FillCHOPPluginInfo(CHOP_PluginInfo *info)
    {
        info->apiVersion = CHOPCPlusPlusAPIVersion;
        info->customOPInfo.opType->setString("Ddpout");
        info->customOPInfo.opLabel->setString("DDP Out");
        info->customOPInfo.authorName->setString("Glen Wilde");
        info->customOPInfo.authorEmail->setString("Glen.w.wilde@gmail.com");
        info->customOPInfo.minInputs = 1;
        info->customOPInfo.maxInputs = 1;
        
        // Ensure the node cooks on startup (important for output nodes)
        info->customOPInfo.cookOnStart = true;
    }

    DLLEXPORT CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
    {
        return new DDPOutputCHOP(info);
    }

    DLLEXPORT void DestroyCHOPInstance(CHOP_CPlusPlusBase* instance)
    {
        delete (DDPOutputCHOP*)instance;
    }
};

DDPOutputCHOP::DDPOutputCHOP(const OP_NodeInfo* info) : myNodeInfo(info)
{
    m_socketInitialized = false;
    m_needsReinitialize = false;
    m_sequenceNumber = 0;
    m_packetsSent = 0;
    m_bytesSent = 0;
    m_lastChannelCount = 0;
    m_lastPixelCount = 0;
    m_lastPort = 0;
    m_showStats = false;
    m_isDiscovering = false;
    m_lastFrameTime = 0.0;
    
    #ifdef _WIN32
        m_socket = INVALID_SOCKET;
        m_wsaInitialized = false;
    #else
        m_socket = -1;
    #endif
    
    memset(&m_destAddr, 0, sizeof(m_destAddr));
    memset(&m_broadcastAddr, 0, sizeof(m_broadcastAddr));
}

DDPOutputCHOP::~DDPOutputCHOP()
{
    closeSocket();
}

void DDPOutputCHOP::getGeneralInfo(CHOP_GeneralInfo* ginfo, const OP_Inputs* inputs, void* reserved1)
{
    // Cook every frame since this is a network input/output node
    ginfo->cookEveryFrame = true;
    ginfo->timeslice = false;
    ginfo->inputMatchIndex = 0;
}

bool DDPOutputCHOP::getOutputInfo(CHOP_OutputInfo* info, const OP_Inputs* inputs, void* reserved1)
{
    info->numChannels = 5;
    info->numSamples = 1;
    info->sampleRate = 60;
    return true;
}

void DDPOutputCHOP::getChannelName(int32_t index, OP_String *name, const OP_Inputs* inputs, void* reserved1)
{
    switch (index)
    {
        case 0:
            name->setString("enabled");
            break;
        case 1:
            name->setString("packets_sent");
            break;
        case 2:
            name->setString("kb_sent");
            break;
        case 3:
            name->setString("channel_count");
            break;
        case 4:
            name->setString("pixel_count");
            break;
    }
}

void DDPOutputCHOP::setupParameters(OP_ParameterManager* manager, void* reserved1)
{
    // IP Address
    {
        OP_StringParameter sp;
        sp.name = "Ipaddress";
        sp.label = "IP Address";
        sp.defaultValue = "127.0.0.1";
        OP_ParAppendResult res = manager->appendString(sp);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Port
    {
        OP_NumericParameter np;
        np.name = "Port";
        np.label = "Port";
        np.defaultValues[0] = DDP_PORT;
        np.minSliders[0] = 1;
        np.maxSliders[0] = 65535;
        np.minValues[0] = 1;
        np.maxValues[0] = 65535;
        OP_ParAppendResult res = manager->appendInt(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Enable Output
    {
        OP_NumericParameter np;
        np.name = "Enable";
        np.label = "Enable Output";
        np.defaultValues[0] = 1;
        OP_ParAppendResult res = manager->appendToggle(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Note: Expects 1 channel with samples arranged as r0,g0,b0,r1,g1,b1...
    // Like DMX Out CHOP - use Shuffle CHOP to arrange RGB channels into one channel with samples
    
    // Gamma Correction
    {
        OP_NumericParameter np;
        np.name = "Gamma";
        np.label = "Gamma";
        np.defaultValues[0] = 1.0;
        np.minSliders[0] = 0.1;
        np.maxSliders[0] = 3.0;
        np.minValues[0] = 0.01;
        np.maxValues[0] = 5.0;
        OP_ParAppendResult res = manager->appendFloat(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Brightness
    {
        OP_NumericParameter np;
        np.name = "Brightness";
        np.label = "Brightness";
        np.defaultValues[0] = 1.0;
        np.minSliders[0] = 0.0;
        np.maxSliders[0] = 1.0;
        np.minValues[0] = 0.0;
        np.maxValues[0] = 1.0;
        OP_ParAppendResult res = manager->appendFloat(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Channels Per Pixel (for display purposes only)
    {
        OP_NumericParameter np;
        np.name = "Channelsperpixel";
        np.label = "Channels Per Pixel";
        np.defaultValues[0] = 3.0; // RGB default
        np.minSliders[0] = 1.0;
        np.maxSliders[0] = 6.0;
        np.minValues[0] = 1.0;
        np.maxValues[0] = 10.0;
        np.clampMins[0] = true;
        np.clampMaxes[0] = true;
        OP_ParAppendResult res = manager->appendInt(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Auto Push (for single device vs multi-device sync)
    {
        OP_NumericParameter np;
        np.name = "Autopush";
        np.label = "Auto Push";
        np.defaultValues[0] = 1;
        OP_ParAppendResult res = manager->appendToggle(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Show Stats Toggle
    {
        OP_NumericParameter np;
        np.name = "Showstats";
        np.label = "Show Stats";
        np.defaultValues[0] = 0; // Off by default for better performance
        OP_ParAppendResult res = manager->appendToggle(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Max FPS
    {
        OP_NumericParameter np;
        np.name = "Maxfps";
        np.label = "Max FPS";
        np.defaultValues[0] = 0.0; // 0 = unlimited
        np.minSliders[0] = 0.0;
        np.maxSliders[0] = 120.0;
        np.minValues[0] = 0.0;
        np.maxValues[0] = 240.0;
        np.clampMins[0] = true;
        np.clampMaxes[0] = true;
        OP_ParAppendResult res = manager->appendFloat(np);
        assert(res == OP_ParAppendResult::Success);
    }
    
    // Discover Devices Button
    {
        OP_NumericParameter np;
        np.name = "Discover";
        np.label = "Discover Devices";
        OP_ParAppendResult res = manager->appendPulse(np);
        assert(res == OP_ParAppendResult::Success);
    }
}

void DDPOutputCHOP::pulsePressed(const char* name, void* reserved1)
{
    if (strcmp(name, "Discover") == 0)
    {
        discoverDevices();
    }
}

void DDPOutputCHOP::initializeSocket()
{
    if (m_socketInitialized)
        return;
    
    #ifdef _WIN32
    if (!m_wsaInitialized)
    {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0)
        {
            m_lastError = "WSAStartup failed";
            return;
        }
        m_wsaInitialized = true;
    }
    
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET)
    {
        m_lastError = "Socket creation failed";
        return;
    }
    
    // Enable address reuse (allows binding even if port is in use)
    BOOL reuseAddr = TRUE;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseAddr, sizeof(reuseAddr));
    
    // Enable broadcast
    BOOL broadcastEnable = TRUE;
    setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, (char*)&broadcastEnable, sizeof(broadcastEnable));
    
    // Bind to any local address on DDP port to receive broadcast responses
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(0);  // Use ephemeral port for sending, but we'll listen on this socket
    
    if (bind(m_socket, (struct sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR)
    {
        m_lastError = "Socket bind failed";
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return;
    }
    
    #else
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket < 0)
    {
        int error = errno;
        m_lastError = "Socket creation failed, errno " + std::to_string(error) + ": " + strerror(error);
        return;
    }
    
    // Enable address reuse (allows binding even if port is in use)
    int reuseAddr = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr));
    
    // Enable broadcast
    int broadcastEnable = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    
    // Bind to any local address on DDP port to receive broadcast responses
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(0);  // Use ephemeral port for sending, but we'll listen on this socket
    
    if (bind(m_socket, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0)
    {
        int error = errno;
        m_lastError = "Socket bind failed, errno " + std::to_string(error) + ": " + strerror(error);
        close(m_socket);
        m_socket = -1;
        return;
    }
    #endif
    
    m_socketInitialized = true;
    m_lastError = "Socket initialized successfully";
}

void DDPOutputCHOP::closeSocket()
{
    if (m_socketInitialized)
    {
        #ifdef _WIN32
            closesocket(m_socket);
            if (m_wsaInitialized)
            {
                WSACleanup();
                m_wsaInitialized = false;
            }
        #else
            close(m_socket);
        #endif
        
        m_socketInitialized = false;
    }
}

void DDPOutputCHOP::createDDPPacket(const uint8_t* pixelData, size_t dataLength, 
                                     size_t offset, bool pushFlag, std::vector<uint8_t>& packet)
{
    packet.resize(DDP_HEADER_SIZE + dataLength);
    
    // Byte 0: Flags (Version 1, optional PUSH flag)
    packet[0] = DDP_FLAGS1_VER1;
    if (pushFlag)
        packet[0] |= DDP_FLAGS1_PUSH;
    
    // Byte 1: Sequence number (lower 4 bits)
    packet[1] = m_sequenceNumber & 0x0F;
    
    // Byte 2: Data type (RGB - most common, receiver determines actual format)
    packet[2] = DDP_DATA_TYPE_RGB;
    
    // Byte 3: Destination ID (Display)
    packet[3] = DDP_ID_DISPLAY;
    
    // Bytes 4-7: Data offset (32-bit big-endian)
    uint32_t offset32 = static_cast<uint32_t>(offset);
    packet[4] = (offset32 >> 24) & 0xFF;
    packet[5] = (offset32 >> 16) & 0xFF;
    packet[6] = (offset32 >> 8) & 0xFF;
    packet[7] = offset32 & 0xFF;
    
    // Bytes 8-9: Data length (16-bit big-endian)
    uint16_t length16 = static_cast<uint16_t>(dataLength);
    packet[8] = (length16 >> 8) & 0xFF;
    packet[9] = length16 & 0xFF;
    
    // Copy pixel data
    std::memcpy(&packet[DDP_HEADER_SIZE], pixelData, dataLength);
    
    // Increment sequence number (wraps at 16)
    m_sequenceNumber = (m_sequenceNumber + 1) & 0x0F;
}

void DDPOutputCHOP::sendDDPData(const std::vector<uint8_t>& pixelData)
{
    if (!m_socketInitialized || pixelData.empty())
        return;
    
    size_t totalBytes = pixelData.size();
    size_t bytesSent = 0;
    
    // Determine if this is the last packet for auto-push
    bool autoPush = true; // Will be set from parameter in execute()
    
    while (bytesSent < totalBytes)
    {
        size_t bytesRemaining = totalBytes - bytesSent;
        size_t bytesInPacket = std::min(bytesRemaining, static_cast<size_t>(DDP_MAX_DATALEN));
        
        // Only push on last packet if auto-push is enabled
        bool isLastPacket = (bytesSent + bytesInPacket >= totalBytes);
        bool pushFlag = autoPush && isLastPacket;
        
        std::vector<uint8_t> packet;
        createDDPPacket(&pixelData[bytesSent], bytesInPacket, bytesSent, pushFlag, packet);
        
        int sendResult = sendto(m_socket, 
                               reinterpret_cast<const char*>(packet.data()), 
                               static_cast<int>(packet.size()), 
                               0,
                               reinterpret_cast<struct sockaddr*>(&m_destAddr), 
                               sizeof(m_destAddr));
        
        if (sendResult > 0 && m_showStats)
        {
            m_packetsSent++;
            m_bytesSent += sendResult;
        }
        else if (sendResult < 0)
        {
            #ifdef _WIN32
                int error = WSAGetLastError();
                m_lastError = "Send failed with error: " + std::to_string(error);
            #else
                int error = errno;
                m_lastError = "Send failed with errno " + std::to_string(error) + ": " + strerror(error);
            #endif
        }
        
        bytesSent += bytesInPacket;
    }
}

void DDPOutputCHOP::sendPushPacket()
{
    if (!m_socketInitialized)
        return;
    
    // Create a PUSH packet with no data (just header)
    std::vector<uint8_t> packet(DDP_HEADER_SIZE);
    
    packet[0] = DDP_FLAGS1_VER1 | DDP_FLAGS1_PUSH;
    packet[1] = m_sequenceNumber & 0x0F;
    packet[2] = DDP_DATA_TYPE_RGB;
    packet[3] = DDP_ID_DISPLAY;
    packet[4] = 0;
    packet[5] = 0;
    packet[6] = 0;
    packet[7] = 0;
    packet[8] = 0;  // Zero length
    packet[9] = 0;
    
    // Send to unicast address (for single device or as trigger)
    int sendResult = sendto(m_socket,
                           reinterpret_cast<const char*>(packet.data()),
                           static_cast<int>(packet.size()),
                           0,
                           reinterpret_cast<struct sockaddr*>(&m_destAddr),
                           sizeof(m_destAddr));
    
    if (sendResult > 0 && m_showStats)
    {
        m_packetsSent++;
        m_bytesSent += sendResult;
    }
    
    m_sequenceNumber = (m_sequenceNumber + 1) & 0x0F;
}

uint8_t DDPOutputCHOP::floatToUint8(float value)
{
    // Clamp to 0-1 range, then convert to 0-255
    value = std::max(0.0f, std::min(1.0f, value));
    return static_cast<uint8_t>(value * 255.0f);
}

float DDPOutputCHOP::applyGamma(float value, float gamma)
{
    if (gamma == 1.0f)
        return value;
    
    // Clamp input
    value = std::max(0.0f, std::min(1.0f, value));
    
    // Apply gamma correction
    return std::pow(value, 1.0f / gamma);
}

void DDPOutputCHOP::processInterleavedChannels(const OP_CHOPInput* chopInput, 
                                                 float gamma, float brightness,
                                                 std::vector<uint8_t>& pixelData)
{
    // Like DMX Out CHOP: expects 1 channel with consecutive samples
    // Works with any data: RGB, RGBW, or any channel count per pixel
    // Examples: r0,g0,b0,r1,g1,b1... or r0,g0,b0,w0,r1,g1,b1,w1...
    if (chopInput->numChannels < 1)
        return;
    
    const float* channelData = chopInput->getChannelData(0);
    int numSamples = chopInput->numSamples;
    
    pixelData.reserve(numSamples);
    
    for (int i = 0; i < numSamples; i++)
    {
        float value = channelData[i];
        
        // Apply brightness
        value *= brightness;
        
        // Apply gamma correction
        value = applyGamma(value, gamma);
        
        // Convert to 8-bit
        pixelData.push_back(floatToUint8(value));
    }
}

void DDPOutputCHOP::processSequentialChannels(const OP_CHOPInput* chopInput,
                                                float gamma, float brightness,
                                                std::vector<uint8_t>& pixelData)
{
    // Sequential: separate channels with multiple samples
    // Works with any channel count (RGB, RGBW, or custom)
    // Channel 0 = all reds, Channel 1 = all greens, etc.
    int numChannels = chopInput->numChannels;
    int numSamples = chopInput->numSamples;
    
    if (numChannels < 1)
        return;
    
    pixelData.reserve(numSamples * numChannels);
    
    // Interleave all channels
    for (int sample = 0; sample < numSamples; sample++)
    {
        for (int channel = 0; channel < numChannels; channel++)
        {
            const float* channelData = chopInput->getChannelData(channel);
            float value = channelData[sample];
            
            // Apply brightness
            value *= brightness;
            
            // Apply gamma correction
            value = applyGamma(value, gamma);
            
            // Convert to 8-bit
            pixelData.push_back(floatToUint8(value));
        }
    }
}

void DDPOutputCHOP::execute(CHOP_Output* output, const OP_Inputs* inputs, void* reserved1)
{
    // Get parameters
    const char* ipAddress = inputs->getParString("Ipaddress");
    int port = inputs->getParInt("Port");
    bool enabled = inputs->getParInt("Enable") != 0;
    float gamma = static_cast<float>(inputs->getParDouble("Gamma"));
    float brightness = static_cast<float>(inputs->getParDouble("Brightness"));
    int channelsPerPixel = inputs->getParInt("Channelsperpixel");
    bool autoPush = inputs->getParInt("Autopush") != 0;
    bool showStats = inputs->getParInt("Showstats") != 0;
    double maxFPS = inputs->getParDouble("Maxfps");
    
    // Reset stats when toggling
    if (showStats != m_showStats)
    {
        m_packetsSent = 0;
        m_bytesSent = 0;
    }
    m_showStats = showStats;
    
    // Output status channels
    output->channels[0][0] = enabled ? 1.0f : 0.0f;
    output->channels[1][0] = static_cast<float>(m_packetsSent);
    output->channels[2][0] = static_cast<float>(m_bytesSent / 1024.0);
    output->channels[3][0] = static_cast<float>(m_lastChannelCount);
    output->channels[4][0] = static_cast<float>(m_lastPixelCount);
    
    if (!enabled)
    {
        if (m_socketInitialized)
        {
            closeSocket();
        }
        return;
    }
    
    // Check if we need to reinitialize socket (IP or port changed)
    bool needsReinit = false;
    if (m_lastIPAddress != ipAddress || m_lastPort != port)
    {
        needsReinit = true;
        m_lastIPAddress = ipAddress;
        m_lastPort = port;
    }
    
    if (needsReinit && m_socketInitialized)
    {
        closeSocket();
    }
    
    // Initialize socket if needed
    if (!m_socketInitialized)
    {
        initializeSocket();
        
        // Setup destination address
        memset(&m_destAddr, 0, sizeof(m_destAddr));
        m_destAddr.sin_family = AF_INET;
        m_destAddr.sin_port = htons(port);
        inet_pton(AF_INET, ipAddress, &m_destAddr.sin_addr);
    }
    
    // Get input CHOP
    const OP_CHOPInput* chopInput = inputs->getInputCHOP(0);
    if (!chopInput || chopInput->numChannels == 0)
        return;
    
    // Check FPS limit
    if (maxFPS > 0 && !shouldSendFrame(maxFPS))
    {
        return; // Skip this frame to maintain target FPS
    }
    
    // Process channel data (expects 1 channel with samples)
    // Like DMX Out: agnostic to format (RGB, RGBW, or any channel count)
    // Examples: r0,g0,b0,r1,g1,b1... or r0,g0,b0,w0,r1,g1,b1,w1...
    std::vector<uint8_t> pixelData;
    processInterleavedChannels(chopInput, gamma, brightness, pixelData);
    
    // Update channel and pixel counts
    m_lastChannelCount = static_cast<int32_t>(pixelData.size());
    m_lastPixelCount = (channelsPerPixel > 0) ? (m_lastChannelCount / channelsPerPixel) : 0;
    
    // Send DDP packets if we have data
    if (!pixelData.empty())
    {
        sendDDPData(pixelData);
    }
}

int32_t DDPOutputCHOP::getNumInfoCHOPChans(void* reserved1)
{
    return 4;
}

void DDPOutputCHOP::getInfoCHOPChan(int32_t index, OP_InfoCHOPChan* chan, void* reserved1)
{
    switch (index)
    {
        case 0:
            chan->name->setString("packets_sent");
            chan->value = static_cast<float>(m_packetsSent);
            break;
        case 1:
            chan->name->setString("kb_sent");
            chan->value = static_cast<float>(m_bytesSent / 1024.0);
            break;
        case 2:
            chan->name->setString("channel_count");
            chan->value = static_cast<float>(m_lastChannelCount);
            break;
        case 3:
            chan->name->setString("pixel_count");
            chan->value = static_cast<float>(m_lastPixelCount);
            break;
    }
}

bool DDPOutputCHOP::getInfoDATSize(OP_InfoDATSize* infoSize, void* reserved1)
{
    infoSize->rows = 7 + static_cast<int32_t>(m_discoveredDevices.size());
    infoSize->cols = 2;
    infoSize->byColumn = false;
    return true;
}

void DDPOutputCHOP::getInfoDATEntries(int32_t index, int32_t nEntries, 
                                       OP_InfoDATEntries* entries, void* reserved1)
{
    if (index == 0)
    {
        entries->values[0]->setString("Packets Sent");
        entries->values[1]->setString(std::to_string(m_packetsSent).c_str());
    }
    else if (index == 1)
    {
        entries->values[0]->setString("Bytes Sent");
        entries->values[1]->setString(std::to_string(m_bytesSent).c_str());
    }
    else if (index == 2)
    {
        entries->values[0]->setString("Channel Count");
        entries->values[1]->setString(std::to_string(m_lastChannelCount).c_str());
    }
    else if (index == 3)
    {
        entries->values[0]->setString("Pixel Count");
        entries->values[1]->setString(std::to_string(m_lastPixelCount).c_str());
    }
    else if (index == 4)
    {
        entries->values[0]->setString("IP Address");
        entries->values[1]->setString(m_lastIPAddress.c_str());
    }
    else if (index == 5)
    {
        entries->values[0]->setString("Last Error");
        entries->values[1]->setString(m_lastError.c_str());
    }
    else if (index == 6)
    {
        entries->values[0]->setString("Devices Found");
        entries->values[1]->setString(std::to_string(m_discoveredDevices.size()).c_str());
    }
    else if (index >= 7 && index < 7 + static_cast<int32_t>(m_discoveredDevices.size()))
    {
        int deviceIdx = index - 7;
        entries->values[0]->setString(("Device " + std::to_string(deviceIdx + 1)).c_str());
        entries->values[1]->setString(m_discoveredDevices[deviceIdx].c_str());
    }
}

void DDPOutputCHOP::sendQueryPacket()
{
    if (!m_socketInitialized)
        return;
    
    // Create DDP STATUS query packet
    std::vector<uint8_t> packet(10);
    
    // Header
    packet[0] = DDP_FLAGS1_VER1 | 0x02;  // VER1 + QUERY flag
    packet[1] = 0;                        // Sequence 0
    packet[2] = 0x00;                     // Data type (ignored for query)
    packet[3] = DDP_ID_STATUS;            // STATUS destination (251)
    packet[4] = 0;                        // Offset (all zeros)
    packet[5] = 0;
    packet[6] = 0;
    packet[7] = 0;
    packet[8] = 0;                        // Length = 0
    packet[9] = 0;
    
    // Enable broadcast on socket
    int broadcastEnable = 1;
    #ifdef _WIN32
        setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcastEnable, sizeof(broadcastEnable));
    #else
        setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    #endif
    
    // Send to broadcast address (255.255.255.255)
    struct sockaddr_in broadcastAddr;
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(DDP_PORT);
    broadcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    
    sendto(m_socket, 
           reinterpret_cast<const char*>(packet.data()), 
           static_cast<int>(packet.size()), 
           0,
           reinterpret_cast<struct sockaddr*>(&broadcastAddr), 
           sizeof(broadcastAddr));
    
    // Also try sending to the configured destination (if it's set)
    if (m_destAddr.sin_addr.s_addr != 0)
    {
        struct sockaddr_in targetAddr = m_destAddr;
        targetAddr.sin_port = htons(DDP_PORT);
        
        sendto(m_socket, 
               reinterpret_cast<const char*>(packet.data()), 
               static_cast<int>(packet.size()), 
               0,
               reinterpret_cast<struct sockaddr*>(&targetAddr), 
               sizeof(targetAddr));
    }
}

void DDPOutputCHOP::discoverDevices()
{
    if (!m_socketInitialized)
    {
        m_lastError = "Socket not initialized. Enable output first.";
        return;
    }
    
    // Clear previous discoveries
    m_discoveredDevices.clear();
    m_isDiscovering = true;
    
    m_lastError = "Sending discovery query...";
    
    // Send query packet (try multiple times)
    for (int i = 0; i < 3; i++)
    {
        sendQueryPacket();
        #ifdef _WIN32
            Sleep(50);
        #else
            usleep(50000);
        #endif
    }
    
    // Set socket to non-blocking temporarily
    #ifdef _WIN32
        u_long mode = 1;  // 1 = non-blocking
        ioctlsocket(m_socket, FIONBIO, &mode);
    #else
        int sockFlags = fcntl(m_socket, F_GETFL, 0);
        fcntl(m_socket, F_SETFL, sockFlags | O_NONBLOCK);
    #endif
    
    // Listen for responses for ~500ms (longer timeout for FPP)
    char buffer[2048];
    struct sockaddr_in responseAddr;
    
    #ifdef _WIN32
        int addrLen = sizeof(responseAddr);
    #else
        socklen_t addrLen = sizeof(responseAddr);
    #endif
    
    // Try to receive responses (non-blocking)
    int packetsReceived = 0;
    for (int i = 0; i < 50; i++)  // 50 attempts with ~10ms between = 500ms total
    {
        #ifdef _WIN32
            addrLen = sizeof(responseAddr);
        #else
            addrLen = sizeof(responseAddr);
        #endif
        
        int received = recvfrom(m_socket, buffer, sizeof(buffer), 0,
                               reinterpret_cast<struct sockaddr*>(&responseAddr),
                               &addrLen);
        
        if (received > 0)
        {
            packetsReceived++;
            
            // Get IP address regardless of packet content
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &responseAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
            
            // Store discovered device - be very permissive
            // Accept ANY UDP response on ANY port as a potential DDP device
            std::string deviceInfo = std::string(ipStr);
            
            // Add port if not standard DDP port
            if (ntohs(responseAddr.sin_port) != DDP_PORT)
            {
                deviceInfo += ":" + std::to_string(ntohs(responseAddr.sin_port));
            }
            
            // Check for duplicates
            bool found = false;
            for (const auto& dev : m_discoveredDevices)
            {
                if (dev == deviceInfo)
                {
                    found = true;
                    break;
                }
            }
            
            if (!found)
            {
                m_discoveredDevices.push_back(deviceInfo);
                m_lastError = "Found device: " + deviceInfo + " (packet size: " + std::to_string(received) + ")";
            }
        }
        
        // Small delay between attempts
        #ifdef _WIN32
            Sleep(10);
        #else
            usleep(10000);  // 10ms
        #endif
    }
    
    m_lastError = "Discovery scan complete. Received " + std::to_string(packetsReceived) + " packets";
    
    // Restore blocking mode
    #ifdef _WIN32
        mode = 0;  // 0 = blocking
        ioctlsocket(m_socket, FIONBIO, &mode);
    #else
        sockFlags = fcntl(m_socket, F_GETFL, 0);
        fcntl(m_socket, F_SETFL, sockFlags & ~O_NONBLOCK);
    #endif
    
    m_isDiscovering = false;
    
    if (m_discoveredDevices.empty())
    {
        m_lastError += " - No devices responded. Check: 1) Device is on, 2) Same network/subnet, 3) Windows firewall, 4) Try entering IP manually";
    }
    else
    {
        m_lastError = "Discovery complete: " + std::to_string(m_discoveredDevices.size()) + " device(s) found";
    }
}

bool DDPOutputCHOP::shouldSendFrame(double targetFPS)
{
    if (targetFPS <= 0)
        return true; // No limit
    
    // Get current time in seconds
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    double currentTime = std::chrono::duration<double>(duration).count();
    
    // Calculate minimum time between frames
    double minFrameTime = 1.0 / targetFPS;
    double elapsed = currentTime - m_lastFrameTime;
    
    if (elapsed >= minFrameTime)
    {
        m_lastFrameTime = currentTime;
        return true;
    }
    
    return false; // Too soon, skip this frame
}

