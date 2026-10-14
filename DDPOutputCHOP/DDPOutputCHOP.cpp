#include "DDPOutputCHOP.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <errno.h>
#ifdef _WIN32
    #include <psapi.h>
#else
    #include <dlfcn.h>
#endif

namespace
{

// Python functions are looked up in the already-loaded TouchDesigner runtime instead of
// linking against Python, so the plugin isn't tied to one Python version.
// All of these are part of CPython's stable ABI.
constexpr int kPyFileInput = 257;

struct PythonApi
{
    const char* missing = nullptr;
    int (*GILEnsure)() = nullptr;
    void (*GILRelease)(int) = nullptr;
    PyObject* (*DictNew)() = nullptr;
    int (*DictSetItemString)(PyObject*, const char*, PyObject*) = nullptr;
    PyObject* (*ListNew)(intptr_t) = nullptr;
    int (*ListSetItem)(PyObject*, intptr_t, PyObject*) = nullptr;
    PyObject* (*TupleGetItem)(PyObject*, intptr_t) = nullptr;
    PyObject* (*LongFromLong)(long) = nullptr;
    PyObject* (*UnicodeFromString)(const char*) = nullptr;
    const char* (*UnicodeAsUTF8)(PyObject*) = nullptr;
    PyObject* (*ObjectStr)(PyObject*) = nullptr;
    PyObject* (*EvalGetBuiltins)() = nullptr;
    PyObject* (*RunStringFlags)(const char*, int, PyObject*, PyObject*, void*) = nullptr;
    void (*ErrFetch)(PyObject**, PyObject**, PyObject**) = nullptr;
    void (*ErrClear)() = nullptr;
    void (*DecRef)(PyObject*) = nullptr;
};

struct GilGuard
{
    PythonApi& py;
    int state;
    explicit GilGuard(PythonApi& api) : py(api), state(api.GILEnsure()) {}
    ~GilGuard() { py.GILRelease(state); }
};

void* findPythonSymbol(const char* name)
{
#ifdef _WIN32
    static HMODULE python = nullptr;
    if (!python)
        python = GetModuleHandleA("python3.dll");
    for (int minor = 30; !python && minor >= 6; minor--)
        python = GetModuleHandleA(("python3" + std::to_string(minor) + ".dll").c_str());

    // TouchDesigner may host Python in a module whose filename doesn't follow
    // CPython's usual python3xx.dll convention. macOS dlsym searches every
    // loaded image, so do the equivalent on Windows before reporting failure.
    if (!python)
    {
        HMODULE modules[1024];
        DWORD bytesNeeded = 0;
        if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytesNeeded))
        {
            const size_t count = std::min(
                static_cast<size_t>(bytesNeeded / sizeof(HMODULE)),
                sizeof(modules) / sizeof(modules[0]));
            for (size_t i = 0; i < count; i++)
            {
                if (GetProcAddress(modules[i], "PyGILState_Ensure"))
                {
                    python = modules[i];
                    break;
                }
            }
        }
    }
    return python ? reinterpret_cast<void*>(GetProcAddress(python, name)) : nullptr;
#else
    return dlsym(RTLD_DEFAULT, name);
#endif
}

