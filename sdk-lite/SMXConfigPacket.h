#ifndef SMXConfigPacket_h
#define SMXConfigPacket_h

#include <vector>
#include <cstdint>

// SMXConfig is needed internally for the connection handshake (the device isn't
// considered "connected" until its config is read). We keep the struct here but
// don't expose it in the public API.

#pragma pack(push, 1)
struct packed_sensor_settings_t {
    uint8_t loadCellLowThreshold;
    uint8_t loadCellHighThreshold;
    uint8_t fsrLowThreshold[4];
    uint8_t fsrHighThreshold[4];
    uint16_t combinedLowThreshold;
    uint16_t combinedHighThreshold;
    uint16_t reserved;
};

struct SMXConfig
{
    uint8_t masterVersion = 0xFF;
    uint8_t configVersion = 0x05;
    uint8_t flags = 0;
    uint16_t debounceNodelayMilliseconds = 0;
    uint16_t debounceDelayMilliseconds = 0;
    uint16_t panelDebounceMicroseconds = 4000;
    uint8_t autoCalibrationMaxDeviation = 100;
    uint8_t badSensorMinimumDelaySeconds = 15;
    uint16_t autoCalibrationAveragesPerUpdate = 60;
    uint16_t autoCalibrationSamplesPerAverage = 500;
    uint16_t autoCalibrationMaxTare = 0xFFFF;
    uint8_t enabledSensors[5];
    uint8_t autoLightsTimeout = 1000/128;
    uint8_t stepColor[3*9];
    uint8_t platformStripColor[3];
    uint16_t autoLightPanelMask = 0xFFFF;
    uint8_t panelRotation;
    packed_sensor_settings_t panelSettings[9];
    uint8_t preDetailsDelayMilliseconds = 5;
    uint8_t padding[49];
};
#pragma pack(pop)

void ConvertToNewConfig(const std::vector<uint8_t> &oldConfig, SMXConfig &newConfig);

#endif
