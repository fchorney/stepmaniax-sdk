#include <chrono>
#include <csignal>
#include <thread>
#include "SMX.h"

volatile std::sig_atomic_t g_shouldExit = 0;

void signal_handler(const int signal) {
    if(signal == SIGINT) {
        g_shouldExit = 1;
    }
}

void OnStateChanged(const int pad, const SMXUpdateCallbackReason reason, void *pUser)
{
    SMXInfo info;
    SMX_GetInfo(pad, &info);

    if(!info.m_bConnected)
    {
        printf("Pad %i: disconnected\n", pad);
        return;
    }

    const uint16_t state = SMX_GetInputState(pad);
    printf("Pad %i (jumper: P%i, serial: %s%s, fw: %i): input %04x\n",
        pad,
        info.m_bIsPlayer2 ? 2 : 1,
        info.m_bHasSerialNumber ? info.m_Serial : "(none)",
        "",
        info.m_iFirmwareVersion,
        state);
}

int main()
{
    std::signal(SIGINT, signal_handler);
    printf("SMX SDK Lite v%s\n", SMX_Version());

    SMX_Start(OnStateChanged, nullptr);

    printf("Scanning for StepManiaX devices... Press Ctrl+C to quit.\n");

    // On first connection, check for duplicate player config or missing serials.
    bool bChecked = false;
    while(!g_shouldExit)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if(!bChecked)
        {
            SMXInfo info[2];
            SMX_GetInfo(0, &info[0]);
            SMX_GetInfo(1, &info[1]);

            // Warn about missing serial numbers.
            for(int i = 0; i < 2; i++)
            {
                if(info[i].m_bConnected && !info[i].m_bHasSerialNumber)
                    printf("Warning: Pad %i has no serial number. Call SMX_SetSerialNumbers() to assign one.\n", i);
            }

            // Warn about duplicate player jumper settings.
            if(info[0].m_bConnected && info[1].m_bConnected &&
               info[0].m_bIsPlayer2 == info[1].m_bIsPlayer2)
            {
                printf("Warning: Both pads are set to P%i! Check the player jumper on the PCB.\n",
                    info[0].m_bIsPlayer2 ? 2 : 1);
                printf("  Pad 0 serial: %s\n", info[0].m_bHasSerialNumber ? info[0].m_Serial : "(none)");
                printf("  Pad 1 serial: %s\n", info[1].m_bHasSerialNumber ? info[1].m_Serial : "(none)");
            }

            if(info[0].m_bConnected || info[1].m_bConnected)
                bChecked = true;
        }
    }

    SMX_Stop();
    return 0;
}