bool loadPythonApi(PythonApi& py)
{
    auto bind = [&](const char* name) -> void* {
        void* symbol = findPythonSymbol(name);
        if (!symbol && !py.missing)
            py.missing = name;
        return symbol;
    };

    py.GILEnsure = reinterpret_cast<int (*)()>(bind("PyGILState_Ensure"));
    py.GILRelease = reinterpret_cast<void (*)(int)>(bind("PyGILState_Release"));
    py.DictNew = reinterpret_cast<PyObject* (*)()>(bind("PyDict_New"));
    py.DictSetItemString = reinterpret_cast<int (*)(PyObject*, const char*, PyObject*)>(bind("PyDict_SetItemString"));
    py.ListNew = reinterpret_cast<PyObject* (*)(intptr_t)>(bind("PyList_New"));
    py.ListSetItem = reinterpret_cast<int (*)(PyObject*, intptr_t, PyObject*)>(bind("PyList_SetItem"));
    py.TupleGetItem = reinterpret_cast<PyObject* (*)(PyObject*, intptr_t)>(bind("PyTuple_GetItem"));
    py.LongFromLong = reinterpret_cast<PyObject* (*)(long)>(bind("PyLong_FromLong"));
    py.UnicodeFromString = reinterpret_cast<PyObject* (*)(const char*)>(bind("PyUnicode_FromString"));
    // TouchDesigner's embedded Python on Windows does not always export this
    // convenience function. It is only used to format exception text, so it
    // must not prevent the routing-table script from running.
    py.UnicodeAsUTF8 = reinterpret_cast<const char* (*)(PyObject*)>(findPythonSymbol("PyUnicode_AsUTF8"));
    py.ObjectStr = reinterpret_cast<PyObject* (*)(PyObject*)>(bind("PyObject_Str"));
    py.EvalGetBuiltins = reinterpret_cast<PyObject* (*)()>(bind("PyEval_GetBuiltins"));
    // PyRun_String is a macro on Windows; PyRun_StringFlags is the exported
    // CPython API function. A null flags pointer has the same behavior.
    py.RunStringFlags = reinterpret_cast<PyObject* (*)(const char*, int, PyObject*, PyObject*, void*)>(
        bind("PyRun_StringFlags"));
    py.ErrFetch = reinterpret_cast<void (*)(PyObject**, PyObject**, PyObject**)>(bind("PyErr_Fetch"));
    py.ErrClear = reinterpret_cast<void (*)()>(bind("PyErr_Clear"));
    py.DecRef = reinterpret_cast<void (*)(PyObject*)>(bind("Py_DecRef"));
    return py.missing == nullptr;
}

std::string pythonExceptionText(PythonApi& py)
{
    if (!py.UnicodeAsUTF8)
    {
        py.ErrClear();
        return "Python script failed (exception text is unavailable)";
    }

    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* trace = nullptr;
    py.ErrFetch(&type, &value, &trace);

    std::string text;
    if (value)
    {
        PyObject* asStr = py.ObjectStr(value);
        if (asStr)
        {
            const char* utf = py.UnicodeAsUTF8(asStr);
            if (utf)
                text = utf;
            py.DecRef(asStr);
        }
        else
        {
            py.ErrClear();
        }
    }

    if (type)
        py.DecRef(type);
    if (value)
        py.DecRef(value);
    if (trace)
        py.DecRef(trace);
    return text.empty() ? std::string("Python error") : text;
}

// _ddp_node is the DDP Out operator. _ddp_devices is a list of IP strings.
// _ddp_defer is 1 when this is called from a cook. The DAT is built from a
// script string at the end of the frame, which is how TouchDesigner runs
// deferred operator creation. Existing rows are kept. The table is docked.
const char* kCreateRoutingTableScript = R"PY(
_body = """
node = args[0]
devices = list(args[1])
parent = node.parent()
if parent is None:
    raise RuntimeError('DDP Out has no network to place the routing table in')
wanted = node.name + '_routing'
assigned = None
try:
    assigned = node.par.Routingtable.eval()
except Exception:
    assigned = None
if isinstance(assigned, str):
    assigned = parent.op(assigned) if assigned else None
table = None
if assigned is not None:
    docked_to = getattr(assigned, 'dock', None)
    if docked_to is None or docked_to == node or assigned.name == wanted:
        table = assigned
if table is None:
    table = parent.op(wanted)
if table is None:
    try:
        table = parent.create('tableDAT', wanted)
    except Exception:
        import td
        table = parent.create(td.tableDAT, wanted)
    table.nodeX = node.nodeX
    table.nodeY = node.nodeY - 150
    table.clear()
if table.numRows == 0:
    table.appendRow(['ip', 'pixels', 'port', 'start', 'active'])
known = set()
if table.numRows > 1:
    for cell in table.col(0)[1:]:
        known.add(cell.val)
for ip in devices:
    if ip not in known:
        table.appendRow([ip, '', '', '', 1])
        known.add(ip)
table.dock = node
node.par.Routingtable = table
"""
if _ddp_defer:
    import td
    td.run(_body, _ddp_node, list(_ddp_devices), endFrame=True)
