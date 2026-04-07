#ifndef SMXManager_h
#define SMXManager_h

#include <memory>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "SMX.h"
#include "SMXDevice.h"
#include "SMXDeviceSearch.h"

namespace SMX {

class SMXManager
{
public:
    static std::shared_ptr<SMXManager> g_pSMX;

    SMXManager(std::function<void(int PadNumber, SMXUpdateCallbackReason reason)> pCallback);
    ~SMXManager();

    void Shutdown();
    SMXDevice *GetDevice(int pad);
    void SetSerialNumbers();

private:
    void ThreadMain();
    void AttemptConnections();
    void CorrectDeviceOrder();

    std::recursive_mutex m_Lock;
    std::thread m_Thread;
    std::condition_variable_any m_Cond;
    std::atomic<bool> m_bShutdown{false};

    SMXDevice m_Devices[2];
    SMXDeviceSearch m_DeviceSearch;

    std::function<void(int PadNumber, SMXUpdateCallbackReason reason)> m_pCallback;
};

}

#endif
