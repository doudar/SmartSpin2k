/* GENERATED compatibility bridge. Opt in by including this header. */
#ifndef SS2K_LEGACY_PROTOCOL_H
#define SS2K_LEGACY_PROTOCOL_H
#include "ss2k_protocol.h"
#define cc_read SS2K_READ
#define cc_write SS2K_WRITE
#define cc_success SS2K_SUCCESS
#define cc_error SS2K_ERROR
#define BLE_firmwareUpdateURL SS2K_ID_FIRMWARE_UPDATE_URL
#define BLE_incline SS2K_ID_INCLINE
#define BLE_simulatedWatts SS2K_ID_SIMULATED_WATTS
#define BLE_simulatedHr SS2K_ID_SIMULATED_HR
#define BLE_simulatedCad SS2K_ID_SIMULATED_CAD
#define BLE_simulatedSpeed SS2K_ID_SIMULATED_SPEED
#define BLE_deviceName SS2K_ID_DEVICE_NAME
#define BLE_shiftStep SS2K_ID_SHIFT_STEP
#define BLE_stepperPower SS2K_ID_STEPPER_POWER
#define BLE_stealthChop SS2K_ID_STEALTH_CHOP
#define BLE_inclineMultiplier SS2K_ID_INCLINE_MULTIPLIER
#define BLE_powerCorrectionFactor SS2K_ID_POWER_CORRECTION_FACTOR
#define BLE_simulateHr SS2K_ID_SIMULATE_HR
#define BLE_simulateWatts SS2K_ID_SIMULATE_WATTS
#define BLE_simulateCad SS2K_ID_SIMULATE_CAD
#define BLE_FTMSMode SS2K_ID_FTMSMODE
#define BLE_autoUpdate SS2K_ID_AUTO_UPDATE
#define BLE_ssid SS2K_ID_SSID
#define BLE_password SS2K_ID_PASSWORD
#define BLE_foundDevices SS2K_ID_FOUND_DEVICES
#define BLE_connectedPowerMeter SS2K_ID_CONNECTED_POWER_METER
#define BLE_connectedHeartMonitor SS2K_ID_CONNECTED_HEART_MONITOR
#define BLE_shifterPosition SS2K_ID_SHIFTER_POSITION
#define BLE_saveToLittleFS SS2K_ID_SAVE_TO_LITTLE_FS
#define BLE_targetPosition SS2K_ID_TARGET_POSITION
#define BLE_externalControl SS2K_ID_EXTERNAL_CONTROL
#define BLE_syncMode SS2K_ID_SYNC_MODE
#define BLE_reboot SS2K_ID_REBOOT
#define BLE_resetToDefaults SS2K_ID_RESET_TO_DEFAULTS
#define BLE_stepperSpeed SS2K_ID_STEPPER_SPEED
#define BLE_ERGSensitivity SS2K_ID_ERGSENSITIVITY
#define BLE_shiftDir SS2K_ID_SHIFT_DIR
#define BLE_minBrakeWatts SS2K_ID_MIN_BRAKE_WATTS
#define BLE_maxBrakeWatts SS2K_ID_MAX_BRAKE_WATTS
#define BLE_restartBLE SS2K_ID_RESTART_BLE
#define BLE_scanBLE SS2K_ID_SCAN_BLE
#define BLE_firmwareVer SS2K_ID_FIRMWARE_VER
#define BLE_resetPowerTable SS2K_ID_RESET_POWER_TABLE
#define BLE_powerTableData SS2K_ID_POWER_TABLE_DATA
#define BLE_simulatedTargetWatts SS2K_ID_SIMULATED_TARGET_WATTS
#define BLE_simulateTargetWatts SS2K_ID_SIMULATE_TARGET_WATTS
#define BLE_hMin SS2K_ID_H_MIN
#define BLE_hMax SS2K_ID_H_MAX
#define BLE_homingSensitivity SS2K_ID_HOMING_SENSITIVITY
#define BLE_pTab4Pwr SS2K_ID_P_TAB4_PWR
#define BLE_UDPLogging SS2K_ID_UDPLOGGING
#define BLE_hardwareVersion SS2K_ID_HARDWARE_VERSION
#define BLE_BLELogging SS2K_ID_BLELOGGING
#define BLE_allSettings SS2K_ID_ALL_SETTINGS
#define BLE_scanResults SS2K_ID_SCAN_RESULTS
#define BLE_gearTeeth SS2K_ID_GEAR_TEETH
#define BLE_gearPreset SS2K_ID_GEAR_PRESET
#define BLE_capabilities SS2K_ID_CAPABILITIES
#define BLE_forgetPeer SS2K_ID_FORGET_PEER
#define BLE_firmwareStream SS2K_ID_FIRMWARE_STREAM
#define CUSTOM_CAPABILITIES_VERSION SS2K_CAPABILITIES_VERSION
#define CUSTOM_CAPABILITY_PASSWORD_WRITE_ONLY SS2K_CAP_PASSWORD_WRITE_ONLY
static inline uint32_t customCharacteristicCapabilities(void) { return SS2K_CAP_PASSWORD_WRITE_ONLY | SS2K_CAP_SECURE_SETTINGS; }
typedef enum { CustomAction, CustomBoolean, CustomUnsigned16, CustomSigned16, CustomSigned32, CustomString, CustomPowerTableRow, CustomSettingsSnapshot, CustomScanResultStream, CustomBooleanWriteStringRead, CustomGearTeeth, CustomCapabilities, CustomFirmwareStream, CustomUnknown } CustomCharacteristicValueFormat;
static inline CustomCharacteristicValueFormat customCharacteristicValueFormat(uint8_t id) {
  switch (id) {
    case 2: return CustomSigned16;
    case 3: return CustomUnsigned16;
    case 4: return CustomUnsigned16;
    case 5: return CustomUnsigned16;
    case 6: return CustomUnsigned16;
    case 7: return CustomString;
    case 8: return CustomUnsigned16;
    case 9: return CustomUnsigned16;
    case 10: return CustomBoolean;
    case 11: return CustomSigned16;
    case 12: return CustomUnsigned16;
    case 13: return CustomBoolean;
    case 14: return CustomBoolean;
    case 15: return CustomBoolean;
    case 16: return CustomUnsigned16;
    case 18: return CustomString;
    case 19: return CustomString;
    case 20: return CustomString;
    case 21: return CustomString;
    case 22: return CustomString;
    case 23: return CustomSigned16;
    case 24: return CustomAction;
    case 25: return CustomSigned32;
    case 26: return CustomBoolean;
    case 27: return CustomBoolean;
    case 28: return CustomAction;
    case 29: return CustomAction;
    case 30: return CustomUnsigned16;
    case 31: return CustomUnsigned16;
    case 32: return CustomBoolean;
    case 33: return CustomUnsigned16;
    case 34: return CustomUnsigned16;
    case 35: return CustomAction;
    case 36: return CustomAction;
    case 37: return CustomString;
    case 38: return CustomAction;
    case 39: return CustomPowerTableRow;
    case 40: return CustomUnsigned16;
    case 41: return CustomBoolean;
    case 42: return CustomSigned32;
    case 43: return CustomSigned32;
    case 44: return CustomUnsigned16;
    case 45: return CustomBoolean;
    case 46: return CustomBoolean;
    case 47: return CustomString;
    case 48: return CustomBooleanWriteStringRead;
    case 49: return CustomSettingsSnapshot;
    case 50: return CustomScanResultStream;
    case 52: return CustomGearTeeth;
    case 53: return CustomUnsigned16;
    case 54: return CustomCapabilities;
    case 55: return CustomAction;
    case 56: return CustomFirmwareStream;
    default: return CustomUnknown;
  }
}
#endif
