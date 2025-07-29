/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

// The .xml file is wrong, make sure to reference the actual FTMS .pdf
struct FitnessMachineIndoorBikeDataFlags {
  enum Types : uint16_t {
    MoreDataBit                 = 1U << 0,
    AverageSpeedPresent         = 1U << 1,
    InstantaneousCadencePresent = 1U << 2,
    AverageCadencePresent       = 1U << 3,
    TotalDistancePresent        = 1U << 4,
    ResistanceLevelPresent      = 1U << 5,
    InstantaneousPowerPresent   = 1U << 6,
    AveragePowerPresent         = 1U << 7,
    ExpendedEnergyPresent       = 1U << 8,
    HeartRatePresent            = 1U << 9,
    MetabolicEquivalentPresent  = 1U << 10,
    ElapsedTimePresent          = 1U << 11,
    RemainingTimePresent        = 1U << 12
  };
};

inline FitnessMachineIndoorBikeDataFlags::Types operator|(FitnessMachineIndoorBikeDataFlags::Types a, FitnessMachineIndoorBikeDataFlags::Types b) {
  return static_cast<FitnessMachineIndoorBikeDataFlags::Types>(static_cast<int>(a) | static_cast<int>(b));
}

// https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/
// Table 4.13: Training Status Field Definition
struct FitnessMachineTrainingStatus {
  enum Types : uint8_t {
    Other                           = 0x00,
    Idle                            = 0x01,
    WarmingUp                       = 0x02,
    LowIntensityInterval            = 0x03,
    HighIntensityInterval           = 0x04,
    RecoveryInterval                = 0x05,
    Isometric                       = 0x06,
    HeartRateControl                = 0x07,
    FitnessTest                     = 0x08,
    SpeedOutsideOfControlRegionLow  = 0x09,
    SpeedOutsideOfControlRegionHigh = 0x0A,
    CoolDown                        = 0x0B,
    WattControl                     = 0x0C,
    ManualMode                      = 0x0D,
    PreWorkout                      = 0x0E,
    PostWorkout                     = 0x0F,
    // Reserved for Future Use 0x10-0xFF
  };
};

// https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/
// Table 4.24: Fitness Machine Control Point characteristic – Result Codes
struct FitnessMachineControlPointResultCode {
  enum Types : uint8_t {
    ReservedForFutureUse = 0x00,
    Success              = 0x01,
    OpCodeNotSupported   = 0x02,
    InvalidParameter     = 0x03,
    OperationFailed      = 0x04,
    ControlNotPermitted  = 0x05,
    // Reserved for Future Use = 0x06-0xFF
  };
};

// https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/
// Table 4.3: Definition of the bits of the Fitness Machine Features field
struct FitnessMachineFeatureFlags {
  enum Types : uint {
    AverageSpeedSupported              = 1U << 0,
    CadenceSupported                   = 1U << 1,
    TotalDistanceSupported             = 1U << 2,
    InclinationSupported               = 1U << 3,
    ElevationGainSupported             = 1U << 4,
    PaceSupported                      = 1U << 5,
    StepCountSupported                 = 1U << 6,
    ResistanceLevelSupported           = 1U << 7,
    StrideCountSupported               = 1U << 8,
    ExpendedEnergySupported            = 1U << 9,
    HeartRateMeasurementSupported      = 1U << 10,
    MetabolicEquivalentSupported       = 1U << 11,
    ElapsedTimeSupported               = 1U << 12,
    RemainingTimeSupported             = 1U << 13,
    PowerMeasurementSupported          = 1U << 14,
    ForceOnBeltAndPowerOutputSupported = 1U << 15,
    UserDataRetentionSupported         = 1U << 16
  };
};

