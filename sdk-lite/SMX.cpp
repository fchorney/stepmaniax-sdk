// SMX SDK Lite — consolidated implementation.
// Contains: helpers, device search, device management, and public API.

#include "SMX.h"
#include "SMXConfigPacket.h"
#include "SMXDeviceConnection.h"

#include <hidapi/hidapi.h>

#include <cstdarg>
#include <string>
#include <utility>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <random>
#include <algorithm>
#include <memory>

using namespace std;

#define SMX_VERSION "0.1.0"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace SMX {

static function<void(const string &log)> g_LogCallback;

double GetMonotonicTime()
{
    static auto start = chrono::steady_clock::now();
    return chrono::duration<double>(chrono::steady_clock::now() - start).count();
}

void Log(const string &s)
{
    if(g_LogCallback)
        g_LogCallback(s);
    else
        printf("%6.3f: %s\n", GetMonotonicTime(), s.c_str());
}

void SetLogCallback(function<void(const string &log)> callback)
{
    g_LogCallback = std::move(callback);
}

string ssprintf(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    const int n = vsnprintf(nullptr, 0, fmt, va);
    va_end(va);
    if(n < 0) return string("Error formatting: ") + fmt;

    string s(n, '\0');
    va_start(va, fmt);
    vsnprintf(&s[0], n + 1, fmt, va);
    va_end(va);
    return s;
}

string BinaryToHex(const void *pData, const int iNumBytes)
{
    const auto *p = static_cast<const unsigned char*>(pData);
    string s;
    for(int i = 0; i < iNumBytes; i++)
        s += ssprintf("%02x", p[i]);
    return s;
}

string BinaryToHex(const string &sString)
{
    return BinaryToHex(sString.data(), static_cast<int>(sString.size()));
}

static void GenerateSerial(uint8_t *pOut)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> dist(0, 255);
    for(int i = 0; i < SERIAL_SIZE; i++)
        pOut[i] = static_cast<uint8_t>(dist(gen));
}

} // namespace SMX

// ---------------------------------------------------------------------------
// SMXDevice — high-level per-controller logic
// ---------------------------------------------------------------------------
namespace {

using namespace SMX;

class SMXDevice
{
public:
    SMXDevice() = default;
    ~SMXDevice() = default;

    SMXDevice(const SMXDevice &) = delete;
    SMXDevice &operator=(const SMXDevice &) = delete;

    SMXDevice(SMXDevice &&other) noexcept:
        m_pLock(other.m_pLock),
        m_pUpdateCallback(std::move(other.m_pUpdateCallback)),
        m_Connection(std::move(other.m_Connection)),
        m_Config(other.m_Config),
        m_bHaveConfig(other.m_bHaveConfig)
    {
        other.m_bHaveConfig = false;
    }

    SMXDevice &operator=(SMXDevice &&other) noexcept
    {
        if(this != &other)
        {
            m_pLock = other.m_pLock;
            m_pUpdateCallback = std::move(other.m_pUpdateCallback);
            m_Connection = std::move(other.m_Connection);
            m_Config = other.m_Config;
            m_bHaveConfig = other.m_bHaveConfig;
            other.m_bHaveConfig = false;
        }
        return *this;
    }

    void SetLock(recursive_mutex *pLock) { m_pLock = pLock; }

    void SetUpdateCallback(function<void(int, SMXUpdateCallbackReason)> cb) { m_pUpdateCallback = std::move(cb); }

    bool OpenDevice(const string &sPath, string &sError)
    {
        return m_Connection.Open(sPath, sError);
    }

    void CloseDevice()
    {
        m_Connection.Close();
        m_bHaveConfig = false;
        CallUpdateCallback(SMXUpdateCallback_Updated);
    }

    string GetDevicePath() const { return m_Connection.GetPath(); }

    bool IsConnected() const
    {
        lock_guard<recursive_mutex> lock(*m_pLock);
        return IsConnectedLocked();
    }

    void SendCommand(const string &cmd, const function<void(string)>& pComplete = nullptr)
    {
        lock_guard<recursive_mutex> lock(*m_pLock);
        if(!m_Connection.IsConnected()) { if(pComplete) pComplete(""); return; }
        m_Connection.SendCommand(cmd, pComplete);
    }

    void GetInfo(SMXInfo &info) const
    {
        lock_guard<recursive_mutex> lock(*m_pLock);
        GetInfoLocked(info);
    }

