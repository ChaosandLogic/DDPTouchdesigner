#include "DDPOutputCHOP.h"
#include <algorithm>

bool DDPOutputCHOP::ensureNetworking()
{
    #ifdef _WIN32
    if (!m_wsaInitialized)
    {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0)
        {
            m_lastError = "WSAStartup failed";
            return false;
        }
        m_wsaInitialized = true;
    }
    #endif
    return true;
}

void DDPOutputCHOP::initializeSocket()
{
    std::lock_guard<std::mutex> lock(m_socketMutex);
    if (m_socketInitialized)
        return;

    if (!ensureNetworking())
        return;

    int sendBufferBytes = DDP_SEND_BUFFER_BYTES;
    
    #ifdef _WIN32
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

    setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufferBytes, sizeof(sendBufferBytes));
    
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

    setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &sendBufferBytes, sizeof(sendBufferBytes));
    
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
    std::lock_guard<std::mutex> lock(m_socketMutex);
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
    
    // Byte 1: Sequence number. 0 = unused (DDP spec). Non-zero values trip WLED's
    // "skip out-of-sequence" filter on larger frames and cause flicker.
    packet[1] = 0;
    
    // Byte 2: Data type
    packet[2] = m_dataType;
    
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
}

void DDPOutputCHOP::sendPacket(const std::vector<uint8_t>& packet, const sockaddr_in& dest)
{
    int sendResult = sendto(m_socket,
                           reinterpret_cast<const char*>(packet.data()),
                           static_cast<int>(packet.size()),
                           0,
                           reinterpret_cast<const struct sockaddr*>(&dest),
                           sizeof(dest));

    if (sendResult > 0 && m_showStats)
    {
        m_packetsSent++;
        m_bytesSent += sendResult;
    }
    else if (sendResult < 0)
    {
        #ifdef _WIN32
            m_lastSendError = WSAGetLastError();
        #else
            m_lastSendError = errno;
        #endif
    }
}

void DDPOutputCHOP::sendPushPacketTo(const sockaddr_in& dest)
{
    // PUSH packet with no data (just header). Sequence 0 = unused.
    m_packetBuffer.assign(DDP_HEADER_SIZE, 0);
    m_packetBuffer[0] = DDP_FLAGS1_VER1 | DDP_FLAGS1_PUSH;
    m_packetBuffer[1] = 0;
    m_packetBuffer[2] = m_dataType;
    m_packetBuffer[3] = DDP_ID_DISPLAY;

    sendPacket(m_packetBuffer, dest);
}

void DDPOutputCHOP::sendFrame(const SendFrame& frame)
{
    std::lock_guard<std::mutex> lock(m_socketMutex);
    if (!m_socketInitialized || frame.pixels.empty() || frame.routes.empty())
        return;

    using Clock = std::chrono::steady_clock;

    // Pace between controllers, not between packets to the same one.
    // Spreading one device's packets over tens of ms makes partial-frame flashes
    // much more likely if the receiver updates before it has seen a PUSH.
    const size_t routeCount = frame.routes.size();
    const bool paceRoutes = frame.pacingWindowSec > 0.0 && routeCount > 1;
    const Clock::time_point start = Clock::now();
    const Clock::duration window = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(frame.pacingWindowSec));

    for (size_t routeIndex = 0; routeIndex < routeCount; routeIndex++)
    {
        if (paceRoutes)
        {
            Clock::time_point due = start + window * static_cast<Clock::rep>(routeIndex)
                / static_cast<Clock::rep>(routeCount);
            if (due - Clock::now() >= std::chrono::milliseconds(1))
                std::this_thread::sleep_until(due);
        }

        const DeviceRoute& route = frame.routes[routeIndex];
        const uint8_t* data = frame.pixels.data() + route.byteOffset;
        size_t bytesSent = 0;

        // Each controller gets a tight burst, with PUSH on its last data packet.
        // Empty PUSH packets are easy to lose and leave the display frozen or flashing.
        while (bytesSent < route.byteCount)
        {
            size_t bytesInPacket = std::min(route.byteCount - bytesSent, static_cast<size_t>(DDP_MAX_DATALEN));
            bool isLastPacket = (bytesSent + bytesInPacket >= route.byteCount);
            createDDPPacket(data + bytesSent, bytesInPacket, bytesSent,
                            isLastPacket, m_packetBuffer);
            sendPacket(m_packetBuffer, route.addr);
            bytesSent += bytesInPacket;
        }
    }
}

void DDPOutputCHOP::senderLoop()
{
    #ifdef _WIN32
        // 1 ms sleep resolution for pacing; the default is ~15.6 ms.
        timeBeginPeriod(1);
    #endif

    SendFrame frame;
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(m_frameMutex);
            m_frameCv.wait(lock, [this] { return m_stopSender || m_hasPendingFrame; });
            if (m_stopSender)
                break;
            std::swap(frame, m_pendingFrame);
            m_hasPendingFrame = false;
        }
        sendFrame(frame);
    }

    #ifdef _WIN32
        timeEndPeriod(1);
    #endif
}

void DDPOutputCHOP::submitFrame(std::vector<DeviceRoute>& routes)
{
    auto now = std::chrono::steady_clock::now();
    double interval = std::chrono::duration<double>(now - m_lastSubmitTime).count();
    m_lastSubmitTime = now;
    if (interval > 0.0 && interval < 1.0)
        m_frameIntervalSec = 0.8 * m_frameIntervalSec + 0.2 * interval;

    double window = std::min(m_frameIntervalSec * DDP_PACING_WINDOW_FRACTION, DDP_PACING_MAX_WINDOW_SEC);

    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (m_hasPendingFrame)
            m_framesDropped++;
        // Swap so both buffers keep their capacity and nothing is reallocated per frame.
        std::swap(m_pendingFrame.pixels, m_workPixels);
        std::swap(m_pendingFrame.routes, routes);
        m_pendingFrame.pacingWindowSec = window;
        m_hasPendingFrame = true;
    }
    m_frameCv.notify_one();
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

