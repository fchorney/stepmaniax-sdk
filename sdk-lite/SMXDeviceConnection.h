#ifndef SMXDeviceConnection_h
#define SMXDeviceConnection_h

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <hidapi/hidapi.h>

namespace SMX {

/// Immutable device information retrieved from the hardware on connection.
/// This struct holds the device metadata that doesn't change during normal operation.
struct SMXDeviceInfo
{
    /// True if this device's physical jumper is set to Player 2 mode.
    bool m_bP2 = false;

    /// Serial number as a null-terminated hex string (32 chars + null terminator).
    /// Format: 32 lowercase hex characters representing 16 bytes of device serial.
    char m_Serial[33] = {};

    /// Device firmware version number.
    uint16_t m_iFirmwareVersion = 0;
};

/// Low-level USB communication abstraction for a single StepManiaX device.
///
/// This class handles:
/// - Opening HID connections to the device
/// - Sending commands and receiving responses asynchronously
/// - Reading input state (pressed panels) from the device
/// - Requesting and parsing device information
/// - Buffering and fragmenting data across HID packets (64 bytes max)
///
/// The class is non-copyable but movable to support device reordering in the manager.
/// All operations are nonblocking; commands are queued and processed in the background.
class SMXDeviceConnection
{
public:
    SMXDeviceConnection();
    ~SMXDeviceConnection();

    // Non-copyable (prevents accidental duplicate connections)
    SMXDeviceConnection(const SMXDeviceConnection &) = delete;
    SMXDeviceConnection &operator=(const SMXDeviceConnection &) = delete;

    // Movable (required for device reordering by pad index)
    SMXDeviceConnection(SMXDeviceConnection &&other) noexcept;
    SMXDeviceConnection &operator=(SMXDeviceConnection &&other) noexcept;

    /// Opens a HID connection to the device at the given path.
    /// Automatically requests device info and enters a pending state until the info arrives.
    /// @param sPath HID device path string.
    /// @param sError [out] Error message if open fails.
    /// @return True if the device was successfully opened, false otherwise.
    bool Open(const std::string &sPath, std::string &sError);

    /// Closes the connection and cancels all pending commands.
    /// Invokes completion callbacks with empty strings to notify of cancellation.
    void Close();

    /// Returns true if the HID connection is open (though device info may not be retrieved yet).
    bool IsConnected() const { return m_pDevice != nullptr; }

    /// Returns true if the connection is open AND device info has been received.
    bool IsConnectedWithDeviceInfo() const { return m_pDevice != nullptr && m_bGotInfo; }

    /// Returns the HID device path.
    std::string GetPath() const { return m_sPath; }

    /// Retrieves the cached device information.
    /// Only valid after IsConnectedWithDeviceInfo() returns true.
    SMXDeviceInfo GetDeviceInfo() const { return m_DeviceInfo; }

    /// Processes I/O operations. Called once per frame from the I/O thread.
    /// Performs nonblocking reads from the HID device, writes pending commands,
    /// and handles command timeouts.
    /// @param sError [out] Error message if an error occurs.
    void Update(std::string &sError);

    /// Sets whether the device should actively send input state updates.
    /// When active, the device continuously sends input packets; when inactive,
    /// it only responds to commands.
    void SetActive(const bool bActive) { m_bActive = bActive; }

    /// Returns whether the device is actively sending input updates.
    bool GetActive() const { return m_bActive; }

    /// Reads a completed packet from the internal buffer.
    /// Packets are queued as they are fully received from the device.
    /// @param out [out] String containing the packet data if available.
    /// @return True if a packet was available and has been dequeued, false if empty.
    bool ReadPacket(std::string &out);

    /// Queues a command to be sent to the device asynchronously.
    /// The command is automatically fragmented into 64-byte HID packets.
    /// @param cmd Command string to send.
    /// @param pComplete Optional callback invoked when the device responds (or on error).
    void SendCommand(const std::string &cmd, std::function<void(std::string response)> pComplete = nullptr);

    /// Retrieves the current input state (pressed panels) bitmask.
    /// Each of the 16 bits represents the state of a panel.
    uint16_t GetInputState() const { return m_iInputState; }

    /// Sets a callback to be invoked when data is successfully read from the device.
    /// This is used to signal the I/O thread that stage status or other data is ready
    /// to be processed, enabling event-based waking instead of polling.
    /// @param cb Callback function with no parameters. Called when device sends data.
    void SetDataReadyCallback(std::function<void()> cb) { m_pDataReadyCallback = std::move(cb); }

    /// Quick check for USB data, called by the USB polling thread.
    /// Performs a fast non-blocking read of available HID data.
    /// Does not wait for responses or handle commands; just reads raw packets.
    /// @param sError [out] Error message if a read fails.
    void QuickCheckForData(std::string &sError);

    /// Checks if any packets are pending in the read buffer.
    /// @return True if there are packets ready to be processed.
    bool HasPendingPackets() const { return !m_sReadBuffers.empty(); }

private:
    /// Sends a device info request packet to the device.
    /// The response is handled asynchronously in HandleUsbPacket() and sets m_bGotInfo.
    /// @param pComplete Optional callback for the device info response.
    void RequestDeviceInfo(std::function<void(std::string response)> pComplete = nullptr);

    /// Processes all available data from the HID device.
    /// Reads packets until no more data is available, handling command timeouts.
    /// @param sError [out] Error message if a read fails.
    void CheckReads(std::string &sError);

    /// Sends the next pending command to the device if no command is currently in flight.
    /// Breaks the command into 64-byte HID packets and sends them sequentially.
    /// @param sError [out] Error message if a write fails.
    void CheckWrites(std::string &sError);

    /// Processes a single USB packet received from the device.
    /// Handles:
    /// - Report ID 3: Input state (panel press data)
    /// - Report ID 6: Command/config packets with fragmentation flags
    /// Updates m_iInputState and queues complete packets to m_sReadBuffers.
    /// @param buf Packet data including report ID as first byte.
    void HandleUsbPacket(const std::string &buf);

    hid_device *m_pDevice = nullptr;                              // HID device handle
    std::string m_sPath;                                          // HID device path
    bool m_bActive = false;                                       // Whether device is sending input updates
    bool m_bGotInfo = false;                                      // Whether device info has been retrieved

    std::list<std::string> m_sReadBuffers;                        // Completed packets ready to read
    std::string m_sCurrentReadBuffer;                             // Packet being accumulated from fragments

    uint16_t m_iInputState = 0;                                   // Current panel press state
    SMXDeviceInfo m_DeviceInfo;                                   // Cached device metadata
    std::function<void()> m_pDataReadyCallback;                   // Callback when data is read from device

    /// Represents a command pending transmission or awaiting response.
    /// Commands may be fragmented into multiple 64-byte HID packets.
    struct PendingCommand {
        std::string sData;                                        // Raw command data (all HID packets combined)
        std::function<void(std::string response)> m_pComplete;    // Callback when response received
        bool m_bIsDeviceInfoCommand = false;                      // True if this is a device info request
        bool m_bSent = false;                                     // True if sent to device and awaiting response
        double m_fSentAt = 0;                                     // Time when command was sent (for timeout detection)
    };

    std::list<std::shared_ptr<PendingCommand>> m_aPendingCommands; // Queue of commands not yet sent
    std::shared_ptr<PendingCommand> m_pCurrentCommand;             // Command currently awaiting response
};

}

#endif