    void GetInfoLocked(SMXInfo &info) const
    {
        info = SMXInfo();
        info.m_bConnected = IsConnectedLocked();
        if(!info.m_bConnected) return;
        const SMXDeviceInfo di = m_Connection.GetDeviceInfo();
        info.m_bIsPlayer2 = di.m_bP2;
        memcpy(info.m_Serial, di.m_Serial, sizeof(info.m_Serial));
        info.m_iFirmwareVersion = di.m_iFirmwareVersion;

        // Check if a serial number has been assigned. An unassigned serial
        // will be all zeros or all 0xFF in the raw bytes, which shows up as
        // "00000000000000000000000000000000" or "ffffffffffffffffffffffffffffffff".
        info.m_bHasSerialNumber = false;
        for(int i = 0; i < 32; i++)
        {
            if(info.m_Serial[i] != '0' && info.m_Serial[i] != 'f')
            {
                info.m_bHasSerialNumber = true;
                break;
            }
        }
    }

    bool IsPlayer2Locked() const
    {
        return IsConnectedLocked() && m_Connection.GetDeviceInfo().m_bP2;
    }

    uint16_t GetInputState() const
    {
        lock_guard<recursive_mutex> lock(*m_pLock);
        return m_Connection.GetInputState();
    }

    // Called from I/O thread with lock held.
    void Update(string &sError)
    {
        if(!m_Connection.IsConnected()) return;

        CheckActive();

        const uint16_t oldState = m_Connection.GetInputState();
        m_Connection.Update(sError);
        if(!sError.empty()) return;

        if(oldState != m_Connection.GetInputState())
            CallUpdateCallback(SMXUpdateCallback_Updated);

        HandlePackets();
    }

private:
    bool IsConnectedLocked() const
    {
        return m_Connection.IsConnectedWithDeviceInfo() && m_bHaveConfig;
    }

    void CheckActive()
    {
        if(!m_Connection.IsConnectedWithDeviceInfo() || m_Connection.GetActive())
            return;
        m_Connection.SetActive(true);
        const SMXDeviceInfo di = m_Connection.GetDeviceInfo();
        m_Connection.SendCommand(di.m_iFirmwareVersion >= 5 ? "G" : "g\n");
    }

    void HandlePackets()
    {
        string buf;
        while(m_Connection.ReadPacket(buf))
        {
            if(buf.empty()) continue;
            if(buf[0] != 'g' && buf[0] != 'G') continue;

            if(buf.size() < 2) { Log("Invalid config packet"); continue; }
            const auto iSize = static_cast<uint8_t>(buf[1]);
            if(static_cast<int>(buf.size()) < iSize + 2) { Log("Invalid config packet"); continue; }

            if(buf[0] == 'g')
            {
                vector<uint8_t> raw(buf.begin() + 2, buf.begin() + 2 + iSize);
                ConvertToNewConfig(raw, m_Config);
            }
            else
            {
                memcpy(&m_Config, buf.data() + 2, min(static_cast<int>(iSize), static_cast<int>(sizeof(m_Config))));
            }

            m_bHaveConfig = true;
            CallUpdateCallback(SMXUpdateCallback_Updated);
        }
    }

    void CallUpdateCallback(SMXUpdateCallbackReason const reason) const
    {
        if(!m_pUpdateCallback) return;
        const SMXDeviceInfo di = m_Connection.GetDeviceInfo();
        m_pUpdateCallback(di.m_bP2 ? 1 : 0, reason);
    }

    recursive_mutex *m_pLock = nullptr;
    function<void(int, SMXUpdateCallbackReason)> m_pUpdateCallback;
    SMXDeviceConnection m_Connection;
    SMXConfig m_Config;
    bool m_bHaveConfig = false;
};

// ---------------------------------------------------------------------------
// SMXManager — device search, I/O thread, orchestration
// ---------------------------------------------------------------------------
class SMXManager
{
public:
    explicit SMXManager(const function<void(int, SMXUpdateCallbackReason)>& callback):
        m_Callback(callback)
    {
        for(auto & m_Device : m_Devices)
        {
            m_Device.SetLock(&m_Lock);
            m_Device.SetUpdateCallback(callback);
        }
        m_Thread = thread([this] { ThreadMain(); });
    }

    ~SMXManager()
    {
        m_bShutdown = true;
        m_Cond.notify_all();
        if(m_Thread.joinable())
            m_Thread.join();
    }

    SMXDevice *GetDevice(const int pad)
    {
        if(pad < 0 || pad > 1) return nullptr;
        return &m_Devices[pad];
    }

