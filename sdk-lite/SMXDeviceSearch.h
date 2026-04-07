#ifndef SMXDeviceSearch_h
#define SMXDeviceSearch_h

#include <string>
#include <vector>
#include <set>
#include <map>
#include <hidapi/hidapi.h>

namespace SMX {

struct SMXDeviceID
{
    std::string sPath;
    std::string sSerial;
};

class SMXDeviceSearch
{
public:
    // Return a list of connected SMX device paths.
    std::vector<SMXDeviceID> GetDevices();

    // Notify that a device was closed so we re-detect it if it reappears.
    void DeviceWasClosed(const std::string &sPath);

private:
    std::set<std::string> m_sOpenPaths;
};

}

#endif
