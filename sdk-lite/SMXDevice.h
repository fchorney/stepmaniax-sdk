#ifndef SMXDevice_h
#define SMXDevice_h

#include <string>
#include <functional>
#include <mutex>
#include <cstdint>

#include "SMX.h"
#include "SMXConfigPacket.h"
#include "SMXDeviceConnection.h"

namespace SMX
{

// High-level interface to a single SMX controller.
// Thread-safe public methods acquire the shared lock internally.
// "Locked" methods assume the caller already holds the lock.
class SMXDevice
{
public:
    SMXDevice();
    ~SMXDevice();

    // Movable, non-copyable.
    SMXDevice(const SMXDevice &) = delete;
    SMXDevice &operator=(const SMXDevice &) = delete;
    SMXDevice(SMXDevice &&other) noexcept;
    SMXDevice &operator=(SMXDevice &&other) noexcept;

    void SetLock(std::recursive_mutex *pLock) { m_pLock = pLock; }

    bool OpenDevice(const std::string &sPath, std::string &sError);
    void CloseDevice();
    std::string GetDevicePath() const;

    void SetUpdateCallback(std::function<void(int PadNumber, SMXUpdateCallbackReason reason)> pCallback);

    // Thread-safe (acquires lock internally).
    bool IsConnected() const;
    void GetInfo(SMXInfo &info);
    uint16_t GetInputState() const;
    void SendCommand(const std::string &cmd, std::function<void(std::string response)> pComplete = nullptr);

    // Must be called with lock held (used by SMXManager).
    void GetInfoLocked(SMXInfo &info);
    bool IsPlayer2Locked() const;
    void Update(std::string &sError);

private:
    void CheckActive();
    void HandlePackets();
    void CallUpdateCallback(SMXUpdateCallbackReason reason);
    bool IsConnectedLocked() const;

    std::recursive_mutex *m_pLock = nullptr;

    std::function<void(int PadNumber, SMXUpdateCallbackReason reason)> m_pUpdateCallback;
    SMXDeviceConnection m_Connection;
    SMXConfig m_Config;
    bool m_bHaveConfig = false;
};

}

#endif
