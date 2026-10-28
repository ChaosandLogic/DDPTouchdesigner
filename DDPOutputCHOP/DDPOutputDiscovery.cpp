#include "DDPOutputCHOP.h"
#include <cstring>
#ifndef _WIN32
    #include <ifaddrs.h>
    #include <net/if.h>
#endif

#ifdef _WIN32
void DDPOutputCHOP::sendDiscoveryQuery(SOCKET sock, const sockaddr_in& dest)
#else
void DDPOutputCHOP::sendDiscoveryQuery(int sock, const sockaddr_in& dest)
#endif
{
    // DDP JSON Status read (ID 251). Length is bytes requested, not payload size.
    uint8_t packet[DDP_HEADER_SIZE] = {};
    packet[0] = DDP_FLAGS1_VER1 | DDP_FLAGS1_QUERY;
    packet[1] = 0;
    packet[2] = 0;
    packet[3] = DDP_ID_STATUS;
    packet[8] = static_cast<uint8_t>((DDP_STATUS_QUERY_LEN >> 8) & 0xFF);
    packet[9] = static_cast<uint8_t>(DDP_STATUS_QUERY_LEN & 0xFF);

    sendto(sock,
           reinterpret_cast<const char*>(packet),
           DDP_HEADER_SIZE,
           0,
           reinterpret_cast<const struct sockaddr*>(&dest),
           sizeof(dest));
}

void DDPOutputCHOP::collectBroadcastTargets(const sockaddr_in& configuredDest, std::vector<sockaddr_in>& targets,
                                            std::vector<in_addr>& localUnicasts)
{
    auto addUniqueAddr = [](std::vector<in_addr>& list, in_addr addr)
    {
        if (addr.s_addr == 0)
            return;
        for (const in_addr& existing : list)
        {
            if (existing.s_addr == addr.s_addr)
                return;
        }
        list.push_back(addr);
    };
    auto addTarget = [&targets](in_addr addr)
    {
        if (addr.s_addr == 0)
            return;
        for (const sockaddr_in& existing : targets)
        {
            if (existing.sin_addr.s_addr == addr.s_addr)
                return;
        }
        sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = htons(DDP_PORT);
        dest.sin_addr = addr;
        targets.push_back(dest);
    };

    // Same-machine receivers (DDP In in this process). Broadcasts do not loop
    // back on Windows, so localhost has to be queried as unicast.
    in_addr loopback;
    loopback.s_addr = htonl(INADDR_LOOPBACK);
    addUniqueAddr(localUnicasts, loopback);
    addTarget(loopback);

    // Global broadcast (some networks block this; subnet broadcasts below cover most cases).
    in_addr globalBroadcast;
    globalBroadcast.s_addr = htonl(INADDR_BROADCAST);
    addTarget(globalBroadcast);

    #ifdef _WIN32
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG bufLen = 15000;
    std::vector<uint8_t> buffer(bufLen);
    PIP_ADAPTER_ADDRESSES adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &bufLen);
    if (result == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(bufLen);
        adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        result = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &bufLen);
    }
    if (result == NO_ERROR)
    {
        for (PIP_ADAPTER_ADDRESSES adapter = adapters; adapter; adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp)
                continue;
            for (PIP_ADAPTER_UNICAST_ADDRESS unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
            {
                if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                auto* sa = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                addUniqueAddr(localUnicasts, sa->sin_addr);
                ULONG prefix = unicast->OnLinkPrefixLength;
                if (prefix == 0 || prefix > 30)
                    prefix = 24;
                uint32_t ip = ntohl(sa->sin_addr.s_addr);
                uint32_t mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
                in_addr bcast;
                bcast.s_addr = htonl((ip & mask) | ~mask);
                addTarget(bcast);
            }
        }
    }
    #else
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == 0)
    {
        for (struct ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next)
        {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (!(ifa->ifa_flags & IFF_UP))
                continue;
            auto* sa = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
            addUniqueAddr(localUnicasts, sa->sin_addr);
            if (ifa->ifa_flags & IFF_LOOPBACK)
                continue;
            if (ifa->ifa_flags & IFF_BROADCAST && ifa->ifa_broadaddr)
            {
                auto* bcast = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
                addTarget(bcast->sin_addr);
            }
            else
            {
                uint32_t ip = ntohl(sa->sin_addr.s_addr);
                in_addr bcast;
                bcast.s_addr = htonl((ip & 0xFFFFFF00u) | 0xFFu);
                addTarget(bcast);
            }
        }
        freeifaddrs(ifaddr);
    }
    #endif

    // Also poke the configured controller IP directly.
    if (configuredDest.sin_addr.s_addr != 0)
        addTarget(configuredDest.sin_addr);
}

