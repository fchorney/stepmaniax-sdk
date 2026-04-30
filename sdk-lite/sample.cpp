#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "SMX.h"

volatile std::sig_atomic_t g_shouldExit = 0;

/// Custom log callback to demonstrate SMX_SetLogCallback.
/// Applications can redirect logs to files, themed UI, or other streams.
void CustomLogCallback(const char *log)
{
    // Example: prepend [SMX] marker to distinguish SDK logs
    std::cerr << "[SMX] " << log << std::endl;
}

void signal_handler(const int signal) {
    if(signal == SIGINT) {
        g_shouldExit = 1;
    }
}

/// Track previous input state for each pad to detect changes during polling.
uint16_t g_lastInputState[2] = {0, 0};

void OnStateChanged(const int pad, const SMXUpdateCallbackReason reason, void *pUser)
{
    if(SMX_REASON_IS(reason, SMXUpdateCallback_Disconnected))
    {
        printf("Pad %i: disconnected\n", pad);
        g_lastInputState[pad] = 0;
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
    }
}

/// Print input state changes for a pad. Called from polling loop.
void PrintInputChange(const int pad)
{
    SMXInfo info;
    SMX_GetInfo(pad, &info);

    if(!info.m_bConnected)
        return;

    const uint16_t state = SMX_GetInputState(pad);
    if(state != g_lastInputState[pad])
    {
        printf("Pad %i: input changed to %04x\n", pad, state);
        g_lastInputState[pad] = state;
    }
}

int main()
{
    std::signal(SIGINT, signal_handler);

    // Demonstrate SMX_SetLogCallback: redirect SDK logs before initialization.
    // This can be called before SMX_Start to capture initialization logs.
    // Pass nullptr to revert to default stdout logging.
    SMX_SetLogCallback(CustomLogCallback);

    printf("SMX SDK Lite v%s\n", SMX_Version());

    // Start scanning for devices and register state change callback.
    // The callback will be invoked asynchronously when devices connect/disconnect
    // or when their input state changes.
    SMX_Start(OnStateChanged, nullptr);

    printf("Scanning for StepManiaX devices... Press Ctrl+C to quit.\n");

    // On first connection, check for duplicate player config or missing serials.
    bool bChecked = false;
    bool bHasAskedSerials = false;
    while(!g_shouldExit)
    {
        // Continuously monitor input changes by polling
        PrintInputChange(0);
        PrintInputChange(1);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

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

        // Demonstrate SMX_SetSerialNumbers():
        // Once all pads are connected, you can assign serial numbers.
        // This is optional but recommended for device identification.
        // Serial numbers persist across power cycles.
        if(!bHasAskedSerials)
        {
            SMXInfo info[2];
            SMX_GetInfo(0, &info[0]);
            SMX_GetInfo(1, &info[1]);

            bool bHaveUnserializedDevices = false;
            if((info[0].m_bConnected && !info[0].m_bHasSerialNumber) ||
               (info[1].m_bConnected && !info[1].m_bHasSerialNumber))
            {
                bHaveUnserializedDevices = true;
            }

            if(bHaveUnserializedDevices)
            {
                printf("Tip: Call SMX_SetSerialNumbers() to assign persistent serial numbers.\n");
                printf("Usage: Uncomment the line below or add to your application.\n");
                // Uncomment the line below to assign serial numbers:
                // SMX_SetSerialNumbers();
                bHasAskedSerials = true;
            }
            else if(info[0].m_bConnected || info[1].m_bConnected)
            {
                bHasAskedSerials = true;
            }
        }
    }

    SMX_Stop();
    return 0;
}
