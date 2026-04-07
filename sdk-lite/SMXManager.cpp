#include "SMXManager.h"
#include "Helpers.h"
#include <algorithm>
#include <utility>

using namespace std;
using namespace SMX;

shared_ptr<SMXManager> SMXManager::g_pSMX;

SMXManager::SMXManager(function<void(int PadNumber, SMXUpdateCallbackReason reason)> pCallback):
    m_pCallback(pCallback)
{
    for(int i = 0; i < 2; i++)
    {
        m_Devices[i].SetLock(&m_Lock);
        m_Devices[i].SetUpdateCallback(pCallback);
    }

    m_Thread = thread([this] { ThreadMain(); });
}

SMXManager::~SMXManager()
{
    Shutdown();
}

SMXDevice *SMXManager::GetDevice(int pad)
{
    if(pad < 0 || pad > 1) return nullptr;
    return &m_Devices[pad];
}

void SMXManager::Shutdown()
{
    if(!m_Thread.joinable())
        return;

    m_bShutdown = true;
    m_Cond.notify_all();
    m_Thread.join();
}

void SMXManager::CorrectDeviceOrder()
{
    // If we have a P2 device in slot 0 or a P1 device in slot 1, swap them.
    SMXInfo info[2];
    m_Devices[0].GetInfoLocked(info[0]);
    m_Devices[1].GetInfoLocked(info[1]);

    // If both are the same player, they're misconfigured — leave them alone.
    if(info[0].m_bConnected && info[1].m_bConnected &&
       m_Devices[0].IsPlayer2Locked() == m_Devices[1].IsPlayer2Locked())
        return;

    bool bP1NeedsSwap = info[0].m_bConnected && m_Devices[0].IsPlayer2Locked();
    bool bP2NeedsSwap = info[1].m_bConnected && !m_Devices[1].IsPlayer2Locked();
    if(bP1NeedsSwap || bP2NeedsSwap)
    {
        SMXDevice temp(std::move(m_Devices[0]));
        m_Devices[0] = std::move(m_Devices[1]);
        m_Devices[1] = std::move(temp);
    }
}

void SMXManager::ThreadMain()
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
                string sPath = m_Devices[i].GetDevicePath();
                m_Devices[i].CloseDevice();
                m_DeviceSearch.DeviceWasClosed(sPath);
            }
        }

        CorrectDeviceOrder();

        // Wait briefly, releasing the lock so other threads can access devices.
        m_Cond.wait_for(m_Lock, chrono::milliseconds(50));
    }

    m_Lock.unlock();
}

void SMXManager::AttemptConnections()
{
    vector<SMXDeviceID> apDevices = m_DeviceSearch.GetDevices();

    for(const SMXDeviceID &dev : apDevices)
    {
        // See if this device is already open.
        bool bAlreadyOpen = false;
        for(int i = 0; i < 2; i++)
        {
            if(m_Devices[i].GetDevicePath() == dev.sPath)
            {
                bAlreadyOpen = true;
                break;
            }
        }
        if(bAlreadyOpen)
            continue;

        // Find an open slot.
        SMXDevice *pSlot = nullptr;
        for(int i = 0; i < 2; i++)
        {
            if(m_Devices[i].GetDevicePath().empty())
            {
                pSlot = &m_Devices[i];
                break;
            }
        }

        if(!pSlot)
        {
            Log("No available slots. Are more than two devices connected?");
            break;
        }

        Log("Opening SMX device: " + dev.sPath);
        string sError;
        pSlot->OpenDevice(dev.sPath, sError);
        if(!sError.empty())
            Log("Error opening device: " + sError);
    }
}

void SMXManager::SetSerialNumbers()
{
    lock_guard<recursive_mutex> lock(m_Lock);

    for(int i = 0; i < 2; i++)
    {
        string sData = "s";
        uint8_t serial[16];
        GenerateRandom(serial, sizeof(serial));
        sData.append((char *)serial, sizeof(serial));
        sData.append(1, '\n');
        m_Devices[i].SendCommand(sData);
    }
}
