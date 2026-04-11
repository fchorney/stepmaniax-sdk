#include "SMXDeviceConnection.h"

#include <algorithm>
#include <utility>

// These are defined in SMX.cpp.
namespace SMX {
    void Log(const std::string &s);
    std::string ssprintf(const char *fmt, ...);
    std::string BinaryToHex(const void *pData, int iNumBytes);
    double GetMonotonicTime();
}

using namespace std;
using namespace SMX;

#define PACKET_FLAG_START_OF_COMMAND   0x04
#define PACKET_FLAG_END_OF_COMMAND     0x01
#define PACKET_FLAG_HOST_CMD_FINISHED  0x02
#define PACKET_FLAG_DEVICE_INFO        0x80

SMXDeviceConnection::SMXDeviceConnection() = default;

SMXDeviceConnection::~SMXDeviceConnection() { Close(); }

SMXDeviceConnection::SMXDeviceConnection(SMXDeviceConnection &&other) noexcept:
    m_pDevice(other.m_pDevice),
    m_sPath(std::move(other.m_sPath)),
    m_bActive(other.m_bActive),
    m_bGotInfo(other.m_bGotInfo),
    m_sReadBuffers(std::move(other.m_sReadBuffers)),
    m_sCurrentReadBuffer(std::move(other.m_sCurrentReadBuffer)),
    m_iInputState(other.m_iInputState),
    m_DeviceInfo(other.m_DeviceInfo),
    m_aPendingCommands(std::move(other.m_aPendingCommands)),
    m_pCurrentCommand(std::move(other.m_pCurrentCommand))
{
    other.m_pDevice = nullptr;
}

SMXDeviceConnection &SMXDeviceConnection::operator=(SMXDeviceConnection &&other) noexcept
{
    if(this != &other)
    {
        Close();
        m_pDevice = other.m_pDevice;
        m_sPath = std::move(other.m_sPath);
        m_bActive = other.m_bActive;
        m_bGotInfo = other.m_bGotInfo;
        m_sReadBuffers = std::move(other.m_sReadBuffers);
        m_sCurrentReadBuffer = std::move(other.m_sCurrentReadBuffer);
        m_iInputState = other.m_iInputState;
        m_DeviceInfo = other.m_DeviceInfo;
        m_aPendingCommands = std::move(other.m_aPendingCommands);
        m_pCurrentCommand = std::move(other.m_pCurrentCommand);
        other.m_pDevice = nullptr;
    }
    return *this;
}

bool SMXDeviceConnection::Open(const string &sPath, string &sError)
{
    m_pDevice = hid_open_path(sPath.c_str());
    if(!m_pDevice)
    {
        sError = "Failed to open HID device: " + sPath;
        return false;
    }

    m_sPath = sPath;
    hid_set_nonblocking(m_pDevice, 1);

    // Request device info. The response is handled in HandleUsbPacket which
    // sets m_bGotInfo directly, so no callback capture of 'this' is needed.
    RequestDeviceInfo(nullptr);

    return true;
}

void SMXDeviceConnection::Close()
{
    if(!m_pDevice)
        return;

    Log("Closing device");

    if(m_pCurrentCommand && m_pCurrentCommand->m_pComplete)
        m_pCurrentCommand->m_pComplete("");
    for(const auto &cmd : m_aPendingCommands)
    {
        if(cmd->m_pComplete)
            cmd->m_pComplete("");
    }

    hid_close(m_pDevice);
    m_pDevice = nullptr;
    m_sPath.clear();
    m_sReadBuffers.clear();
    m_sCurrentReadBuffer.clear();
    m_aPendingCommands.clear();
    m_pCurrentCommand = nullptr;
    m_bActive = false;
    m_bGotInfo = false;
    m_iInputState = 0;
}

void SMXDeviceConnection::Update(string &sError)
{
    if(!m_pDevice)
    {
        sError = "Device not open";
        return;
    }

    CheckReads(sError);
    if(!sError.empty()) return;
    CheckWrites(sError);
}

bool SMXDeviceConnection::ReadPacket(string &out)
{
    if(m_sReadBuffers.empty())
        return false;
    out = m_sReadBuffers.front();
    m_sReadBuffers.pop_front();
    return true;
}

