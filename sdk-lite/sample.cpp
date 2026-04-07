#include <cstdio>
#include <chrono>
#include <thread>
#include "SMX.h"

void OnStateChanged(int pad, SMXUpdateCallbackReason reason, void *pUser)
{
    SMXInfo info;
    SMX_GetInfo(pad, &info);

    if(!info.m_bConnected)
    {
        printf("Pad %i: disconnected\n", pad);
        return;
    }

    uint16_t state = SMX_GetInputState(pad);
    printf("Pad %i (P%i, serial %s, fw %i): input %04x\n",
        pad,
        pad + 1,
        info.m_Serial,
        info.m_iFirmwareVersion,
        state);
}

int main()
{
    printf("SMX SDK Lite v%s\n", SMX_Version());

    SMX_Start(OnStateChanged, nullptr);

    printf("Scanning for StepManiaX devices... Press Ctrl+C to quit.\n");

    while(true)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

    SMX_Stop();
    return 0;
}