else:
    scope = dict(globals())
    scope['args'] = (_ddp_node, list(_ddp_devices))
    exec(_body, scope)
)PY";

// TouchDesigner creates this DAT with each new Custom OP instance. Calling
// through OP_Context is portable across its macOS and Windows Python hosts;
// unlike the CPython convenience APIs, it is part of the Custom OP SDK.
const char* kCallbacksStub = R"PY(# DDP Out callbacks

def onCreateRoutingTable(ddpOut):
    script = """
node = args[0]
parent = node.parent()
if parent is None:
    raise RuntimeError('DDP Out has no network to place the routing table in')
wanted = node.name + '_routing'
table = None
try:
    assigned = node.par.Routingtable.eval()
    if not isinstance(assigned, str):
        table = assigned
except Exception:
    pass
if table is None:
    table = parent.op(wanted)
if table is None:
    table = parent.create('tableDAT', wanted)
    table.nodeX = node.nodeX
    table.nodeY = node.nodeY - 150
    table.clear()
if table.numRows == 0:
    table.appendRow(['ip', 'pixels', 'port', 'start', 'active'])
table.dock = node
node.par.Routingtable = table
"""
    run(script, ddpOut, endFrame=True)
    return ddpOut
)PY";

std::string lowerCopy(const char* s)
{
    std::string out = s ? s : "";
    size_t begin = out.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();
    size_t end = out.find_last_not_of(" \t\r\n");
    out = out.substr(begin, end - begin + 1);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

const char* cellText(const OP_DATInput* table, int row, int col)
{
    if (!table || row < 0 || col < 0 || row >= table->numRows || col >= table->numCols)
        return "";
    const char* cell = table->getCell(row, col);
    return cell ? cell : "";
}

bool parseNonNegInt(const char* text, long& out)
{
    std::string trimmed = lowerCopy(text);
    if (trimmed.empty())
        return false;
    char* end = nullptr;
    long value = std::strtol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || value < 0)
        return false;
    out = value;
    return true;
}

bool isOff(const char* text)
{
    std::string value = lowerCopy(text);
    return value == "0" || value == "false" || value == "no" || value == "off";
}

enum class ColKind
{
    None,
    Ip,
    Pixels,
    Port,
    Start,
    Active
};

ColKind columnKind(const char* name)
{
    std::string n = lowerCopy(name);
    if (n == "ip" || n == "netaddress" || n == "address" || n == "host")
        return ColKind::Ip;
    if (n == "pixels" || n == "pixel" || n == "length" || n == "count" || n == "numpixels")
        return ColKind::Pixels;
    if (n == "port")
        return ColKind::Port;
    if (n == "start" || n == "offset" || n == "pixelstart")
        return ColKind::Start;
    if (n == "active" || n == "enable" || n == "enabled")
        return ColKind::Active;
    return ColKind::None;
}

} // namespace

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

        info->customOPInfo.pythonCallbacksDAT = kCallbacksStub;
        info->customOPInfo.minorVersion = 7;
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
    m_routeCount = 0;
    m_lastPort = 0;
    m_showStats = false;
    m_isDiscovering = false;
    m_routingTableAttempts = 0;
    m_routingTableWait = 0;
    m_lastFrameTime = 0.0;
    
    #ifdef _WIN32
        m_socket = INVALID_SOCKET;
        m_wsaInitialized = false;
    #else
        m_socket = -1;
    #endif
    
    memset(&m_destAddr, 0, sizeof(m_destAddr));
    memset(&m_broadcastAddr, 0, sizeof(m_broadcastAddr));

    // Same moment DMX Out creates its routing table: while the node is being
    // constructed, before it cooks. If the node is not in a network yet, the
    // first cooks schedule the same script for the end of the frame.
    createRoutingTable(false);
    if (!m_createError.empty())
        m_createError.clear();
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
    info->numChannels = 6;
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
        case 5:
            name->setString("routes");
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

    // Optional table DAT. Empty = send the whole input to IP Address.
    // Columns: ip, pixels, and optional port, start, active.
    {
        OP_StringParameter sp;
        sp.name = "Routingtable";
        sp.label = "Routing Table";
        sp.defaultValue = "";
        OP_ParAppendResult res = manager->appendDAT(sp);
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
    
    // Channels Per Pixel: RGB (3) or RGBW (4), used to turn routing pixel counts into bytes
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

    // Create or assign the routing Table DAT beside this node.
    {
        OP_NumericParameter np;
        np.name = "Createtable";
        np.label = "Create Routing Table";
        OP_ParAppendResult res = manager->appendPulse(np);
        assert(res == OP_ParAppendResult::Success);
    }
}

