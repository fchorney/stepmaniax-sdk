#include "SMXDevice.h"
#include "Helpers.h"
#include <cstring>
#include <algorithm>
#include <utility>

using namespace std;
using namespace SMX;

SMXDevice::SMXDevice() {}
SMXDevice::~SMXDevice() {}

SMXDevice::SMXDevice(SMXDevice &&other) noexcept:
    m_pLock(other.m_pLock),
    m_pUpdateCallback(std::move(other.m_pUpdateCallback)),
    m_Connection(std::move(other.m_Connection)),
    m_Config(other.m_Config),
    m_bHaveConfig(other.m_bHaveConfig)
{
    other.m_bHaveConfig = false;
}

SMXDevice &SMXDevice::operator=(SMXDevice &&other) noexcept
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

bool SMXDevice::OpenDevice(const string &sPath, string &sError)
{
    return m_Connection.Open(sPath, sError);
}

void SMXDevice::CloseDevice()
{
    m_Connection.Close();
    m_bHaveConfig = false;
    CallUpdateCallback(SMXUpdateCallback_Updated);
}

string SMXDevice::GetDevicePath() const
{
    return m_Connection.GetPath();
}

void SMXDevice::SetUpdateCallback(function<void(int PadNumber, SMXUpdateCallbackReason reason)> pCallback)
{
    m_pUpdateCallback = pCallback;
}

bool SMXDevice::IsConnected() const
{
    lock_guard<recursive_mutex> lock(*m_pLock);
    return IsConnectedLocked();
}

bool SMXDevice::IsConnectedLocked() const
{
    return m_Connection.IsConnectedWithDeviceInfo() && m_bHaveConfig;
}

void SMXDevice::SendCommand(const string &cmd, function<void(string response)> pComplete)
{
    lock_guard<recursive_mutex> lock(*m_pLock);

    if(!m_Connection.IsConnected())
    {
        if(pComplete) pComplete("");
        return;
    }

    m_Connection.SendCommand(cmd, pComplete);
}

void SMXDevice::GetInfo(SMXInfo &info)
{
    lock_guard<recursive_mutex> lock(*m_pLock);
    GetInfoLocked(info);
}

void SMXDevice::GetInfoLocked(SMXInfo &info)
{
    info = SMXInfo();
    info.m_bConnected = IsConnectedLocked();
    if(!info.m_bConnected)
        return;

    SMXDeviceInfo deviceInfo = m_Connection.GetDeviceInfo();
    memcpy(info.m_Serial, deviceInfo.m_Serial, sizeof(info.m_Serial));
    info.m_iFirmwareVersion = deviceInfo.m_iFirmwareVersion;
}

bool SMXDevice::IsPlayer2Locked() const
{
    if(!IsConnectedLocked())
        return false;
    return m_Connection.GetDeviceInfo().m_bP2;
}

uint16_t SMXDevice::GetInputState() const
{
    lock_guard<recursive_mutex> lock(*m_pLock);
    return m_Connection.GetInputState();
}

void SMXDevice::Update(string &sError)
{
    if(!m_Connection.IsConnected())
        return;

    CheckActive();

    uint16_t iOldState = m_Connection.GetInputState();

    m_Connection.Update(sError);
    if(!sError.empty())
        return;

    if(iOldState != m_Connection.GetInputState())
        CallUpdateCallback(SMXUpdateCallback_Updated);

    HandlePackets();
}

void SMXDevice::CheckActive()
{
    if(!m_Connection.IsConnectedWithDeviceInfo() || m_Connection.GetActive())
        return;

    m_Connection.SetActive(true);

    SMXDeviceInfo deviceInfo = m_Connection.GetDeviceInfo();
    m_Connection.SendCommand(deviceInfo.m_iFirmwareVersion >= 5 ? "G" : "g\n");
}

void SMXDevice::HandlePackets()
{
    string buf;
    while(m_Connection.ReadPacket(buf))
    {
        if(buf.empty())
            continue;

        switch(buf[0])
        {
        case 'g':
        case 'G':
        {
            if(buf.size() < 2)
            {
                Log("Communication error: invalid configuration packet");
                continue;
            }

            uint8_t iSize = (uint8_t)buf[1];
            if((int)buf.size() < iSize + 2)
            {
                Log("Communication error: invalid configuration packet");
                continue;
            }

            if(buf[0] == 'g')
            {
                vector<uint8_t> rawConfig(iSize);
                memcpy(rawConfig.data(), buf.data() + 2, iSize);
                ConvertToNewConfig(rawConfig, m_Config);
            }
            else
            {
                memcpy(&m_Config, buf.data() + 2, min((int)iSize, (int)sizeof(m_Config)));
            }

            m_bHaveConfig = true;
            CallUpdateCallback(SMXUpdateCallback_Updated);
            break;
        }
        }
    }
}

void SMXDevice::CallUpdateCallback(SMXUpdateCallbackReason reason)
{
    if(!m_pUpdateCallback)
        return;

    SMXDeviceInfo deviceInfo = m_Connection.GetDeviceInfo();
    m_pUpdateCallback(deviceInfo.m_bP2 ? 1 : 0, reason);
}
