#include "DDPOutputCHOP.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <errno.h>
#ifndef _WIN32
    #include <dlfcn.h>
#endif

namespace
{

// Python functions are looked up in the already-loaded TouchDesigner runtime instead of
// linking against Python, so the plugin isn't tied to one Python version.
// All of these are part of CPython's stable ABI.
void* findPythonSymbol(const char* name)
{
#ifdef _WIN32
    static HMODULE python = nullptr;
    if (!python)
        python = GetModuleHandleA("python3.dll");
    for (int minor = 30; !python && minor >= 6; minor--)
        python = GetModuleHandleA(("python3" + std::to_string(minor) + ".dll").c_str());
    return python ? reinterpret_cast<void*>(GetProcAddress(python, name)) : nullptr;
#else
    return dlsym(RTLD_DEFAULT, name);
#endif
}

// Py_DecRef may be missing on some TouchDesigner Python builds; skip the release then.
void pythonDecRef(PyObject* obj)
{
    using DecRefFn = void (*)(PyObject*);
    DecRefFn decRef = reinterpret_cast<DecRefFn>(findPythonSymbol("Py_DecRef"));
    if (decRef && obj)
        decRef(obj);
}

// Runs Python source in TouchDesigner's interpreter via builtins.exec, using only
// stable-ABI functions (python3.dll on Windows). Errors are printed to the Textport.
// Returns false if the functions couldn't be found or the code raised.
bool execPython(const std::string& code)
{
    using GILEnsureFn = int (*)();
    using GILReleaseFn = void (*)(int);
    using ImportFn = PyObject* (*)(const char*);
    using GetAttrFn = PyObject* (*)(PyObject*, const char*);
    using FromStringFn = PyObject* (*)(const char*);
    using DictNewFn = PyObject* (*)();
    using CallArgsFn = PyObject* (*)(PyObject*, ...);
    using ErrOccurredFn = PyObject* (*)();
    using ErrPrintFn = void (*)();

    auto gilEnsure = reinterpret_cast<GILEnsureFn>(findPythonSymbol("PyGILState_Ensure"));
    auto gilRelease = reinterpret_cast<GILReleaseFn>(findPythonSymbol("PyGILState_Release"));
    auto importModule = reinterpret_cast<ImportFn>(findPythonSymbol("PyImport_ImportModule"));
    auto getAttr = reinterpret_cast<GetAttrFn>(findPythonSymbol("PyObject_GetAttrString"));
    auto fromString = reinterpret_cast<FromStringFn>(findPythonSymbol("PyUnicode_FromString"));
    auto dictNew = reinterpret_cast<DictNewFn>(findPythonSymbol("PyDict_New"));
    auto callArgs = reinterpret_cast<CallArgsFn>(findPythonSymbol("PyObject_CallFunctionObjArgs"));
    auto errOccurred = reinterpret_cast<ErrOccurredFn>(findPythonSymbol("PyErr_Occurred"));
    auto errPrint = reinterpret_cast<ErrPrintFn>(findPythonSymbol("PyErr_Print"));
    if (!gilEnsure || !gilRelease || !importModule || !getAttr || !fromString || !dictNew || !callArgs)
        return false;

    int gil = gilEnsure();
    PyObject* builtins = importModule("builtins");
    PyObject* execFn = builtins ? getAttr(builtins, "exec") : nullptr;
    PyObject* source = fromString(code.c_str());
    PyObject* globals = dictNew();
    PyObject* result = (execFn && source && globals)
        ? callArgs(execFn, source, globals, static_cast<PyObject*>(nullptr))
        : nullptr;
    bool ok = result != nullptr;
    if (!ok && errOccurred && errPrint && errOccurred())
        errPrint();
    pythonDecRef(result);
    pythonDecRef(globals);
    pythonDecRef(source);
    pythonDecRef(execFn);
    pythonDecRef(builtins);
    gilRelease(gil);
    return ok;
}

std::string pyQuote(const std::string& text)
{
    std::string out = "'";
    for (char c : text)
    {
        if (c == '\\' || c == '\'')
        {
            out += '\\';
            out += c;
        }
        else if (c == '\n')
            out += "\\n";
        else
            out += c;
    }
    return out + "'";
}

std::string pyStringList(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (size_t i = 0; i < items.size(); i++)
    {
        if (i > 0)
            out += ", ";
        out += pyQuote(items[i]);
    }
    return out + "]";
}

// Updates the assigned Routing Table (or creates <name>_routing): new IPs fill
// rows with an empty ip cell first, then get appended. IPs already in the table
// are left alone.
#define DDP_UPDATE_TABLE_PY \
    "def _updateRoutingTable(o, ips):\n" \
    "    import td\n" \
    "    t = o.par.Routingtable.eval()\n" \
    "    if t is None or not t.isTable:\n" \
    "        name = o.name + '_routing'\n" \
    "        t = o.parent().op(name)\n" \
    "        if t is None:\n" \
    "            t = o.parent().create(td.tableDAT, name)\n" \
    "            t.nodeX = o.nodeX\n" \
    "            t.nodeY = o.nodeY - 200\n" \
    "            t.clear()\n" \
    "    if t.numRows == 0:\n" \
    "        t.appendRow(['ip', 'pixels', 'start', 'active'])\n" \
    "    head = [c.val.strip().lower() for c in t.row(0)]\n" \
    "    ipNames = ('ip', 'netaddress', 'address', 'host')\n" \
    "    hasHeader = any(h in ipNames for h in head)\n" \
    "    ipc = next((i for i, h in enumerate(head) if h in ipNames), 0)\n" \
    "    ac = next((i for i, h in enumerate(head) if h in ('active', 'enable', 'enabled')), -1) if hasHeader else -1\n" \
    "    rows = range(1 if hasHeader else 0, t.numRows)\n" \
    "    known = set(t[r, ipc].val.strip() for r in rows)\n" \
    "    blanks = [r for r in rows if not t[r, ipc].val.strip()]\n" \
    "    for ip in ips:\n" \
    "        if not ip or ip in known:\n" \
    "            continue\n" \
    "        known.add(ip)\n" \
    "        if blanks:\n" \
    "            r = blanks.pop(0)\n" \
    "            t[r, ipc] = ip\n" \
    "            if ac >= 0 and not t[r, ac].val.strip():\n" \
    "                t[r, ac] = 1\n" \
    "        else:\n" \
    "            row = [''] * max(t.numCols, ipc + 1)\n" \
    "            row[ipc] = ip\n" \
    "            if ac >= 0:\n" \
    "                row[ac] = 1\n" \
    "            t.appendRow(row)\n" \
    "    o.par.Routingtable = t\n" \
    "    return t\n"

// Creates <name>_info (an Info DAT watching the node) unless one by that name
// already exists. The caller docks it to the DDP Out.
#define DDP_CREATE_INFO_PY \
    "def _createInfoDAT(o):\n" \
    "    import td\n" \
    "    name = o.name + '_info'\n" \
    "    i = o.parent().op(name)\n" \
    "    if i is None:\n" \
    "        i = o.parent().create(td.infoDAT, name)\n" \
    "        i.nodeX = o.nodeX + 200\n" \
    "        i.nodeY = o.nodeY - 200\n" \
    "    i.par.op = o\n" \
    "    return i\n"

// Run one frame later via td.run(). args[0] = DDP Out path, args[1] = list of IPs,
// args[2] = also create the Info DAT. Companion DATs named <chop>_routing and
// <chop>_info are docked to the CHOP; a routing table from elsewhere is left put.
const char* kBuildTableScript =
    DDP_UPDATE_TABLE_PY
    DDP_CREATE_INFO_PY
    "def _dockToNode(child, parent):\n"
    "    if child is None or child.parent() is not parent.parent():\n"
    "        return\n"
    "    child.dock = parent\n"
    "    child.showDocked = True\n"
    "o = op(args[0])\n"
    "if o is not None:\n"
    "    t = _updateRoutingTable(o, args[1])\n"
    "    i = _createInfoDAT(o) if args[2] else o.parent().op(o.name + '_info')\n"
    "    if t is not None and t.name == o.name + '_routing':\n"
    "        _dockToNode(t, o)\n"
    "    _dockToNode(i, o)\n";

// onlyOnce: skip if this node already auto-created a table (flag in its storage),
// so reopening a project never recreates one. The auto-create path also adds
// the Info DAT. Either path docks the companion DATs onto the CHOP.
std::string buildRoutingTableExec(const std::string& opPath, const std::vector<std::string>& ips, bool onlyOnce)
{
    std::string code = "import td\n"
                       "_o = td.op(" + pyQuote(opPath) + ")\n"
                       "if _o is not None";
    if (onlyOnce)
        code += " and not _o.fetch('ddpRoutingInit', False, search=False)";
    code += ":\n";
    if (onlyOnce)
        code += "    _o.store('ddpRoutingInit', True)\n";
    code += "    td.run(" + pyQuote(kBuildTableScript) + ", _o.path, " + pyStringList(ips) + ", "
          + (onlyOnce ? "True" : "False") + ", delayFrames=1)\n";
    return code;
}

std::vector<std::string> ipsOnly(const std::vector<std::string>& entries)
{
    // Entries may be "ip", "ip:port", or "ip (man mod)".
    std::vector<std::string> ips;
    for (const std::string& entry : entries)
    {
        size_t end = entry.find_first_of(" :");
        ips.push_back(entry.substr(0, end == std::string::npos ? entry.size() : end));
    }
    return ips;
}

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

// Menu names are "3" / "4". getParInt on a menu returns the index (0/1), while
// older builds stored the real channel count as an int (3/4). Accept both.
int channelsPerPixelFromInputs(const OP_Inputs* inputs)
{
    const char* text = inputs->getParString("Channelsperpixel");
    if (text && text[0])
    {
        if (std::strcmp(text, "4") == 0)
            return 4;
        if (std::strcmp(text, "3") == 0)
            return 3;
    }

    int value = inputs->getParInt("Channelsperpixel");
    if (value == 4 || value == 1)
        return 4;
    return 3;
}

enum class ColKind
{
    None,
    Ip,
    Pixels,
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
        // 0, not 1: a node with no input wired must still cook, or the
        // auto-create step for a freshly placed node never runs.
        info->customOPInfo.minInputs = 0;
        info->customOPInfo.maxInputs = 1;
        
