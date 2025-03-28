#include <Calculations.h>
#include <cmath>
#include <stdio.h>
#include <Logger.h>

double const Calculations::pi = 3.14159;
double const Calculations::gravity = 9.81;
double const Calculations::rollingResistanceCoefficient = 0.00415; // Asphalt Road
double const Calculations::windResistanceCoefficient = 0.51; // Road bike + touring position
double const Calculations::wheelDiameter = 0.7;

/**
 * Calculates the gravitational resistance based on the total weight and the grade
 * 
 * @param totalWeight Total weight of the bike and the rider in kg
 * @return Gravitational resistance in N
 */
double Calculations::calculateGravitationalResistance(double totalWeight, double grade) {
  double gravitationalResistance = gravity * sin(atan(grade/100)) * totalWeight;
  log_d("Gravitational resistance: %f", gravitationalResistance);
  return gravitationalResistance;
}

/**
 * Calculates the rolling resistance based on the total weight
 * 
 * @param totalWeight Total weight of the bike and the rider in kg
 * @return Rolling resistance in N
 */
double Calculations::calculateRollingResistance(double totalWeight) {
  double rollingResistance = gravity * totalWeight * rollingResistanceCoefficient;
  log_d("Rolling resistance: %f", rollingResistance);
  return rollingResistance;
}

/**
 * Calculates the wind resistance based on the bicycle and wind speed
 * 
 * @param bicycleSpeed Speed of bicycle in m/s
 * @param windSpeed Speed of wind in m/s (positive for headwind, negative for tailwind)
 * @return Wind resistance in N
 */
double Calculations::calculateWindResistance(double bicycleSpeed, double windSpeed) {
  double windResistance = 0.5 * windResistanceCoefficient * pow(bicycleSpeed + windSpeed, 2);
  log_d("Wind resistance: %f", windResistance);
  return windResistance;
}

/**
 * Calculates the total resistance
 * 
 * @param totalWeight Total weight of the bike and the rider in kg
 * @param grade Grade in percentage
 * @param speed Speed of bicycle in m/s
 * @param gearRatio Requested gear ratio
 * @param defaultGearRatio Default gear ratio of the bike (chainring / sprocket)
 * @param difficulty Difficulty in percentage (0-200%)
 * @return Total resistance in N
 */
double Calculations::calculateGearedResistance(double totalWeight, double grade, double speed, double gearRatio, double defaultGearRatio, uint16_t difficulty) {
  // calculate the relative gear ratio
  double relativeGearRatio = calculateRelativeGearRatio(gearRatio, defaultGearRatio);

  // calculate the wind resistance based on speed and gear ratio
  double windResistance = calculateWindResistance(speed, 0);

  // calculate the gravitational and rolling resistance 
  double gravitationalResistance = calculateGravitationalResistance(totalWeight, grade);
  double rollingResistance = calculateRollingResistance(totalWeight);

  // apply the difficulty to the gravitational and wind resistance
  gravitationalResistance = gravitationalResistance * (difficulty / 100.0);
  windResistance = windResistance * (difficulty / 100.0);

  // calculate the geared total resistance (that would apply with the specified gear ratio)
  double gearedTotalResistance = calculateGearedValue(gravitationalResistance + rollingResistance, relativeGearRatio) + windResistance;
  log_d("Geared resistance: %fN (gravitational: %fN, rolling: %fN, wind: %fN, speed: %fm/s = %fkm/h, difficulty: %d)", gearedTotalResistance, gravitationalResistance, rollingResistance, windResistance, speed, speed * 3.6, difficulty);
  return gearedTotalResistance;
}

/**
 * Calculates the FE-C basic resistance percentage value for the simulation
 * 
 * @param totalWeight Total weight of the bike and the rider in kg
 * @param grade Grade in percentage
 * @param measuredSpeed Measured speed from trainer in m/s
 * @param cadence Cadence in RPM
 * @param gearRatio Requested gear ratio
 * @param defaultGearRatio Default gear ratio of the bike (chainring / sprocket)
 * @param maximumResistance Maximum resistance in N from trainer
 * @param difficulty Difficulty in percentage (0-200%)
 * @return FE-C basic resistance percentage value in 0.5% (0x00 = 0%, 0xC8 = 100%)
 */
uint8_t Calculations::calculateFECBasicResistancePercentageValue(double totalWeight, double grade, double measuredSpeed, uint8_t cadence, double gearRatio, double defaultGearRatio, uint16_t maximumResistance, uint16_t difficulty) {
  uint8_t basicResistancePercentageValue = 200;

  // calculate the speed based on cadence, wheel diameter and gear ratio
  double speed = calculateSpeed(cadence, wheelDiameter, gearRatio);

  // calculate the geared total resistance
  double gearedTotalResistance = calculateGearedResistance(totalWeight, grade, speed, gearRatio, defaultGearRatio, difficulty);

  if (gearedTotalResistance < 0) {
    basicResistancePercentageValue = 0;
  } else if ((maximumResistance != 0) && (gearedTotalResistance <= maximumResistance)) {
    basicResistancePercentageValue = round(gearedTotalResistance / maximumResistance * 200);
  } 
  log_d("FE-C Basic resistance value: %d (%fN = %f%% of %dN)", basicResistancePercentageValue, gearedTotalResistance, basicResistancePercentageValue / 2.0, maximumResistance);
  return basicResistancePercentageValue;
}

