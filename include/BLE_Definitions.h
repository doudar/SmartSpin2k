/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

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
  enum Types : uint {
    ReservedForFutureUse                  = 0x00,
    Reset                                 = 0x01,
    StoppedOrPausedByUser                 = 0x02,
    StoppedOrPausedBySafetyKey            = 0x03,
    StartedOrResumedByUser                = 0x04,
    TargetSpeedChanged                    = 0x05,
    TargetInclineChanged                  = 0x06,
    TargetResistanceLevelChanged          = 0x07,
    TargetPowerChanged                    = 0x08,
    TargetHeartRateChanged                = 0x09,
    IndoorBikeSimulationParametersChanged = 0x12,
    WheelCircumferenceChanged             = 0x13,
    SpinDownStatus                        = 0x14,
    TargetedCadenceChanged                = 0x15,
    // Reserved for Future Use 0x16-0xFE
    ControlPermissionLost = 0xFF
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