#ifndef SMXDeviceConnection_h
#define SMXDeviceConnection_h

#include <string>
#include <list>
#include <functional>
#include <memory>
#include <hidapi/hidapi.h>

namespace SMX {

struct SMXDeviceInfo
{
    bool m_bP2 = false;
    char m_Serial[33] = {};
    uint16_t m_iFirmwareVersion = 0;
};

class SMXDeviceConnection
{
public:
    SMXDeviceConnection();
    ~SMXDeviceConnection();

    // Non-copyable, movable.
    SMXDeviceConnection(const SMXDeviceConnection &) = delete;
    SMXDeviceConnection &operator=(const SMXDeviceConnection &) = delete;
    SMXDeviceConnection(SMXDeviceConnection &&other) noexcept;
    SMXDeviceConnection &operator=(SMXDeviceConnection &&other) noexcept;

    bool Open(const std::string &sPath, std::string &sError);
    void Close();

    bool IsConnected() const { return m_pDevice != nullptr; }
    bool IsConnectedWithDeviceInfo() const { return m_pDevice != nullptr && m_bGotInfo; }
    std::string GetPath() const { return m_sPath; }

    SMXDeviceInfo GetDeviceInfo() const { return m_DeviceInfo; }

    void Update(std::string &sError);

    void SetActive(bool bActive) { m_bActive = bActive; }
    bool GetActive() const { return m_bActive; }

    bool ReadPacket(std::string &out);
    void SendCommand(const std::string &cmd, std::function<void(std::string response)> pComplete = nullptr);

    uint16_t GetInputState() const { return m_iInputState; }

private:
    void RequestDeviceInfo(std::function<void(std::string response)> pComplete = nullptr);
    void CheckReads(std::string &sError);
    void CheckWrites(std::string &sError);
    void HandleUsbPacket(const std::string &buf);

    hid_device *m_pDevice = nullptr;
    std::string m_sPath;
    bool m_bActive = false;
    bool m_bGotInfo = false;

    std::list<std::string> m_sReadBuffers;
    std::string m_sCurrentReadBuffer;

    uint16_t m_iInputState = 0;
    SMXDeviceInfo m_DeviceInfo;

    struct PendingCommand {
        std::string sData;
        std::function<void(std::string response)> m_pComplete;
        bool m_bIsDeviceInfoCommand = false;
        bool m_bSent = false;
        double m_fSentAt = 0;
    };

    std::list<std::shared_ptr<PendingCommand>> m_aPendingCommands;
    std::shared_ptr<PendingCommand> m_pCurrentCommand;
};

}

#endif