        // Ensure the node cooks on startup (important for output nodes)
        info->customOPInfo.cookOnStart = true;

        // No Callbacks DAT parameter. Routing-table updates run the built-in script.
        // Bump minorVersion so existing nodes drop removed parameters
        // (Callbacks DAT, Port, Auto Push, Discover).
        info->customOPInfo.pythonCallbacksDAT = nullptr;
        info->customOPInfo.minorVersion = 6;
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
    m_dataType = DDP_DATA_TYPE_UNDEFINED;
    m_packetsSent = 0;
    m_bytesSent = 0;
    m_lastChannelCount = 0;
    m_lastPixelCount = 0;
    m_routeCount = 0;
    m_showStats = false;
    m_companionsDocked = false;
    m_isDiscovering = false;
    m_lastFrameTime = 0.0;
    m_lastSendError = 0;
    m_hasPendingFrame = false;
    m_stopSender = false;
    m_framesDropped = 0;
    m_frameIntervalSec = 1.0 / 60.0;
    m_discoveryDone = false;
    m_pendingTableUpdate = false;
    m_autoCreateState = AutoCreateState::Start;
    m_autoCreateStatus = "Not started (node hasn't cooked yet)";
    
    #ifdef _WIN32
        m_socket = INVALID_SOCKET;
        m_wsaInitialized = false;
    #else
        m_socket = -1;
    #endif
    