/**
 * Calculates the FE-C track resistance grade for the simulation
 * 
 * @param totalWeight Total weight of the bike and the rider in kg
 * @param grade Grade in percentage
 * @param measuredSpeed Measured speed from trainer in m/s
 * @param cadence Cadence in RPM
 * @param gearRatio Requested gear ratio
 * @param defaultGearRatio Default gear ratio of the bike (chainring / sprocket)
 * @param difficulty Difficulty in percentage (0-200%)
 * @return FE-C resistance grade in 0.01% (0x4E20 = 0%)
 */
uint16_t Calculations::calculateFECTrackResistanceGrade(double totalWeight, double grade, double measuredSpeed, uint8_t cadence, double gearRatio, double defaultGearRatio, uint16_t difficulty) {
  uint16_t trackResistanceGrade = 0x4E20;
  
  // calculate the speed and realSpeed based on cadence, wheel diameter and gear ratio
  double speed = calculateSpeed(cadence, wheelDiameter, gearRatio);
  double realSpeed = calculateSpeed(cadence, wheelDiameter, defaultGearRatio);

  // calculate the wind resistance (including difficulty) based on realSpeed
  double windResistance = calculateWindResistance(realSpeed, 0);
  windResistance = windResistance * (difficulty / 100.0);

  // calculate the rolling resistance
  double rollingResistance = calculateRollingResistance(totalWeight);

  // calculate the geared total resistance
  double gearedTotalResistance = calculateGearedResistance(totalWeight, grade, speed, gearRatio, defaultGearRatio, difficulty);

  // calculate the geared grade based on the theoretical gravitational resistance
  double theoreticalGravitationalResistance = gearedTotalResistance - windResistance - rollingResistance;
  double calculatedGrade = (tan(asin(theoreticalGravitationalResistance / totalWeight / gravity)) * 100) - 1;

  trackResistanceGrade += (calculatedGrade * 100);
  log_d("FE-C Track resistance grade: %d (%f%)", trackResistanceGrade, calculatedGrade);
  return trackResistanceGrade;
}

/**
 * Calculates the FE-C target power value for the simulation
 * 
 * @param totalWeight Total weight of the bike and the rider in kg
 * @param grade Grade in percentage
 * @param measuredSpeed Measured speed from trainer in m/s
 * @param cadence Cadence in RPM
 * @param gearRatio Requested gear ratio
 * @param defaultGearRatio Default gear ratio of the bike (chainring / sprocket)
 * @param difficulty Difficulty in percentage (0-200%)
 * @return FE-C target power value in 0.25W (0x00 = 0W)
 */
uint16_t Calculations::calculateFECTargetPowerValue(double totalWeight, double grade, double measuredSpeed, uint8_t cadence, double gearRatio, double defaultGearRatio, uint16_t difficulty) {
  uint16_t targetPowerValue = 0;

  // calculate the speed based on cadence, wheel diameter and gear ratio
  double speed = calculateSpeed(cadence, wheelDiameter, gearRatio);
  double trainerSpeed = calculateSpeed(cadence, wheelDiameter, defaultGearRatio);

  // calculate the geared total resistance
  double gearedTotalResistance = calculateGearedResistance(totalWeight, grade, speed, gearRatio, defaultGearRatio, difficulty);

  // calculate the power based on the total resistance and speed
  double power = gearedTotalResistance * trainerSpeed;
  if (power < 0) {
    power = 0;
  }
  targetPowerValue = round(power * 4);
  log_d("FE-C Target power value: %d (%fW)", targetPowerValue, power);
  return targetPowerValue;
}

/**
 * Calculates the speed based on the cadence, wheel diameter and gear ratio
 * 
 * @param cadence Cadence in RPM
 * @param wheelDiameter Wheel diameter in m
 * @param gearRatio Gear ratio
 * @return Speed in m/s
 */
double Calculations::calculateSpeed(uint8_t cadence, double wheelDiameter, double gearRatio) {
  return ((cadence * gearRatio) * (wheelDiameter * pi) / 60);
}

/**
 * Calculates the geared value based on the relative gear ratio
 * 
 * @param originalValue Original value (unit doesn't matter)
 * @param gearRatio Gear ratio
 * @return Geared value (unit as original value)
 */
double Calculations::calculateGearedValue(double originalValue, double gearRatio) {
  double gearedValue = originalValue * gearRatio;
  if (originalValue < 0) {
    gearedValue = originalValue * (1 / gearRatio);
  }
  log_d("Original / Ratio / Geared: %f / %f / %f", originalValue, gearRatio, gearedValue);
  return gearedValue;
}

/**
 * Calculates the relative gear ratio based on the software requested and hardware installed gear ratio
 * 
 * @param gearRatio Requested gear ratio
 * @param defaultGearRatio Default gear ratio of the bike (chainring / sprocket)
 * @return Relative gear ratio
 */
double Calculations::calculateRelativeGearRatio(double gearRatio, double defaultGearRatio) {
  double relativeGearRatio = gearRatio / defaultGearRatio;
  log_d("Relative gear ratio: %f (%f / %f)", relativeGearRatio, gearRatio, defaultGearRatio);
  return relativeGearRatio;
}