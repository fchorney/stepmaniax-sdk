#include "SMXDeviceSearch.h"
#include "Helpers.h"
#include <cstring>

using namespace std;
using namespace SMX;

vector<SMXDeviceID> SMX::SMXDeviceSearch::GetDevices()
{
    vector<SMXDeviceID> results;

    struct hid_device_info *devs = hid_enumerate(0x2341, 0x8037);
    struct hid_device_info *cur = devs;

    while(cur)
    {
        // Verify product string matches.
        bool bMatch = false;
        if(cur->product_string)
        {
            // Compare wide string to "StepManiaX".
            const wchar_t *expected = L"StepManiaX";
            bMatch = (wcscmp(cur->product_string, expected) == 0);
        }

        if(bMatch && cur->path)
        {
            SMXDeviceID id;
            id.sPath = cur->path;

            if(cur->serial_number)
            {
                // Convert wide serial to narrow string.
                wstring ws(cur->serial_number);
                id.sSerial = string(ws.begin(), ws.end());
            }

            results.push_back(id);
        }

        cur = cur->next;
    }

    hid_free_enumeration(devs);
    return results;
}

void SMX::SMXDeviceSearch::DeviceWasClosed(const string &sPath)
{
    m_sOpenPaths.erase(sPath);
}