void DDPOutputCHOP::pulsePressed(const char* name, void* reserved1)
{
    if (strcmp(name, "Discover") == 0)
        discoverDevices();
    else if (strcmp(name, "Createtable") == 0)
        createRoutingTable(true);
}

void DDPOutputCHOP::getErrorString(OP_String* error, void* reserved1)
{
    if (!m_createError.empty())
        error->setString(m_createError.c_str());
}

void DDPOutputCHOP::createRoutingTable(bool defer)
{
    (void)defer;

    auto fail = [&](const std::string& message) {
        m_createError = message;
        m_lastError = message;
    };

    if (!myNodeInfo || !myNodeInfo->context)
    {
        fail("Create Routing Table: node is not available");
        return;
    }

    // The SDK fills item zero with this DDP Out operator.
    PyObject* args = myNodeInfo->context->createArgumentsTuple(0, nullptr);
    if (!args)
    {
        fail("Create Routing Table: could not reference this operator");
        return;
    }

    PyObject* result = myNodeInfo->context->callPythonCallback(
        "onCreateRoutingTable", args, nullptr, nullptr);

    // Reference cleanup is optional only for unusual embedded runtimes that
    // don't export Py_DecRef. At most a few retry objects are retained there.
    using DecRefFn = void (*)(PyObject*);
    DecRefFn decRef = reinterpret_cast<DecRefFn>(findPythonSymbol("Py_DecRef"));
    if (decRef)
        decRef(args);

    if (!result)
    {
        fail("Create Routing Table: callback failed (see Textport)");
        return;
    }

    if (decRef)
        decRef(result);
    m_createError.clear();
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
    
    // Byte 2: Data type (undefined; receivers treat the payload as raw bytes)
    packet[2] = DDP_DATA_TYPE_UNDEFINED;
    
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

void DDPOutputCHOP::sendDDPData(const uint8_t* pixelData, size_t totalBytes, const sockaddr_in& dest, bool autoPush)
{
    if (!m_socketInitialized || !pixelData || totalBytes == 0)
        return;
    
    size_t bytesSent = 0;
    
    while (bytesSent < totalBytes)
    {
        size_t bytesRemaining = totalBytes - bytesSent;
        size_t bytesInPacket = std::min(bytesRemaining, static_cast<size_t>(DDP_MAX_DATALEN));
        
        // Only push on last packet if auto-push is enabled.
        // Multi-device sends pass autoPush false and follow with a separate PUSH.
        bool isLastPacket = (bytesSent + bytesInPacket >= totalBytes);
        bool pushFlag = autoPush && isLastPacket;
        
        std::vector<uint8_t> packet;
        createDDPPacket(pixelData + bytesSent, bytesInPacket, bytesSent, pushFlag, packet);
        
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

void DDPOutputCHOP::sendPushPacketTo(const sockaddr_in& dest)
{
    if (!m_socketInitialized)
        return;
    
    // Create a PUSH packet with no data (just header)
    std::vector<uint8_t> packet(DDP_HEADER_SIZE);
    
    packet[0] = DDP_FLAGS1_VER1 | DDP_FLAGS1_PUSH;
    packet[1] = m_sequenceNumber & 0x0F;
    packet[2] = DDP_DATA_TYPE_UNDEFINED;
    packet[3] = DDP_ID_DISPLAY;
    packet[4] = 0;
    packet[5] = 0;
    packet[6] = 0;
    packet[7] = 0;
    packet[8] = 0;  // Zero length
    packet[9] = 0;
    
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
    
    m_sequenceNumber = (m_sequenceNumber + 1) & 0x0F;
}

uint8_t DDPOutputCHOP::floatToUint8(float value, bool normalizedInput)
{
    if (normalizedInput)
    {
        // Input is 0-1 range, convert to 0-255
        value = std::max(0.0f, std::min(1.0f, value));
        return static_cast<uint8_t>(value * 255.0f);
    }
    else
    {
        // Input is 0-255 range, just clamp and convert
        value = std::max(0.0f, std::min(255.0f, value));
        return static_cast<uint8_t>(value);
    }
}

float DDPOutputCHOP::applyGamma(float value, float gamma, bool normalizedInput)
{
    if (gamma == 1.0f)
        return value;
    
    // Normalize to 0-1 for gamma calculation if input is 0-255
    if (!normalizedInput)
        value = value / 255.0f;
    
    // Clamp input
    value = std::max(0.0f, std::min(1.0f, value));
    
    // Apply gamma correction
    value = std::pow(value, 1.0f / gamma);
    
    // Convert back to 0-255 if input was 0-255
    if (!normalizedInput)
        value = value * 255.0f;
    
    return value;
}

void DDPOutputCHOP::processInterleavedChannels(const OP_CHOPInput* chopInput, 
                                                 float gamma, float brightness,
                                                 bool normalizedInput,
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
        
        // Apply brightness (scale appropriately based on input range)
        if (normalizedInput)
            value *= brightness;
        else
            value *= brightness; // brightness is 0-1, so this scales 0-255 down proportionally
        
        // Apply gamma correction
        value = applyGamma(value, gamma, normalizedInput);
        
        // Convert to 8-bit
        pixelData.push_back(floatToUint8(value, normalizedInput));
    }
}

void DDPOutputCHOP::processSequentialChannels(const OP_CHOPInput* chopInput,
                                                float gamma, float brightness,
                                                bool normalizedInput,
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
            
            // Apply brightness (scale appropriately based on input range)
            if (normalizedInput)
                value *= brightness;
            else
                value *= brightness; // brightness is 0-1, so this scales 0-255 down proportionally
            
            // Apply gamma correction
            value = applyGamma(value, gamma, normalizedInput);
            
            // Convert to 8-bit
            pixelData.push_back(floatToUint8(value, normalizedInput));
        }
    }
}

