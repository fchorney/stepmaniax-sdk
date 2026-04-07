#ifndef SMX_H
#define SMX_H

#include <stdint.h>

#ifdef SMX_EXPORTS
    #ifdef _WIN32
        #define SMX_API extern "C" __declspec(dllexport)
    #else
        #define SMX_API extern "C" __attribute__((visibility("default")))
    #endif
#else
    #ifdef _WIN32
        #define SMX_API extern "C" __declspec(dllimport)
    #else
        #define SMX_API extern "C"
    #endif
#endif

struct SMXInfo;

// All functions are nonblocking. Getters return the most recent state.
// Setters return immediately and do their work in the background.

enum SMXUpdateCallbackReason {
    // Generic state change: connection, disconnection, inputs changed, etc.
    SMXUpdateCallback_Updated,
};

// Initialize and start searching for devices.
// UpdateCallback is called asynchronously from a helper thread when something
// happens: connection, disconnection, input changes, etc.
typedef void SMXUpdateCallback(int pad, SMXUpdateCallbackReason reason, void *pUser);
SMX_API void SMX_Start(SMXUpdateCallback UpdateCallback, void *pUser);

// Shut down and disconnect from all devices. Waits for callbacks to complete.
// Must not be called from within the update callback.
SMX_API void SMX_Stop();

// Set a function to receive diagnostic logs. By default, logs go to stdout.
// Can be called before SMX_Start.
typedef void SMXLogCallback(const char *log);
SMX_API void SMX_SetLogCallback(SMXLogCallback callback);

// Get info about a pad. Use this to detect which pads are connected.
SMX_API void SMX_GetInfo(int pad, SMXInfo *info);

// Get a mask of the currently pressed panels.
SMX_API uint16_t SMX_GetInputState(int pad);

// Assign serial numbers to controllers that don't have one.
SMX_API void SMX_SetSerialNumbers();

// Return the build version string.
SMX_API const char *SMX_Version();

struct SMXInfo
{
    // True if we're fully connected to this controller.
    bool m_bConnected = false;

    // True if the physical player jumper is set to player 2.
    bool m_bIsPlayer2 = false;

    // True if this controller has a serial number assigned.
    // If false, use SMX_SetSerialNumbers() to assign one.
    bool m_bHasSerialNumber = false;

    // Device serial number (null-terminated hex string).
    // Only meaningful if m_bHasSerialNumber is true.
    char m_Serial[33] = {};

    // Device firmware version.
    uint16_t m_iFirmwareVersion = 0;
};

#endif