    memset(&m_destAddr, 0, sizeof(m_destAddr));
    memset(&m_broadcastAddr, 0, sizeof(m_broadcastAddr));

    m_senderThread = std::thread(&DDPOutputCHOP::senderLoop, this);
}

DDPOutputCHOP::~DDPOutputCHOP()
{
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_stopSender = true;
    }
    m_frameCv.notify_all();
    if (m_senderThread.joinable())
        m_senderThread.join();

    // Discovery finishes on its own within ~600 ms.
    if (m_discoveryThread.joinable())
        m_discoveryThread.join();

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
    
    // Optional table DAT. Empty = send the whole input to IP Address.
    // Columns: ip, pixels, and optional start, active.
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
    
    // Channels Per Pixel: 3 = RGB, 4 = RGBW (same parameter name as the old int slider)
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

    // Discover & Update Routing Table Button. Name kept as "Createtable" so
    // existing projects and scripts that pulse it keep working.
    {
        OP_NumericParameter np;
        np.name = "Createtable";
        np.label = "Discover & Update Routing Table";
        OP_ParAppendResult res = manager->appendPulse(np);
        assert(res == OP_ParAppendResult::Success);
    }
}

void DDPOutputCHOP::pulsePressed(const char* name, void* reserved1)
{
    if (strcmp(name, "Createtable") == 0)
    {
        createRoutingTable();
    }
}