bool DDPOutputCHOP::parseStatusReply(const char* buffer, int length, const sockaddr_in& from, std::string& outInfo)
{
    if (!buffer || length < DDP_HEADER_SIZE)
        return false;

    const auto* bytes = reinterpret_cast<const uint8_t*>(buffer);
    uint8_t flags = bytes[0];
    uint8_t id = bytes[3];
    if ((flags & DDP_FLAGS1_REPLY) == 0)
        return false;
    if (id != DDP_ID_STATUS)
        return false;

    char ipStr[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &from.sin_addr, ipStr, INET_ADDRSTRLEN);
    outInfo = ipStr;

    uint16_t dataLen = static_cast<uint16_t>((bytes[8] << 8) | bytes[9]);
    int payloadStart = DDP_HEADER_SIZE;
    if (flags & DDP_FLAGS1_TIME)
        payloadStart += 4;
    if (dataLen > 0 && payloadStart + dataLen <= length)
    {
        std::string json(buffer + payloadStart, buffer + payloadStart + dataLen);
        auto extract = [&json](const char* key) -> std::string
        {
            std::string needle = std::string("\"") + key + "\"";
            size_t pos = json.find(needle);
            if (pos == std::string::npos)
                return {};
            pos = json.find(':', pos + needle.size());
            if (pos == std::string::npos)
                return {};
            pos = json.find('"', pos + 1);
            if (pos == std::string::npos)
                return {};
            size_t end = json.find('"', pos + 1);
            if (end == std::string::npos)
                return {};
            return json.substr(pos + 1, end - pos - 1);
        };

        std::string man = extract("man");
        std::string mod = extract("mod");
        if (!man.empty() || !mod.empty())
        {
            outInfo += " (";
            if (!man.empty())
                outInfo += man;
            if (!man.empty() && !mod.empty())
                outInfo += " ";
            if (!mod.empty())
                outInfo += mod;
            outInfo += ")";
        }
    }

    return true;
}

void DDPOutputCHOP::discoverDevices()
{
    if (m_discoveryThread.joinable())
        m_discoveryThread.join();

    m_isDiscovering = true;
    std::vector<std::string> devices;
    std::string status;
    runDiscovery(m_destAddr, devices, status);
    m_discoveredDevices = std::move(devices);
    m_lastError = status;
    m_isDiscovering = false;
}

void DDPOutputCHOP::startAsyncDiscovery()
{
    if (m_discoveryThread.joinable())
        m_discoveryThread.join();

    m_discoveryDone = false;
    m_isDiscovering = true;
    m_lastError = "Discovering devices...";
    sockaddr_in configuredDest = m_destAddr;
    m_discoveryThread = std::thread([this, configuredDest]()
    {
        std::vector<std::string> devices;
        std::string status;
        runDiscovery(configuredDest, devices, status);
        {
            std::lock_guard<std::mutex> lock(m_discoveryMutex);
            m_pendingDevices = std::move(devices);
            m_pendingDiscoveryStatus = std::move(status);
        }
        m_discoveryDone = true;
    });
}

bool DDPOutputCHOP::collectAsyncDiscovery()
{
    if (!m_discoveryDone)
        return false;

    if (m_discoveryThread.joinable())
        m_discoveryThread.join();
    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        m_discoveredDevices = std::move(m_pendingDevices);
        m_lastError = std::move(m_pendingDiscoveryStatus);
        m_pendingDevices.clear();
        m_pendingDiscoveryStatus.clear();
    }
    m_discoveryDone = false;
    m_isDiscovering = false;
    return true;
}