    void SetSerialNumbers()
    {
        lock_guard<recursive_mutex> lock(m_Lock);
        for(auto & m_Device : m_Devices)
        {
            string sData = "s";
            uint8_t serial[SERIAL_SIZE];
            GenerateSerial(serial);
            sData.append(reinterpret_cast<char*>(serial), sizeof(serial));
            sData.append(1, '\n');
            m_Device.SendCommand(sData);
        }
    }

private:
    void ThreadMain()
    {
        m_Lock.lock();
        while(!m_bShutdown)
        {
            AttemptConnections();

            for(int i = 0; i < 2; i++)
            {
                string sError;
                m_Devices[i].Update(sError);
                if(!sError.empty())
                {
                    Log(ssprintf("Device %i error: %s", i, sError.c_str()));
                    string path = m_Devices[i].GetDevicePath();
                    m_Devices[i].CloseDevice();
                }
            }

            CorrectDeviceOrder();
            m_Cond.wait_for(m_Lock, chrono::milliseconds(50));
        }
        m_Lock.unlock();
    }

    void AttemptConnections()
    {
        // Enumerate SMX devices via hidapi.
        hid_device_info *devs = hid_enumerate(0x2341, 0x8037);
        for(const hid_device_info *cur = devs; cur; cur = cur->next)
        {
            if(!cur->product_string || wcscmp(cur->product_string, L"StepManiaX") != 0)
                continue;
            if(!cur->path)
                continue;

            string sPath = cur->path;

            // Skip if already open.
            bool bOpen = false;
            for(const auto & m_Device : m_Devices)
                if(m_Device.GetDevicePath() == sPath) { bOpen = true; break; }
            if(bOpen) continue;

            // Find an empty slot.
            SMXDevice *pSlot = nullptr;
            for(auto & m_Device : m_Devices)
                if(m_Device.GetDevicePath().empty()) { pSlot = &m_Device; break; }

            if(!pSlot) { Log("No available slots for device."); break; }

            Log("Opening SMX device: " + sPath);
            string sError;
            pSlot->OpenDevice(sPath, sError);
            if(!sError.empty())
                Log("Error opening device: " + sError);
        }
        hid_free_enumeration(devs);
    }

    void CorrectDeviceOrder()
    {
        SMXInfo info[2];
        m_Devices[0].GetInfoLocked(info[0]);
        m_Devices[1].GetInfoLocked(info[1]);

        if(info[0].m_bConnected && info[1].m_bConnected &&
           m_Devices[0].IsPlayer2Locked() == m_Devices[1].IsPlayer2Locked())
            return;

        const bool bSwap = (info[0].m_bConnected && m_Devices[0].IsPlayer2Locked()) ||
                     (info[1].m_bConnected && !m_Devices[1].IsPlayer2Locked());
        if(bSwap)
        {
            SMXDevice temp(std::move(m_Devices[0]));
            m_Devices[0] = std::move(m_Devices[1]);
            m_Devices[1] = std::move(temp);
        }
    }

    recursive_mutex m_Lock;
    thread m_Thread;
    condition_variable_any m_Cond;
    atomic<bool> m_bShutdown{false};
    SMXDevice m_Devices[2];
    function<void(int, SMXUpdateCallbackReason)> m_Callback;
};

// File-static singleton. No global variable visible outside this file.
shared_ptr<SMXManager> g_pSMX;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
SMX_API void SMX_Start(SMXUpdateCallback callback, void *pUser)
{
    if(g_pSMX) return;
    hid_init();

    auto cb = [callback, pUser](const int pad, const SMXUpdateCallbackReason reason) {
        callback(pad, reason, pUser);
    };
    g_pSMX = make_shared<SMXManager>(cb);
}

SMX_API void SMX_Stop()
{
    g_pSMX.reset();
    hid_exit();
}

SMX_API void SMX_SetLogCallback(SMXLogCallback callback)
{
    SetLogCallback([callback](const string &log) {
        callback(log.c_str());
    });
}

SMX_API void SMX_GetInfo(const int pad, SMXInfo *info)
{
    if(!g_pSMX) return;
    const auto *dev = g_pSMX->GetDevice(pad);
    if(dev) dev->GetInfo(*info);
}

SMX_API uint16_t SMX_GetInputState(const int pad)
{
    if(!g_pSMX) return 0;
    const auto *dev = g_pSMX->GetDevice(pad);
    return dev ? dev->GetInputState() : 0;
}

SMX_API void SMX_SetSerialNumbers()
{
    if(g_pSMX) g_pSMX->SetSerialNumbers();
}

SMX_API const char *SMX_Version()
{
    return SMX_VERSION;
}
