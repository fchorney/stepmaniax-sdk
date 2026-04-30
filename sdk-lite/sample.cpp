#include <csignal>
#include <iostream>
#include <thread>

#include "SMX.h"

volatile std::sig_atomic_t g_shouldExit = 0;

void CustomLogCallback(const char *log)
{
    std::cerr << "[SMX] " << log << std::endl;
}

void signal_handler(const int signal) {
    if(signal == SIGINT) {
        g_shouldExit = 1;
    }
}

void OnStateChanged(const int pad, const SMXUpdateCallbackReason reason, void *pUser)
{
    if(SMX_REASON_IS(reason, SMXUpdateCallback_Disconnected))
    {
        printf("Pad %i: disconnected\n", pad);
        return;
    }

    if(SMX_REASON_IS(reason, SMXUpdateCallback_Connected))
    {
        SMXInfo info;
        SMX_GetInfo(pad, &info);
        printf("Pad %i connected (jumper: P%i, serial: %s, fw: %i)\n",
            pad,
            info.m_bIsPlayer2 ? 2 : 1,
            info.m_bHasSerialNumber ? info.m_Serial : "(none)",
            info.m_iFirmwareVersion);

        if(!info.m_bHasSerialNumber)
            printf("Warning: Pad %i has no serial number. Call SMX_SetSerialNumbers() to assign one.\n", pad);

        return;
    }

    if(SMX_REASON_IS(reason, SMXUpdateCallback_InputState))
    {
        const uint16_t state = SMX_GetInputState(pad);
        printf("Pad %i: input state %04x\n", pad, state);
    }
}

int main()
{
    std::signal(SIGINT, signal_handler);
    SMX_SetLogCallback(CustomLogCallback);

    printf("SMX SDK Lite v%s\n", SMX_Version());
    SMX_Start(OnStateChanged, nullptr);
    printf("Scanning for StepManiaX devices... Press Ctrl+C to quit.\n");

    while(!g_shouldExit)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    SMX_Stop();
    return 0;
}
