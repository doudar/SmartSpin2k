#include <cstdint>
#ifndef FTMS_H
#define FTMS_H

#pragma pack(1) 
typedef struct {
  uint32_t fitnessMachineFeatures;
  uint32_t targetSettingFeatures;
} FITNESS_MACHINE_FEATURES_TYPE;

enum FITNESS_MACHINE_FEATURES {
  average_speed_supported = 0x01,
  cadence_supported = 0x02,
  total_distance_supported = 0x04,
  inclination_supported = 0x08,
  elevation_gain_supported = 0x10,
  pace_supported = 0x20,
  step_count_supported = 0x40,
  resistance_level_supported = 0x80,
  stride_count_supported = 0x100,
  expended_energy_supported = 0x200,
  heart_rate_measurement_supported = 0x400,
  metabolic_equivalent_supported = 0x800,
  elapsed_time_supported = 0x1000,
  remaining_time_supported = 0x2000,
  power_measurement_supported = 0x4000,
  force_on_belt_and_power_output_supported = 0x8000,
  user_data_retention_supported = 0x10000
};

enum TARGET_SETTING_FEATURES {
  speed_target_setting_supported = 0x01,
  inclination_target_setting_supported = 0x02,
  resistance_target_setting_supported = 0x04,
  power_target_setting_supported = 0x08,
  heart_rate_target_setting_supported = 0x10,
  targeted_expended_energy_configuration_supported = 0x20,
  targeted_step_number_configuration_supported = 0x40,
  targeted_stride_number_configuration_supported = 0x80,
  targeted_distance_configuration_supported = 0x100,
  targeted_training_time_configuration_supported = 0x200,
  targeted_time_in_two_heart_rate_zones_configuration_supported = 0x400,
  targeted_time_in_three_heart_rate_zones_configuration_supported = 0x800,
  targeted_time_in_five_heart_rate_zones_configuration_supported = 0x1000,
  indoor_bike_simulation_parameters_supported = 0x2000,
  wheel_circumference_configuration_supported = 0x4000,
  spin_down_control_supported = 0x8000,
  targeted_cadence_configuration_supported = 0x10000
};

enum INDOOR_BIKE_DATA_CHARACTERISTICS_FLAGS {
  more_data = 0x01, // 0=Instantaneous speed field is present, 1=Instantaneous speed field is not present
  average_speed_present = 0x02, // 0=Average speed is not present, 1=Average speed is present
  instantaneous_cadence_present = 0x04, // 0=Instantaneous cadence is present, 1=Instantaneous cadence is not present DOCUMENTATION BUG! It's the other way around
  average_cadence_present = 0x08, // 0=Average cadence is not present, 1=Average cadence is present
  total_distance_present = 0x10, // 0=Total distance is not present, 1=Total distance is present
  resistance_level_present = 0x20, // 0=Resistance level is not present, 1=Resistance level is present
  instantaneous_power_present = 0x40, // 0=Instantaneous power is not present, 1=Instantaneous power is present
  average_power_present = 0x80, // 0=Average power is not present, 1=Average power is present
  expended_energy_present = 0x100, // 0=Expended energy is not present, 1=Expended energy is present
  heart_rate_present = 0x200, // 0=Heart rate is not present, 1=Heart rate is present
  metabolic_equivalent_present = 0x400, // 0=Metabolic equivalent is not present, 1=Metabolic equivalent is present
  elapsed_time_present = 0x800, // 0=Elapsed time is not present, 1=Elapsed time is present
  remaining_time_present = 0x1000 // 0=Remaining time is not present, 1=Remaining time is present
};

enum FITNESS_MACHINE_CONTROL_POINT_RESULT_CODE {
  reserved = 0x00,
  success = 0x01,
  op_code_not_supported = 0x02,
  invalid_parameter = 0x03,
  operation_failed = 0x04,
  control_not_permitted = 0x05
};

#endif