// https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/
// Table 4.4: Definition of the bits of the Target Setting Features field
struct FitnessMachineTargetFlags {
  enum Types : uint {
    SpeedTargetSettingSupported                           = 1U << 0,
    InclinationTargetSettingSupported                     = 1U << 1,
    ResistanceTargetSettingSupported                      = 1U << 2,
    PowerTargetSettingSupported                           = 1U << 3,
    HeartRateTargetSettingSupported                       = 1U << 4,
    TargetedExpendedEnergyConfigurationSupported          = 1U << 5,
    TargetedStepNumberConfigurationSupported              = 1U << 6,
    TargetedStrideNumberConfigurationSupported            = 1U << 7,
    TargetedDistanceConfigurationSupported                = 1U << 8,
    TargetedTrainingTimeConfigurationSupported            = 1U << 9,
    TargetedTimeTwoHeartRateZonesConfigurationSupported   = 1U << 10,
    TargetedTimeThreeHeartRateZonesConfigurationSupported = 1U << 11,
    TargetedTimeFiveHeartRateZonesConfigurationSupported  = 1U << 12,
    IndoorBikeSimulationParametersSupported               = 1U << 13,
    WheelCircumferenceConfigurationSupported              = 1U << 14,
    SpinDownControlSupported                              = 1U << 15,
    TargetedCadenceConfigurationSupported                 = 1U << 16
  };
};

// https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/
// Table 4.16.1: Fitness Machine Control Point Procedure Requirements
struct FitnessMachineControlPointProcedure {
  enum Types : uint8_t {
    RequestControl                    = 0x00,
    Reset                             = 0x01,
    SetTargetSpeed                    = 0x02,
    SetTargetInclination              = 0x03,
    SetTargetResistanceLevel          = 0x04,
    SetTargetPower                    = 0x05,
    SetTargetHeartRate                = 0x06,
    StartOrResume                     = 0x07,
    StopOrPause                       = 0x08,
    SetIndoorBikeSimulationParameters = 0x11,
    SetWheelCircumference             = 0x12,
    SpinDownControl                   = 0x13,
    SetTargetedCadence                = 0x14,
    // Reserved for Future Use 0x15-0x7F
    ResponseCode = 0x80
    // Reserved for Future Use 0x81-0xFF
  };
};

// https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/
// Table 4.17: Fitness Machine Status
struct FitnessMachineStatus {
  enum Types : uint8_t {
    ReservedForFutureUse                  = 0x00,
    Reset                                 = 0x01,
    StoppedOrPausedByUser                 = 0x02,
    StoppedBySafetyKey                    = 0x03,
    StartedOrResumedByUser                = 0x04,
    TargetSpeedChanged                    = 0x05,
    TargetInclineChanged                  = 0x06,
    TargetResistanceLevelChanged          = 0x07,
    TargetPowerChanged                    = 0x08,
    TargetHeartRateChanged                = 0x09,
    TargetedExpendedEnergyChanged         = 0x0A,
    TargetedNumberofStepsChanged          = 0x0B,
    TargetedNumberofStridesChanged        = 0x0C,
    TargetedDistanceChanged               = 0x0D,
    TargetedTrainingTimeChanged           = 0x0E,
    TargetedTimeinTwoHeartRateZonesChanged = 0x0F,
    TargetedTimeinThreeHeartRateZonesChanged = 0x10,
    TargetedTimeinFiveHeartRateZonesChanged = 0x11,
    IndoorBikeSimulationParametersChanged = 0x12,
    WheelCircumferenceChanged             = 0x13,
    SpinDownStatus                        = 0x14,
    TargetedCadenceChanged                = 0x15,
    // Reserved for Future Use 0x16-0xFE
    ControlPermissionLost                 = 0xFF
  };
  // Table 4.27: Spin Down Status value definition
  // These are the parameter values for the SpinDownStatus (0x14) Op Code
  enum SpinDownParameters : uint8_t {
    SpinDown_Reserved          = 0x00,
    SpinDown_SpinDownRequested = 0x01,
    SpinDown_Success           = 0x02,
    SpinDown_Error             = 0x03,
    SpinDown_StopPedaling      = 0x04
    // Reserved for Future Use 0x05-0xFF
  };
};

