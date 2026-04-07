#include "SMXConfigPacket.h"
#include <cstring>
#include <cstddef>

#pragma pack(push, 1)
struct OldSMXConfig
{
    uint8_t unused1 = 0xFF, unused2 = 0xFF;
    uint8_t unused3 = 0xFF, unused4 = 0xFF;
    uint8_t unused5 = 0xFF, unused6 = 0xFF;
    uint16_t masterDebounceMilliseconds = 0;
    uint8_t panelThreshold7Low = 0xFF, panelThreshold7High = 0xFF;
    uint8_t panelThreshold4Low = 0xFF, panelThreshold4High = 0xFF;
    uint8_t panelThreshold2Low = 0xFF, panelThreshold2High = 0xFF;
    uint16_t panelDebounceMicroseconds = 4000;
    uint16_t autoCalibrationPeriodMilliseconds = 1000;
    uint8_t autoCalibrationMaxDeviation = 100;
    uint8_t badSensorMinimumDelaySeconds = 15;
    uint16_t autoCalibrationAveragesPerUpdate = 60;
    uint8_t unused7 = 0xFF, unused8 = 0xFF;
    uint8_t panelThreshold1Low = 0xFF, panelThreshold1High = 0xFF;
    uint8_t enabledSensors[5];
    uint8_t autoLightsTimeout = 1000/128;
    uint8_t stepColor[3*9];
    uint8_t panelRotation;
    uint16_t autoCalibrationSamplesPerAverage = 500;
    uint8_t masterVersion = 0xFF;
    uint8_t configVersion = 0x03;
    uint8_t unused9[10];
    uint8_t panelThreshold0Low, panelThreshold0High;
    uint8_t panelThreshold3Low, panelThreshold3High;
    uint8_t panelThreshold5Low, panelThreshold5High;
    uint8_t panelThreshold6Low, panelThreshold6High;
    uint8_t panelThreshold8Low, panelThreshold8High;
    uint16_t debounceDelayMilliseconds = 0;
    uint8_t padding[164];
};
#pragma pack(pop)

void ConvertToNewConfig(const std::vector<uint8_t> &oldConfigData, SMXConfig &newConfig)
{
    const OldSMXConfig &old = *(const OldSMXConfig *)oldConfigData.data();

    newConfig.debounceNodelayMilliseconds = old.masterDebounceMilliseconds;

    newConfig.panelSettings[7].loadCellLowThreshold = old.panelThreshold7Low;
    newConfig.panelSettings[4].loadCellLowThreshold = old.panelThreshold4Low;
    newConfig.panelSettings[2].loadCellLowThreshold = old.panelThreshold2Low;
    newConfig.panelSettings[7].loadCellHighThreshold = old.panelThreshold7High;
    newConfig.panelSettings[4].loadCellHighThreshold = old.panelThreshold4High;
    newConfig.panelSettings[2].loadCellHighThreshold = old.panelThreshold2High;

    newConfig.panelDebounceMicroseconds = old.panelDebounceMicroseconds;
    newConfig.autoCalibrationMaxDeviation = old.autoCalibrationMaxDeviation;
    newConfig.badSensorMinimumDelaySeconds = old.badSensorMinimumDelaySeconds;
    newConfig.autoCalibrationAveragesPerUpdate = old.autoCalibrationAveragesPerUpdate;

    newConfig.panelSettings[1].loadCellLowThreshold = old.panelThreshold1Low;
    newConfig.panelSettings[1].loadCellHighThreshold = old.panelThreshold1High;

    memcpy(newConfig.enabledSensors, old.enabledSensors, sizeof(newConfig.enabledSensors));
    newConfig.autoLightsTimeout = old.autoLightsTimeout;
    memcpy(newConfig.stepColor, old.stepColor, sizeof(newConfig.stepColor));
    newConfig.panelRotation = old.panelRotation;
    newConfig.autoCalibrationSamplesPerAverage = old.autoCalibrationSamplesPerAverage;

    if(old.configVersion == 0xFF)
        return;

    newConfig.masterVersion = old.masterVersion;
    newConfig.configVersion = old.configVersion;

    if(old.configVersion < 2)
        return;

    newConfig.panelSettings[0].loadCellLowThreshold = old.panelThreshold0Low;
    newConfig.panelSettings[3].loadCellLowThreshold = old.panelThreshold3Low;
    newConfig.panelSettings[5].loadCellLowThreshold = old.panelThreshold5Low;
    newConfig.panelSettings[6].loadCellLowThreshold = old.panelThreshold6Low;
    newConfig.panelSettings[8].loadCellLowThreshold = old.panelThreshold8Low;
    newConfig.panelSettings[0].loadCellHighThreshold = old.panelThreshold0High;
    newConfig.panelSettings[3].loadCellHighThreshold = old.panelThreshold3High;
    newConfig.panelSettings[5].loadCellHighThreshold = old.panelThreshold5High;
    newConfig.panelSettings[6].loadCellHighThreshold = old.panelThreshold6High;
    newConfig.panelSettings[8].loadCellHighThreshold = old.panelThreshold8High;

    if(old.configVersion < 3)
        return;

    newConfig.debounceDelayMilliseconds = old.debounceDelayMilliseconds;
}