void SMXDeviceConnection::CheckReads(string &sError)
{
    if(m_pCurrentCommand && m_pCurrentCommand->m_bSent)
    {
        const double fSecondsAgo = GetMonotonicTime() - m_pCurrentCommand->m_fSentAt;
        if(fSecondsAgo > 2.0)
        {
            Log("Command timed out. Retrying...");
            m_pCurrentCommand->m_bSent = false;
            m_aPendingCommands.push_front(m_pCurrentCommand);
            m_pCurrentCommand = nullptr;
        }
    }

    // hidapi read buffer. On Windows, hidapi includes the report ID as the first
    // byte. On Linux/macOS with hidraw, the report ID is stripped. We read into
    // a buffer with an extra byte at the front so we can normalize this.
    unsigned char rawbuf[65];
    while(true)
    {
        // Read into rawbuf+1, leaving rawbuf[0] free for the report ID on
        // platforms where hidapi strips it.
        const int res = hid_read(m_pDevice, rawbuf + 1, 64);
        if(res < 0)
        {
            sError = "Error reading from device";
            return;
        }
        if(res == 0)
            break;

        // On Linux/macOS hidraw, the report ID is stripped from the read.
        // On Windows, it's included. We detect this by checking if the first
        // byte looks like a valid report ID we expect (3 or 5 or 6).
        // If rawbuf[1] is a known report ID and the size matches what we'd
        // expect with the ID included, use it as-is. Otherwise, we need to
        // figure out which report this is.
        //
        // Simpler approach: on Linux, hidapi strips the report ID. We know
        // the device sends report IDs 3 (input, 2 bytes payload) and 6
        // (serial, 63 bytes payload). Since hidapi strips the ID, we need
        // to infer it from the data length.
        //
        // Report 3 (input state): 2 bytes on wire (without ID)
        // Report 6 (serial packet): up to 63 bytes on wire (without ID)
        //
        // However, this heuristic is fragile. A more robust approach:
        // use hid_read and check if the first byte is a known report ID.
#ifdef _WIN32
        // Windows: report ID is included in the read.
        HandleUsbPacket(string((char *)rawbuf + 1, res));
#else
        // Linux/macOS: report ID is stripped. We need to prepend it.
        // Infer report ID from size: input reports are small (2 bytes),
        // serial packets are larger.

        /* I think all OSs give you the report number on the first item tbh. Workshop this more later
        uint8_t reportId;
        if(res == 2)
            reportId = 3; // input state report
        else
            reportId = 6; // serial/command report

        rawbuf[0] = reportId;
        HandleUsbPacket(string((char *)rawbuf, res + 1));
        */
        HandleUsbPacket(string(reinterpret_cast<char*>(rawbuf) + 1, res));
#endif
    }
}

void SMXDeviceConnection::HandleUsbPacket(const string &buf)
{
    if(buf.empty())
        return;

    const auto iReportId = static_cast<uint8_t>(buf[0]);
    switch(iReportId)
    {
    case 3:
        if(buf.size() >= 3)
            m_iInputState = (static_cast<uint8_t>(buf[2]) << 8) | static_cast<uint8_t>(buf[1]);
        break;

    case 6:
    {
        if(buf.size() < 3)
            return;

        const int cmd = static_cast<uint8_t>(buf[1]);
        const int bytes = static_cast<uint8_t>(buf[2]);
        if(static_cast<int>(buf.size()) < 3 + bytes)
        {
            Log("Communication error: oversized packet (ignored)");
            return;
        }

        string sPacket(buf.begin() + 3, buf.begin() + 3 + bytes);

        if(cmd & PACKET_FLAG_DEVICE_INFO)
        {
            if(!m_pCurrentCommand || !m_pCurrentCommand->m_bIsDeviceInfoCommand)
                break;

#pragma pack(push, 1)
            struct data_info_packet
            {
                char cmd;
                uint8_t packet_size;
                char player;
                char unused2;
                uint8_t serial[16];
                uint16_t firmware_version;
                char unused3;
            };
#pragma pack(pop)

            sPacket.resize(sizeof(data_info_packet), '\0');
            const auto *packet = reinterpret_cast<const data_info_packet*>(sPacket.data());

            m_DeviceInfo.m_bP2 = (packet->player == '1');
            m_DeviceInfo.m_iFirmwareVersion = packet->firmware_version;

            const string sHexSerial = BinaryToHex(packet->serial, 16);
            memcpy(m_DeviceInfo.m_Serial, sHexSerial.c_str(), 33);

            Log(ssprintf("Received device info. Master version: %i, P%i",
                m_DeviceInfo.m_iFirmwareVersion, m_DeviceInfo.m_bP2 + 1));
            m_bGotInfo = true;

            if(m_pCurrentCommand->m_pComplete)
                m_pCurrentCommand->m_pComplete(sPacket);
            m_pCurrentCommand = nullptr;
            break;
        }

        if(!m_bActive)
            break;

        if((cmd & PACKET_FLAG_START_OF_COMMAND) && !m_sCurrentReadBuffer.empty())
        {
            Log(ssprintf("Got START_OF_COMMAND with %i bytes in read buffer",
                static_cast<int>(m_sCurrentReadBuffer.size())));
            m_sCurrentReadBuffer.clear();
        }

        m_sCurrentReadBuffer.append(sPacket);

        if(cmd & PACKET_FLAG_HOST_CMD_FINISHED)
        {
            if(m_pCurrentCommand && m_pCurrentCommand->m_pComplete)
                m_pCurrentCommand->m_pComplete(m_sCurrentReadBuffer);
            m_pCurrentCommand = nullptr;
        }

        if(cmd & PACKET_FLAG_END_OF_COMMAND)
        {
            if(!m_sCurrentReadBuffer.empty())
                m_sReadBuffers.push_back(m_sCurrentReadBuffer);
            m_sCurrentReadBuffer.clear();
        }
        break;
    }
    default:
        // Insert some kind of log here maybe?
        break;
    }
}