inline FitnessMachineFeatureFlags::Types operator|(FitnessMachineFeatureFlags::Types a, FitnessMachineFeatureFlags::Types b) {
  return static_cast<FitnessMachineFeatureFlags::Types>(static_cast<int>(a) | static_cast<int>(b));
}

inline FitnessMachineTargetFlags::Types operator|(FitnessMachineTargetFlags::Types a, FitnessMachineTargetFlags::Types b) {
  return static_cast<FitnessMachineTargetFlags::Types>(static_cast<int>(a) | static_cast<int>(b));
}

struct FitnessMachineFeature {
  union {
    struct {
      union {
        enum FitnessMachineFeatureFlags::Types featureFlags;
        uint8_t bytes[4];
      } featureFlags;
      union {
        enum FitnessMachineTargetFlags::Types targetFlags;
        uint8_t bytes[4];
      } targetFlags;
    };
    uint8_t bytes[8];
  };
};

struct FtmsStatus {
  uint8_t data[8];
  int length;
};

// https://www.bluetooth.com/specifications/specs/cpp-1-1-html/
// See "4.4. Cycling Power Feature"
struct CyclingPowerFeatureFlags {
  enum Types : uint32_t {
    PedalPowerBalanceSupported           = 1U << 0,
    AccumulatedTorqueSupported           = 1U << 1,
    WheelRevolutionDataSupported         = 1U << 2,
    CrankRevolutionDataSupported         = 1U << 3,
    ExtremeMagnitudesSupported           = 1U << 4,
    ExtremesAnglesSupported              = 1U << 5,
    TopBottomDeadSpotAnglesSupported     = 1U << 6,
    AccumulatedEnergySupported           = 1U << 7,
    ExtremeTorquesSupported              = 1U << 8,
    OffsetCompensationIndicatorSupported = 1U << 9,
  };
};

inline CyclingPowerFeatureFlags::Types operator|(CyclingPowerFeatureFlags::Types a, CyclingPowerFeatureFlags::Types b) {
  return static_cast<CyclingPowerFeatureFlags::Types>(static_cast<int>(a) | static_cast<int>(b));
}

class CyclingPowerMeasurement {
 public:
  // Flags definition as per specification
  struct Flags {
    uint16_t pedalPowerBalancePresent : 1;
    uint16_t pedalPowerBalanceReference : 1;
    uint16_t accumulatedTorquePresent : 1;
    uint16_t accumulatedTorqueSource : 1;
    uint16_t wheelRevolutionDataPresent : 1;
    uint16_t crankRevolutionDataPresent : 1;
    uint16_t extremeForceMagnitudesPresent : 1;
    uint16_t extremeTorqueMagnitudesPresent : 1;
    uint16_t extremeAnglesPresent : 1;
    uint16_t topDeadSpotAnglePresent : 1;
    uint16_t bottomDeadSpotAnglePresent : 1;
    uint16_t accumulatedEnergyPresent : 1;
    uint16_t offsetCompensationIndicator : 1;
    uint16_t reserved : 3;
  } flags;

  // Assuming these are the possible data fields based on flags
  int16_t instantaneousPower;  // Mandatory
  // Other fields as optional, based on the flags
  uint8_t pedalPowerBalance;  // Example optional field
  uint16_t accumulatedTorque;
  uint32_t cumulativeWheelRevolutions;
  uint16_t lastWheelEventTime;
  uint16_t cumulativeCrankRevolutions;
  uint16_t lastCrankEventTime;

