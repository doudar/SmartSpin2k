#include <unity.h>
#include <stdio.h>
#include <Calculations.h>
#include <Logger.h>

double zwiftGears[] = {0.75, 0.87, 0.99, 1.11, 1.23, 1.38, 1.53, 1.68, 1.86, 2.04, 2.22, 2.40, 2.61, 2.82, 3.03, 3.24, 3.49, 3.74, 3.99, 4.24, 4.54, 4.84, 5.14, 5.49};

double totalWeight = 100.0 + 10.0; // 100kg user and 10kg bike weight
double defaultGearRatio = double(34) / double(14); // chainring 34 and sprocket 14 teeth
double cadence = 70;
double gearRatio = 2.4;
double pi = 3.14159;
double grade = 1;
double measuredSpeed = 5;
uint16_t maximumResistance = 200; // 200 N
uint16_t difficulty = 100; // 100% -> 1:1

void test_trackresistance(void) {
  //Logger::defaultLogLevel = LOG_LEVEL_DEBUG;

  uint16_t resistanceGrade;
  double gearedGrade;
  int gearNumber = 1;
  for(double zwiftGear : zwiftGears) {
    resistanceGrade = Calculations::calculateFECTrackResistanceGrade(totalWeight, grade, measuredSpeed, cadence, zwiftGear, defaultGearRatio, difficulty);
    gearedGrade = (resistanceGrade - 0x4E20) / 100.0;
    log_i("Zwift gear %d (%f) - Requested grade: %f -> Geared grade: %f", gearNumber, zwiftGear, grade, gearedGrade);
    gearNumber++;
  }
  
  TEST_ASSERT(true);
}

void test_basicresistance(void) {
  //Logger::defaultLogLevel = LOG_LEVEL_DEBUG;

  uint8_t basicResistancePercentageValue;
  double gearedResistance;
  int gearNumber = 1;
  for(double zwiftGear : zwiftGears) {
    basicResistancePercentageValue = Calculations::calculateFECBasicResistancePercentageValue(totalWeight, grade, measuredSpeed, cadence, zwiftGear, defaultGearRatio, maximumResistance, difficulty);
    gearedResistance = basicResistancePercentageValue / 200.0 * maximumResistance;
    log_i("Zwift gear %d (%f) -> Resistance: %fN (%f%% of %dN)", gearNumber, zwiftGear, gearedResistance, basicResistancePercentageValue / 2.0, maximumResistance);
    gearNumber++;
  }
  
  TEST_ASSERT(true);
}

void test_targetpower(void) {
  //Logger::defaultLogLevel = LOG_LEVEL_DEBUG;

  uint16_t targetPowerValue;
  int gearNumber = 1;
  for(double zwiftGear : zwiftGears) {
    targetPowerValue = Calculations::calculateFECTargetPowerValue(totalWeight, grade, measuredSpeed, cadence, zwiftGear, defaultGearRatio, difficulty);
    log_i("Zwift gear %d (%f) -> Target power value: %d (%dW)", gearNumber, zwiftGear, targetPowerValue, targetPowerValue / 4);
    gearNumber++;
  }
  
  TEST_ASSERT(true);
}

int main(int argc, char **argv) {
  Logger::defaultLogLevel = LOG_LEVEL_INFO;
  UNITY_BEGIN();
  //RUN_TEST(test_basicresistance);
  //RUN_TEST(test_trackresistance);
  RUN_TEST(test_targetpower);
  UNITY_END();
  return 0;
}