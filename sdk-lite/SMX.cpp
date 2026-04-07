#include "SMX.h"
#include "SMXManager.h"
#include "Helpers.h"
#include <hidapi/hidapi.h>

using namespace std;
using namespace SMX;

#define SMX_VERSION "0.1.0"

SMX_API void SMX_Start(SMXUpdateCallback callback, void *pUser)
{
    if(SMXManager::g_pSMX)
        return;

    hid_init();

    auto UpdateCallback = [callback, pUser](int pad, SMXUpdateCallbackReason reason) {
        callback(pad, reason, pUser);
    };

    SMXManager::g_pSMX = make_shared<SMXManager>(UpdateCallback);
}

SMX_API void SMX_Stop()
{
    SMXManager::g_pSMX.reset();
    hid_exit();
}

SMX_API void SMX_SetLogCallback(SMXLogCallback callback)
{
    SMX::SetLogCallback([callback](const string &log) {
        callback(log.c_str());
    });
}

SMX_API void SMX_GetInfo(int pad, SMXInfo *info)
{
    if(!SMXManager::g_pSMX) return;
    SMXDevice *pDev = SMXManager::g_pSMX->GetDevice(pad);
    if(pDev) pDev->GetInfo(*info);
}

SMX_API uint16_t SMX_GetInputState(int pad)
{
    if(!SMXManager::g_pSMX) return 0;
    SMXDevice *pDev = SMXManager::g_pSMX->GetDevice(pad);
    return pDev ? pDev->GetInputState() : 0;
}

SMX_API void SMX_SetSerialNumbers()
{
    if(!SMXManager::g_pSMX) return;
    SMXManager::g_pSMX->SetSerialNumbers();
}

SMX_API const char *SMX_Version()
{
    return SMX_VERSION;
}