  std::vector<uint8_t> toByteArray() {
    std::vector<uint8_t> data;
    // Add flags to data vector
    data.push_back(static_cast<uint8_t>(*(reinterpret_cast<uint16_t*>(&flags)) & 0xFF));
    data.push_back(static_cast<uint8_t>((*(reinterpret_cast<uint16_t*>(&flags)) >> 8) & 0xFF));

    // Add Instantaneous Power
    data.push_back(static_cast<uint8_t>(instantaneousPower & 0xFF));
    data.push_back(static_cast<uint8_t>((instantaneousPower >> 8) & 0xFF));

    // Conditional fields based on flags
    if (flags.wheelRevolutionDataPresent) {
      // Add wheel revolution data if present
      data.push_back(static_cast<uint8_t>(cumulativeWheelRevolutions & 0xFF));
      data.push_back(static_cast<uint8_t>((cumulativeWheelRevolutions >> 8) & 0xFF));
      data.push_back(static_cast<uint8_t>((cumulativeWheelRevolutions >> 16) & 0xFF));
      data.push_back(static_cast<uint8_t>((cumulativeWheelRevolutions >> 24) & 0xFF));

      data.push_back(static_cast<uint8_t>(lastWheelEventTime & 0xFF));
      data.push_back(static_cast<uint8_t>((lastWheelEventTime >> 8) & 0xFF));
    }
    // Conditional fields based on flags
    if (flags.crankRevolutionDataPresent) {
      // Add crank revolution data if present
      data.push_back(static_cast<uint8_t>(cumulativeCrankRevolutions & 0xFF));
      data.push_back(static_cast<uint8_t>((cumulativeCrankRevolutions >> 8) & 0xFF));

      data.push_back(static_cast<uint8_t>(lastCrankEventTime & 0xFF));
      data.push_back(static_cast<uint8_t>((lastCrankEventTime >> 8) & 0xFF));
    }

    return data;
  }
};

// https://www.bluetooth.com/specifications/specs/cscs-1-0/
// See "3.2. CSC Feature"
struct CyclingSpeedCadenceFeatureFlags {
  enum Types : uint16_t {
    WheelRevolutionDataSupported     = 1U << 0,
    CrankRevolutionDataSupported     = 1U << 1,
    MultipleSensorLocationsSupported = 1U << 2
    // 3-15: Reserved for Future Use
  };
};

inline CyclingSpeedCadenceFeatureFlags::Types operator|(CyclingSpeedCadenceFeatureFlags::Types a, CyclingSpeedCadenceFeatureFlags::Types b) {
  return static_cast<CyclingSpeedCadenceFeatureFlags::Types>(static_cast<int>(a) | static_cast<int>(b));
}

class CscMeasurement {
 public:
  // Flags definition as per specification
  struct Flags {
    uint8_t wheelRevolutionDataPresent : 1;
    uint8_t crankRevolutionDataPresent : 1;
    uint8_t reserved : 6;
  } flags;

  // Data fields
  uint32_t cumulativeWheelRevolutions;
  uint16_t lastWheelEventTime;  // Resolution of 1/1024 seconds
  uint16_t cumulativeCrankRevolutions;
  uint16_t lastCrankEventTime;  // Resolution of 1/1024 seconds

  CscMeasurement() : cumulativeWheelRevolutions(0), lastWheelEventTime(0), cumulativeCrankRevolutions(0), lastCrankEventTime(0) {
    // Clear all flags initially
    *(reinterpret_cast<uint8_t*>(&flags)) = 0;
  }

  std::vector<uint8_t> toByteArray() {
    std::vector<uint8_t> data;

    // Add flags to data vector
    data.push_back(*(reinterpret_cast<uint8_t*>(&flags)));

    // Conditional fields based on flags
    if (flags.wheelRevolutionDataPresent) {
      // Add wheel revolution data if present
      for (int i = 0; i < 4; ++i) {
        data.push_back((cumulativeWheelRevolutions >> (i * 8)) & 0xFF);
      }
      for (int i = 0; i < 2; ++i) {
        data.push_back((lastWheelEventTime >> (i * 8)) & 0xFF);
      }
    }

    if (flags.crankRevolutionDataPresent) {
      // Add crank revolution data if present
      for (int i = 0; i < 2; ++i) {
        data.push_back((cumulativeCrankRevolutions >> (i * 8)) & 0xFF);
      }
      for (int i = 0; i < 2; ++i) {
        data.push_back((lastCrankEventTime >> (i * 8)) & 0xFF);
      }
    }

    return data;
  }
};