bool DDPOutputCHOP::parseRoutingTable(const OP_Inputs* inputs, int channelsPerPixel, size_t dataBytes,
                                      std::vector<DeviceRoute>& routes)
{
    const OP_DATInput* table = inputs->getParDAT("Routingtable");
    if (!table || !table->isTable || table->numRows <= 0 || table->numCols <= 0)
        return false;

    int ipCol = 0;
    int pixCol = 1;
    int portCol = 2;
    int startCol = 3;
    int activeCol = -1;
    int firstRow = 0;
    bool hasHeader = false;

    for (int col = 0; col < table->numCols; col++)
    {
        if (columnKind(cellText(table, 0, col)) != ColKind::None)
        {
            hasHeader = true;
            break;
        }
    }

    if (hasHeader)
    {
        ipCol = -1;
        pixCol = -1;
        portCol = -1;
        startCol = -1;
        for (int col = 0; col < table->numCols; col++)
        {
            switch (columnKind(cellText(table, 0, col)))
            {
                case ColKind::Ip: ipCol = col; break;
                case ColKind::Pixels: pixCol = col; break;
                case ColKind::Port: portCol = col; break;
                case ColKind::Start: startCol = col; break;
                case ColKind::Active: activeCol = col; break;
                default: break;
            }
        }
        firstRow = 1;
        if (ipCol < 0)
        {
            m_lastError = "Routing table needs an ip column";
            return true;
        }
        // Header only: keep sending the whole input to IP Address.
        if (firstRow >= table->numRows)
            return false;
    }
    else if (table->numCols < 2)
    {
        pixCol = -1;
        portCol = -1;
        startCol = -1;
    }
    else if (table->numCols < 3)
    {
        portCol = -1;
        startCol = -1;
    }
    else if (table->numCols < 4)
    {
        startCol = -1;
    }

    int defaultPort = inputs->getParInt("Port");
    if (defaultPort < 1 || defaultPort > 65535)
        defaultPort = DDP_PORT;

    size_t cpp = static_cast<size_t>(std::max(1, channelsPerPixel));
    size_t totalPixels = dataBytes / cpp;
    size_t cursor = 0;
    std::string problems;

    for (int row = firstRow; row < table->numRows; row++)
    {
        const char* rawIp = cellText(table, row, ipCol);
        size_t begin = 0;
        while (rawIp[begin] == ' ' || rawIp[begin] == '\t')
            begin++;
        size_t end = std::strlen(rawIp);
        while (end > begin && (rawIp[end - 1] == ' ' || rawIp[end - 1] == '\t' || rawIp[end - 1] == '\r' || rawIp[end - 1] == '\n'))
            end--;
        std::string ip(rawIp + begin, end - begin);
        if (ip.empty())
            continue;
        if (activeCol >= 0 && isOff(cellText(table, row, activeCol)))
            continue;

        DeviceRoute route;
        memset(&route.addr, 0, sizeof(route.addr));
        route.addr.sin_family = AF_INET;
        route.byteOffset = 0;
        route.byteCount = 0;

        if (inet_pton(AF_INET, ip.c_str(), &route.addr.sin_addr) != 1)
        {
            if (!problems.empty())
                problems += "; ";
            problems += "bad ip " + ip;
            continue;
        }

        int port = defaultPort;
        long parsedPort = 0;
        if (portCol >= 0 && parseNonNegInt(cellText(table, row, portCol), parsedPort) &&
            parsedPort >= 1 && parsedPort <= 65535)
        {
            port = static_cast<int>(parsedPort);
        }
        route.addr.sin_port = htons(static_cast<uint16_t>(port));

        bool hasStart = false;
        long startPix = 0;
        if (startCol >= 0 && parseNonNegInt(cellText(table, row, startCol), startPix))
            hasStart = true;

        bool hasCount = false;
        long countPix = 0;
        if (pixCol >= 0 && parseNonNegInt(cellText(table, row, pixCol), countPix) && countPix > 0)
            hasCount = true;

        // No pixels column means every row gets the same stream (or from `start` to the end).
        // Otherwise rows consume the stream in order, unless `start` is set.
        size_t start = 0;
        if (hasStart)
            start = static_cast<size_t>(startPix);
        else if (pixCol >= 0)
            start = cursor;

        size_t count = 0;
        if (hasCount)
            count = static_cast<size_t>(countPix);
        else if (start < totalPixels)
            count = totalPixels - start;

        if (start >= totalPixels || count == 0)
        {
            cursor = start;
            continue;
        }
        if (start + count > totalPixels)
            count = totalPixels - start;

        route.byteOffset = start * cpp;
        route.byteCount = count * cpp;
        routes.push_back(route);
        cursor = start + count;
    }

    if (!problems.empty())
        m_lastError = "Routing table: " + problems;
    else if (routes.empty())
        m_lastError = "Routing table has no devices";
    else if (m_lastError.rfind("Routing table", 0) == 0)
        m_lastError.clear();

    return true;
}