void DDPOutputCHOP::runDiscovery(const sockaddr_in& configuredDest, std::vector<std::string>& devices, std::string& status)
{
    devices.clear();

    // Discovery owns its own (refcounted) Winsock init and socket, so it can run
    // on a worker thread without touching the output socket.
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        status = "Discovery: WSAStartup failed";
        return;
    }
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        status = "Discovery: socket creation failed";
        WSACleanup();
        return;
    }
    BOOL broadcastEnable = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcastEnable, sizeof(broadcastEnable));
    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);
    #else
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        status = "Discovery: socket creation failed";
        return;
    }
    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    int sockFlags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, sockFlags | O_NONBLOCK);
    #endif

    sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(0);
    #ifdef _WIN32
    if (bind(sock, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) == SOCKET_ERROR)
    {
        status = "Discovery: bind failed";
        closesocket(sock);
        WSACleanup();
        return;
    }
    #else
    if (bind(sock, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) < 0)
    {
        status = "Discovery: bind failed";
        close(sock);
        return;
    }
    #endif

    std::vector<sockaddr_in> targets;
    std::vector<in_addr> localUnicasts;
    collectBroadcastTargets(configuredDest, targets, localUnicasts);

    auto sendAll = [this, sock, &targets]()
    {
        for (const sockaddr_in& dest : targets)
            sendDiscoveryQuery(sock, dest);
    };

    // A few spaced queries; devices may delay replies by MAC (spec ~500 ms window).
    sendAll();
    #ifdef _WIN32
        Sleep(40);
    #else
        usleep(40000);
    #endif
    sendAll();
    #ifdef _WIN32
        Sleep(40);
    #else
        usleep(40000);
    #endif
    sendAll();

    char buffer[2048];
    sockaddr_in responseAddr;
    int packetsReceived = 0;
    const int polls = DDP_DISCOVERY_WAIT_MS / 10;
    for (int i = 0; i < polls; i++)
    {
        #ifdef _WIN32
        int addrLen = sizeof(responseAddr);
        #else
        socklen_t addrLen = sizeof(responseAddr);
        #endif

        while (true)
        {
            #ifdef _WIN32
            addrLen = sizeof(responseAddr);
            #else
            addrLen = sizeof(responseAddr);
            #endif
            int received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                    reinterpret_cast<sockaddr*>(&responseAddr), &addrLen);
            if (received <= 0)
                break;

            packetsReceived++;
            std::string deviceInfo;
            if (!parseStatusReply(buffer, received, responseAddr, deviceInfo))
                continue;

            size_t ipEnd = deviceInfo.find_first_of(" :");
            std::string ipOnly = deviceInfo.substr(0, ipEnd == std::string::npos ? deviceInfo.size() : ipEnd);

            bool found = false;
            for (const auto& dev : devices)
            {
                size_t existingEnd = dev.find_first_of(" :");
                std::string existingIp = dev.substr(0, existingEnd == std::string::npos ? dev.size() : existingEnd);
                if (existingIp == ipOnly)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                devices.push_back(deviceInfo);
        }

        #ifdef _WIN32
            Sleep(10);
        #else
            usleep(10000);
        #endif
    }

    #ifdef _WIN32
        closesocket(sock);
        WSACleanup();
    #else
        close(sock);
    #endif

    // 127.0.0.1 and this machine's LAN address are the same receiver. Keep localhost.
    auto isLocalIp = [&localUnicasts](const std::string& ip) -> bool
    {
        in_addr addr;
        if (inet_pton(AF_INET, ip.c_str(), &addr) != 1)
            return false;
        for (const in_addr& local : localUnicasts)
        {
            if (local.s_addr == addr.s_addr)
                return true;
        }
        return false;
    };
    {
        std::vector<std::string> collapsed;
        bool keptLocal = false;
        for (const std::string& device : devices)
        {
            size_t ipEnd = device.find_first_of(" :");
            std::string ipOnly = device.substr(0, ipEnd == std::string::npos ? device.size() : ipEnd);
            if (!isLocalIp(ipOnly))
            {
                collapsed.push_back(device);
                continue;
            }
            if (keptLocal)
                continue;
            keptLocal = true;
            std::string rest = ipEnd == std::string::npos ? std::string() : device.substr(ipEnd);
            collapsed.push_back("127.0.0.1" + rest);
        }
        devices.swap(collapsed);
    }

    if (devices.empty())
    {
        status = "Discovery complete: 0 devices (" + std::to_string(packetsReceived)
            + " packet(s)). Controllers must answer DDP JSON status (ID 251). "
              "WLED often does not. Check subnet/firewall, or enter IPs in the routing table.";
    }
    else
    {
        status = "Discovery complete: " + std::to_string(devices.size())
            + " device(s) found (" + std::to_string(packetsReceived) + " packet(s))";
    }
}

