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

// USB report flags used in the SMX protocol for packet fragmentation and control.
#define PACKET_FLAG_START_OF_COMMAND   0x04  // Indicates start of a multi-packet command
#define PACKET_FLAG_END_OF_COMMAND     0x01  // Indicates end of a multi-packet command
#define PACKET_FLAG_HOST_CMD_FINISHED  0x02  // Device has finished processing command
#define PACKET_FLAG_DEVICE_INFO        0x80  // This packet contains device info response

SMXDeviceConnection::SMXDeviceConnection() = default;

SMXDeviceConnection::~SMXDeviceConnection() { Close(); }

/// Move constructor transfers the HID connection and all pending I/O state from another instance.
/// The source object is left in a disconnected state (m_pDevice set to nullptr).
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

/// Move assignment operator transfers connection and state, properly closing existing connection.
/// The source object is left in a disconnected state.
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

/// Opens a connection to the SMX device at the given HID path.
/// Sets the device to non-blocking mode and automatically requests device information.
/// The device is considered fully connected once device info is received (see IsConnectedWithDeviceInfo).
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

/// Closes the HID connection and cleans up all pending I/O state.
/// Invokes any pending command completion callbacks with empty strings to indicate cancellation.
void SMXDeviceConnection::Close()
{
    if(!m_pDevice)
        return;

    Log("Closing device");

    // Notify pending commands that they will not complete.
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

/// Processes I/O operations. Called once per frame from the I/O thread.
/// Handles reads and writes in sequence, returning errors if either operation fails.
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

/// Reads a completed packet from the internal queue.
/// Returns one packet per call; call repeatedly to drain all buffered packets.
bool SMXDeviceConnection::ReadPacket(string &out)
{
    if(m_sReadBuffers.empty())
        return false;
    out = m_sReadBuffers.front();
    m_sReadBuffers.pop_front();
    return true;
}

/// Processes all available HID data from the device.
/// Handles reading packets, detecting command timeouts, and buffering data.
/// Commands that don't receive responses within 2 seconds are requeued for retry.
/// Uses a 1-byte pad before the read buffer to normalize report ID handling across platforms:
/// - Windows: includes report ID in read data
/// - Linux/macOS: strips report ID from read data
void SMXDeviceConnection::CheckReads(string &sError)
{
    // Check if current command has timed out (2 second limit).
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
    bool bHasData = false;
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

        bHasData = true;

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

    // Signal that data was read, allowing I/O thread to wake if callback is set.
    // This enables responsive processing of stage status updates from the device.
    if(bHasData && m_pDataReadyCallback)
        m_pDataReadyCallback();
}

/// Processes a single USB packet received from the device.
/// Handles two report types:
/// - Report ID 3: Input state (pressed panels) - updates m_iInputState
/// - Report ID 6: Command/config response - buffers and processes fragmented packets
///
/// For fragmented packets (report 6), uses flags to detect start/end and handle buffering:
/// - START_OF_COMMAND: clears partial data and begins new packet
/// - END_OF_COMMAND: queues the complete packet to m_sReadBuffers
/// - HOST_CMD_FINISHED: invokes command callback if present
void SMXDeviceConnection::HandleUsbPacket(const string &buf)
{
    if(buf.empty())
        return;

    const auto iReportId = static_cast<uint8_t>(buf[0]);
    switch(iReportId)
    {
    case 3:
        // Input state report (2 bytes: panel state in little-endian 16-bit value)
        if(buf.size() >= 3)
            m_iInputState = (static_cast<uint8_t>(buf[2]) << 8) | static_cast<uint8_t>(buf[1]);
        break;

    case 6:
    {
        // Command/config response report
        if(buf.size() < 3)
            return;

        const int cmd = static_cast<uint8_t>(buf[1]);      // Command/flags byte
        const int bytes = static_cast<uint8_t>(buf[2]);    // Payload length
        if(static_cast<int>(buf.size()) < 3 + bytes)
        {
            Log("Communication error: oversized packet (ignored)");
            return;
        }

        string sPacket(buf.begin() + 3, buf.begin() + 3 + bytes);

        // Device info response (special flag 0x80)
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

        // Regular command response (only process if device is active)
        if(!m_bActive)
            break;

        // START_OF_COMMAND: clear any partial data and begin buffering
        if((cmd & PACKET_FLAG_START_OF_COMMAND) && !m_sCurrentReadBuffer.empty())
        {
            Log(ssprintf("Got START_OF_COMMAND with %i bytes in read buffer",
                static_cast<int>(m_sCurrentReadBuffer.size())));
            m_sCurrentReadBuffer.clear();
        }

        m_sCurrentReadBuffer.append(sPacket);

        // HOST_CMD_FINISHED: invoke callback if this is a command response
        if(cmd & PACKET_FLAG_HOST_CMD_FINISHED)
        {
            if(m_pCurrentCommand && m_pCurrentCommand->m_pComplete)
                m_pCurrentCommand->m_pComplete(m_sCurrentReadBuffer);
            m_pCurrentCommand = nullptr;
        }

        // END_OF_COMMAND: queue complete packet for reading
        if(cmd & PACKET_FLAG_END_OF_COMMAND)
        {
            if(!m_sCurrentReadBuffer.empty())
                m_sReadBuffers.push_back(m_sCurrentReadBuffer);
            m_sCurrentReadBuffer.clear();
        }
        break;
    }
    default:
        // Unknown report ID (silently ignored)
        break;
    }
}

/// Sends the next pending command to the device if no command is currently in flight.
/// Fragments the command into 64-byte HID packets, each with a 5-byte header (report ID + flags + length)
/// and up to 61 bytes of command payload. Sets command timing information for timeout detection.
void SMXDeviceConnection::CheckWrites(string &sError)
{
    // If a command is already in flight, wait for response before sending next.
    if(m_pCurrentCommand)
        return;

    // If no pending commands, nothing to do.
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

    // Mark command as sent and start timeout timer.
    pCmd->m_bSent = true;
    pCmd->m_fSentAt = GetMonotonicTime();
    m_pCurrentCommand = pCmd;
}

/// Sends a device info request to the device asynchronously.
/// This is a special command that triggers the device to respond with its firmware version,
/// player jumper setting, and serial number. The response is handled directly in HandleUsbPacket
/// and sets m_bGotInfo when complete, so no special completion callback is needed.
void SMXDeviceConnection::RequestDeviceInfo(function<void(string response)> pComplete)
{
    const auto pCmd = make_shared<PendingCommand>();
    pCmd->m_pComplete = std::move(pComplete);
    pCmd->m_bIsDeviceInfoCommand = true;

    // Build device info request packet.
    // Report ID 5, flag 0x80 (DEVICE_INFO), payload size 0.
    string sPacket(64, '\0');
    sPacket[0] = 5;  // report ID
    sPacket[1] = static_cast<char>(PACKET_FLAG_DEVICE_INFO);
    sPacket[2] = 0;

    pCmd->sData = sPacket;
    m_aPendingCommands.push_back(pCmd);
}

/// Queues a command for transmission to the device.
/// The command is fragmented into 64-byte HID packets with appropriate flags:
/// - Each packet begins with report ID 5, followed by flags byte and payload length
/// - START_OF_COMMAND (0x04) marks the first packet
/// - END_OF_COMMAND (0x01) marks the final packet
/// - Payload is up to 61 bytes per packet (64 byte limit - 3 byte header)
///
/// The fragmented packet data is stored in pCmd->sData and will be sent sequentially
/// by CheckWrites. Commands are processed one at a time; this just queues them.
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

/// Quick check for USB data - reads available packets without full processing.
/// This is called frequently by the USB polling thread to detect stage status updates.
void SMXDeviceConnection::QuickCheckForData(std::string &sError)
{
    if(!m_pDevice)
        return;

    // Similar to CheckReads but more lightweight - just reads the packets
    unsigned char rawbuf[65];
    while(true)
    {
        const int res = hid_read(m_pDevice, rawbuf + 1, 64);
        if(res < 0)
        {
            sError = "Error reading from device";
            return;
        }
        if(res == 0)
            break;

#ifdef _WIN32
        HandleUsbPacket(std::string((char *)rawbuf + 1, res));
#else
        HandleUsbPacket(std::string(reinterpret_cast<char*>(rawbuf) + 1, res));
#endif
    }
}