void DDPOutputCHOP::createRoutingTable()
{
    // Must not block the cook thread: DDP In on this same process only replies
    // when it cooks, so a synchronous wait would miss local devices.
    m_pendingTableUpdate = true;
    const bool inFlight = m_discoveryThread.joinable() || m_discoveryDone;
    if (!inFlight)
        startAsyncDiscovery();
    m_lastError = "Discovering devices...";
}

void DDPOutputCHOP::finishDiscoveryIfReady()
{
    if (!collectAsyncDiscovery())
        return;

    if (m_autoCreateState == AutoCreateState::Discovering)
        m_autoCreateState = AutoCreateState::BuildTable;

    if (!m_pendingTableUpdate)
        return;

    m_pendingTableUpdate = false;
    std::string code = buildRoutingTableExec(myNodeInfo->opPath, ipsOnly(m_discoveredDevices), false);
    if (execPython(code))
        m_lastError = "Routing table updated with " + std::to_string(m_discoveredDevices.size()) + " discovered device(s)";
    else
        m_lastError = "Discover & Update Routing Table: could not run Python (see Textport)";
}

void DDPOutputCHOP::dockCompanionDATs()
{
    if (m_companionsDocked || !myNodeInfo)
        return;

    // Companion DATs from an earlier session are already in the network.
    // Ones created later are docked by the table script itself.
    std::string code =
        "import td\n"
        "_o = td.op(" + pyQuote(myNodeInfo->opPath) + ")\n"
        "if _o is not None:\n"
        "    for _suffix in ('_routing', '_info'):\n"
        "        _d = _o.parent().op(_o.name + _suffix)\n"
        "        if _d is not None and _d.parent() is _o.parent():\n"
        "            _d.dock = _o\n"
        "            _d.showDocked = True\n";
    if (execPython(code))
        m_companionsDocked = true;
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

    // Headerless tables: column 2 is skipped (it used to be port) so older
    // tables keep start in column 3.
    int ipCol = 0;
    int pixCol = 1;
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
        startCol = -1;
        for (int col = 0; col < table->numCols; col++)
        {
            switch (columnKind(cellText(table, 0, col)))
            {
                case ColKind::Ip: ipCol = col; break;
                case ColKind::Pixels: pixCol = col; break;
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
    }
    else if (table->numCols < 2)
    {
        pixCol = -1;
        startCol = -1;
    }
    else if (table->numCols < 4)
    {
        startCol = -1;
    }

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

        route.addr.sin_port = htons(DDP_PORT);

        bool hasStart = false;
        long startPix = 0;
        if (startCol >= 0 && parseNonNegInt(cellText(table, row, startCol), startPix))
            hasStart = true;

        bool hasCount = false;
        long countPix = 0;
        // 0 is a real count (send nothing). Only a blank cell means "the rest".
        if (pixCol >= 0 && parseNonNegInt(cellText(table, row, pixCol), countPix))
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

void DDPOutputCHOP::updateAutoCreate(const OP_Inputs* inputs)
{
    switch (m_autoCreateState)
    {
        case AutoCreateState::Start:
        {
            // Only a freshly placed node looks like this: no table, no input, default IP.
            // A configured node in an existing project fails at least one check.
            const char* tablePath = inputs->getParString("Routingtable");
            const char* ip = inputs->getParString("Ipaddress");
            if (tablePath && tablePath[0])
            {
                m_autoCreateState = AutoCreateState::Done;
                m_autoCreateStatus = std::string("Skipped: Routing Table already set (") + tablePath + ")";
                break;
            }
            if (inputs->getInputCHOP(0))
            {
                m_autoCreateState = AutoCreateState::Done;
                m_autoCreateStatus = "Skipped: input already connected (existing node)";
                break;
            }
            if (!ip || std::strcmp(ip, "127.0.0.1") != 0)
            {
                m_autoCreateState = AutoCreateState::Done;
                m_autoCreateStatus = "Skipped: IP Address already changed (existing node)";
                break;
            }
            startAsyncDiscovery();
            m_autoCreateState = AutoCreateState::Discovering;
            m_autoCreateStatus = "Discovering devices";
            break;
        }
        case AutoCreateState::Discovering:
            break;
        case AutoCreateState::BuildTable:
        {
            m_autoCreateState = AutoCreateState::Done;
            std::string code = buildRoutingTableExec(myNodeInfo->opPath, ipsOnly(m_discoveredDevices), true);
            if (execPython(code))
                m_autoCreateStatus = "Routing table and Info DAT requested with " + std::to_string(m_discoveredDevices.size())
                    + " discovered device(s) (skipped if this node already did it once)";
            else
                m_autoCreateStatus = "Could not run Python to create the table (see Textport)";
            break;
        }
        case AutoCreateState::Done:
            break;
    }
}

void DDPOutputCHOP::execute(CHOP_Output* output, const OP_Inputs* inputs, void* reserved1)
{
    // Get parameters
    const char* ipAddress = inputs->getParString("Ipaddress");
    bool enabled = inputs->getParInt("Enable") != 0;
    float gamma = static_cast<float>(inputs->getParDouble("Gamma"));
    float brightness = static_cast<float>(inputs->getParDouble("Brightness"));
    int channelsPerPixel = channelsPerPixelFromInputs(inputs);
    bool showStats = inputs->getParInt("Showstats") != 0;
    double maxFPS = inputs->getParDouble("Maxfps");
    const char* valueRange = inputs->getParString("Valuerange");
    bool normalizedInput = (strcmp(valueRange, "0-1") == 0);
    m_dataType = DDP_DATA_TYPE_UNDEFINED;
    
    // Reset stats when toggling
    if (showStats != m_showStats)
    {
        m_packetsSent = 0;
        m_bytesSent = 0;
    }
    m_showStats = showStats;

    finishDiscoveryIfReady();
    updateAutoCreate(inputs);
    dockCompanionDATs();

    int sendError = m_lastSendError.exchange(0);
    if (sendError != 0)
    {
        #ifdef _WIN32
            m_lastError = "Send failed with error: " + std::to_string(sendError);
        #else
            m_lastError = "Send failed with errno " + std::to_string(sendError) + ": " + strerror(sendError);
        #endif
    }
    
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
    
    // Check if we need to reinitialize socket (IP changed)
    bool needsReinit = false;
    if (m_lastIPAddress != ipAddress)
    {
        needsReinit = true;
        m_lastIPAddress = ipAddress;
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
        m_destAddr.sin_port = htons(DDP_PORT);
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
    m_workPixels.clear();
    processInterleavedChannels(chopInput, gamma, brightness, normalizedInput, m_workPixels);
    
    // Update channel and pixel counts
    m_lastChannelCount = static_cast<int32_t>(m_workPixels.size());
    m_lastPixelCount = (channelsPerPixel > 0) ? (m_lastChannelCount / channelsPerPixel) : 0;
    
    // Hand the frame to the sender thread if we have data.
    // A routing table slices the stream across controllers. With no table,
    // the whole input still goes to IP Address.
    if (!m_workPixels.empty())
    {
        std::vector<DeviceRoute> routes;
        bool usingTable = parseRoutingTable(inputs, channelsPerPixel, m_workPixels.size(), routes);
        if (usingTable)
        {
            m_routes = routes;
            m_routeCount = static_cast<int32_t>(routes.size());
        }
        else
        {
            m_routes.clear();
            m_routeCount = 1;
            DeviceRoute route;
            route.addr = m_destAddr;
            route.byteOffset = 0;
            route.byteCount = m_workPixels.size();
            routes.push_back(route);
        }
        submitFrame(routes);
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
    infoSize->rows = 3 + static_cast<int32_t>(m_discoveredDevices.size());
    infoSize->cols = 2;
    infoSize->byColumn = false;
    return true;
}

void DDPOutputCHOP::getInfoDATEntries(int32_t index, int32_t nEntries, 
                                       OP_InfoDATEntries* entries, void* reserved1)
{
    if (index == 0)
    {
        entries->values[0]->setString("Last Status");
        entries->values[1]->setString(m_lastError.c_str());
    }
    else if (index == 1)
    {
        entries->values[0]->setString("Devices Found");
        entries->values[1]->setString(std::to_string(m_discoveredDevices.size()).c_str());
    }
    else if (index == 2)
    {
        // Frames replaced before the sender thread got to them (network can't keep up).
        int64_t dropped;
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            dropped = m_framesDropped;
        }
        entries->values[0]->setString("Frames Dropped");
        entries->values[1]->setString(std::to_string(dropped).c_str());
    }
    else if (index >= 3 && index < 3 + static_cast<int32_t>(m_discoveredDevices.size()))
    {
        int deviceIdx = index - 3;
        entries->values[0]->setString(("Device " + std::to_string(deviceIdx + 1)).c_str());
        entries->values[1]->setString(m_discoveredDevices[deviceIdx].c_str());
    }
}