void DDPOutputCHOP::execute(CHOP_Output* output, const OP_Inputs* inputs, void* reserved1)
{
    // Creating a DAT during this cook is not allowed, so schedule it for the
    // end of the frame. That still runs while the timeline is stopped.
    // Retry a few times in case the node was not in the network yet.
    if (!inputs->getParDAT("Routingtable") && m_routingTableAttempts < 8)
    {
        if (m_routingTableWait <= 0)
        {
            m_routingTableAttempts++;
            m_routingTableWait = 3;
            createRoutingTable(true);
        }
        else
        {
            m_routingTableWait--;
        }
    }

    // Get parameters
    const char* ipAddress = inputs->getParString("Ipaddress");
    int port = inputs->getParInt("Port");
    bool enabled = inputs->getParInt("Enable") != 0;
    float gamma = static_cast<float>(inputs->getParDouble("Gamma"));
    float brightness = static_cast<float>(inputs->getParDouble("Brightness"));
    const char* channelsPerPixelName = inputs->getParString("Channelsperpixel");
    int channelsPerPixel = (channelsPerPixelName && std::strcmp(channelsPerPixelName, "4") == 0) ? 4 : 3;
    bool autoPush = inputs->getParInt("Autopush") != 0;
    bool showStats = inputs->getParInt("Showstats") != 0;
    double maxFPS = inputs->getParDouble("Maxfps");
    const char* valueRange = inputs->getParString("Valuerange");
    bool normalizedInput = (strcmp(valueRange, "0-1") == 0);
    
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
    output->channels[5][0] = static_cast<float>(m_routeCount);
    
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
    processInterleavedChannels(chopInput, gamma, brightness, normalizedInput, pixelData);
    
    // Update channel and pixel counts
    m_lastChannelCount = static_cast<int32_t>(pixelData.size());
    m_lastPixelCount = (channelsPerPixel > 0) ? (m_lastChannelCount / channelsPerPixel) : 0;
    
    // Send DDP packets if we have data.
    // A routing table slices the stream across controllers. With no table,
    // the whole input still goes to IP Address.
    if (!pixelData.empty())
    {
        std::vector<DeviceRoute> routes;
        bool usingTable = parseRoutingTable(inputs, channelsPerPixel, pixelData.size(), routes);
        if (usingTable)
        {
            m_routes = routes;
            m_routeCount = static_cast<int32_t>(routes.size());
            bool multi = routes.size() > 1;
            for (const DeviceRoute& route : routes)
                sendDDPData(pixelData.data() + route.byteOffset, route.byteCount, route.addr, autoPush && !multi);

            // Latch every controller together after the data is buffered.
            if (autoPush && multi)
            {
                for (const DeviceRoute& route : routes)
                    sendPushPacketTo(route.addr);
            }
        }
        else
        {
            m_routes.clear();
            m_routeCount = 1;
            sendDDPData(pixelData.data(), pixelData.size(), m_destAddr, autoPush);
        }
    }

    output->channels[1][0] = static_cast<float>(m_packetsSent);
    output->channels[2][0] = static_cast<float>(m_bytesSent / 1024.0);
    output->channels[3][0] = static_cast<float>(m_lastChannelCount);
    output->channels[4][0] = static_cast<float>(m_lastPixelCount);
    output->channels[5][0] = static_cast<float>(m_routeCount);
}