void SMXDeviceConnection::CheckWrites(string &sError)
{
    if(m_pCurrentCommand)
        return;

    if(m_aPendingCommands.empty())
        return;

    const auto pCmd = m_aPendingCommands.front();
    m_aPendingCommands.pop_front();

    // Send all HID packets for this command sequentially.
    // Each packet is 64 bytes (report ID + 63 bytes payload).
    const string &sData = pCmd->sData;
    for(size_t offset = 0; offset < sData.size(); offset += 64)
    {
        const size_t len = min(static_cast<size_t>(64), sData.size() - offset);
        const int res = hid_write(m_pDevice, reinterpret_cast<const unsigned char*>(sData.data()) + offset, len);
        if(res < 0)
        {
            sError = "Error writing to device";
            if(pCmd->m_pComplete)
                pCmd->m_pComplete("");
            return;
        }
    }

    pCmd->m_bSent = true;
    pCmd->m_fSentAt = GetMonotonicTime();
    m_pCurrentCommand = pCmd;
}

void SMXDeviceConnection::RequestDeviceInfo(function<void(string response)> pComplete)
{
    const auto pCmd = make_shared<PendingCommand>();
    pCmd->m_pComplete = std::move(pComplete);
    pCmd->m_bIsDeviceInfoCommand = true;

    string sPacket(64, '\0');
    sPacket[0] = 5;  // report ID
    sPacket[1] = static_cast<char>(PACKET_FLAG_DEVICE_INFO);
    sPacket[2] = 0;

    pCmd->sData = sPacket;
    m_aPendingCommands.push_back(pCmd);
}

void SMXDeviceConnection::SendCommand(const string &cmd, function<void(string response)> pComplete)
{
    const auto pCmd = make_shared<PendingCommand>();
    pCmd->m_pComplete = std::move(pComplete);

    // Build HID packets. Each carries up to 61 bytes of command payload.
    string allPackets;
    int i = 0;
    do {
        const uint8_t iPacketSize = min(static_cast<int>(cmd.size() - i), 61);
        uint8_t iFlags = 0;
        if(i == 0)
            iFlags |= PACKET_FLAG_START_OF_COMMAND;
        if(i + iPacketSize == static_cast<int>(cmd.size()))
            iFlags |= PACKET_FLAG_END_OF_COMMAND;

        string sPacket(64, '\0');
        sPacket[0] = 5;  // report ID
        sPacket[1] = static_cast<char>(iFlags);
        sPacket[2] = static_cast<char>(iPacketSize);
        if(iPacketSize > 0)
            memcpy(&sPacket[3], cmd.data() + i, iPacketSize);

        allPackets += sPacket;
        i += iPacketSize;
    } while(i < static_cast<int>(cmd.size()));

    pCmd->sData = allPackets;
    m_aPendingCommands.push_back(pCmd);
}