int32_t DDPOutputCHOP::getNumInfoCHOPChans(void* reserved1)
{
    return 5;
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
        case 4:
            chan->name->setString("routes");
            chan->value = static_cast<float>(m_routeCount);
            break;
    }
}

bool DDPOutputCHOP::getInfoDATSize(OP_InfoDATSize* infoSize, void* reserved1)
{
    infoSize->rows = 8 + static_cast<int32_t>(m_discoveredDevices.size());
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
        entries->values[0]->setString("Routes");
        entries->values[1]->setString(std::to_string(m_routeCount).c_str());
    }
    else if (index == 5)
    {
        entries->values[0]->setString("IP Address");
        entries->values[1]->setString(m_lastIPAddress.c_str());
    }
    else if (index == 6)
    {
        entries->values[0]->setString("Last Error");
        entries->values[1]->setString(m_lastError.c_str());
    }
    else if (index == 7)
    {
        entries->values[0]->setString("Devices Found");
        entries->values[1]->setString(std::to_string(m_discoveredDevices.size()).c_str());
    }
    else if (index >= 8 && index < 8 + static_cast<int32_t>(m_discoveredDevices.size()))
    {
        int deviceIdx = index - 8;
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
    packet[2] = DDP_DATA_TYPE_UNDEFINED;  // Data type (ignored for query)
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

