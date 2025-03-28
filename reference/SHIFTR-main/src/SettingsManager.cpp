#include <SettingsManager.h>
#include <Utils.h>
#include <Config.h>

char SettingsManager::iotWebConfChainringTeethParameterValue[16];
char SettingsManager::iotWebConfSprocketTeethParameterValue[16];
char SettingsManager::iotWebConfVirtualShiftingModeParameterValue[24];
char SettingsManager::iotWebConfVirtualShiftingParameterValue[16];
char SettingsManager::iotWebConfTrainerDeviceParameterValue[128];
char SettingsManager::iotWebConfGradeSmoothingParameterValue[16];
char SettingsManager::iotWebConfDifficultyParameterValue[16];
char SettingsManager::iotWebConfFTMSParameterValue[16];

char SettingsManager::iotWebConfVirtualShiftingModeValues[][24] = { "basic_resistance", "target_power", "track_resistance" };
char SettingsManager::iotWebConfVirtualShiftingModeNames[][24] = { "Basic Resistance", "Target Power", "Track Resistance" };

IotWebConfParameterGroup SettingsManager::iotWebConfSettingsGroup = IotWebConfParameterGroup("settings", "Device settings");
IotWebConfNumberParameter SettingsManager::iotWebConfChainringTeethParameter("Chainring teeth", "chainring_teeth", SettingsManager::iotWebConfChainringTeethParameterValue, sizeof(iotWebConfChainringTeethParameterValue), "34", "1..100", "min='1' max='100' step='1'");
IotWebConfNumberParameter SettingsManager::iotWebConfSprocketTeethParameter("Sprocket teeth", "sprocket_teeth", SettingsManager::iotWebConfSprocketTeethParameterValue, sizeof(iotWebConfSprocketTeethParameterValue), "14", "1..100", "min='1' max='100' step='1'");
IotWebConfSelectParameter SettingsManager::iotWebConfVirtualShiftingModeParameter = IotWebConfSelectParameter("Virtual Shifting Mode", "virtual_shifting_mode", SettingsManager::iotWebConfVirtualShiftingModeParameterValue, sizeof(iotWebConfVirtualShiftingModeParameterValue), (char*)iotWebConfVirtualShiftingModeValues, (char*)iotWebConfVirtualShiftingModeNames, sizeof(iotWebConfVirtualShiftingModeValues) / sizeof(iotWebConfVirtualShiftingModeParameterValue), sizeof(iotWebConfVirtualShiftingModeParameterValue), "basic_resistance");
IotWebConfCheckboxParameter SettingsManager::iotWebConfVirtualShiftingParameter = IotWebConfCheckboxParameter("Virtual shifting", "virtual_shifting", SettingsManager::iotWebConfVirtualShiftingParameterValue, sizeof(iotWebConfVirtualShiftingParameterValue), true);
IotWebConfTextParameter SettingsManager::iotWebConfTrainerDeviceParameter = IotWebConfTextParameter("Trainer device", "trainer_device", iotWebConfTrainerDeviceParameterValue, sizeof(iotWebConfTrainerDeviceParameterValue), "");
IotWebConfCheckboxParameter SettingsManager::iotWebConfGradeSmoothingParameter = IotWebConfCheckboxParameter("Grade smoothing", "grade_smoothing", SettingsManager::iotWebConfGradeSmoothingParameterValue, sizeof(iotWebConfGradeSmoothingParameterValue), true);
IotWebConfNumberParameter SettingsManager::iotWebConfDifficultyParameter("Difficulty", "difficulty", SettingsManager::iotWebConfDifficultyParameterValue, sizeof(iotWebConfDifficultyParameterValue), "100", "1..200", "min='1' max='200' step='1'");
IotWebConfCheckboxParameter SettingsManager::iotWebConfFTMSParameter = IotWebConfCheckboxParameter("FTMS Emulation", "ftms_emulation", SettingsManager::iotWebConfFTMSParameterValue, sizeof(iotWebConfFTMSParameterValue), false);

IotWebConf* SettingsManager::iotWebConf;

void SettingsManager::initialize(IotWebConf* iotWebConf) {
  SettingsManager::iotWebConf = iotWebConf;
  iotWebConfSettingsGroup.addItem(&iotWebConfTrainerDeviceParameter);
  iotWebConfSettingsGroup.addItem(&iotWebConfVirtualShiftingParameter);
  iotWebConfSettingsGroup.addItem(&iotWebConfChainringTeethParameter);
  iotWebConfSettingsGroup.addItem(&iotWebConfSprocketTeethParameter);
  iotWebConfSettingsGroup.addItem(&iotWebConfVirtualShiftingModeParameter);
  iotWebConfSettingsGroup.addItem(&iotWebConfGradeSmoothingParameter);
  iotWebConfSettingsGroup.addItem(&iotWebConfDifficultyParameter);
  iotWebConfSettingsGroup.addItem(&iotWebConfFTMSParameter);
}

VirtualShiftingMode SettingsManager::getVirtualShiftingMode() {
  if (strncmp(iotWebConfVirtualShiftingModeParameterValue, iotWebConfVirtualShiftingModeValues[1], sizeof(iotWebConfVirtualShiftingModeParameterValue)) == 0) {
    return VirtualShiftingMode::TARGET_POWER;
  }
  if (strncmp(iotWebConfVirtualShiftingModeParameterValue, iotWebConfVirtualShiftingModeValues[2], sizeof(iotWebConfVirtualShiftingModeParameterValue)) == 0) {
    return VirtualShiftingMode::TRACK_RESISTANCE;
  }
  return VirtualShiftingMode::BASIC_RESISTANCE;
}

void SettingsManager::setVirtualShiftingMode(VirtualShiftingMode virtualShiftingMode) {
  switch (virtualShiftingMode) {
    case VirtualShiftingMode::TARGET_POWER:
      strncpy(iotWebConfVirtualShiftingModeParameterValue, iotWebConfVirtualShiftingModeValues[1], sizeof(iotWebConfVirtualShiftingModeParameterValue));
      break;
    case VirtualShiftingMode::TRACK_RESISTANCE:
      strncpy(iotWebConfVirtualShiftingModeParameterValue, iotWebConfVirtualShiftingModeValues[2], sizeof(iotWebConfVirtualShiftingModeParameterValue));
      break;
    default:
      strncpy(iotWebConfVirtualShiftingModeParameterValue, iotWebConfVirtualShiftingModeValues[0], sizeof(iotWebConfVirtualShiftingModeParameterValue));
      break;
  }
}

std::map<size_t, std::string> SettingsManager::getVirtualShiftingModes() {
  std::map<size_t, std::string> returnMap;
  for (size_t index = 0; index < (sizeof(iotWebConfVirtualShiftingModeValues) / sizeof(iotWebConfVirtualShiftingModeParameterValue)); index++)
  {
    returnMap.emplace(index, iotWebConfVirtualShiftingModeNames[index]);
  }
  return returnMap;
}

uint16_t SettingsManager::getChainringTeeth() {
  return atoi(iotWebConfChainringTeethParameterValue);
}

void SettingsManager::setChainringTeeth(uint16_t chainringTeeth) {
  std::string chainringTeethString = std::to_string(chainringTeeth);
  strncpy(iotWebConfChainringTeethParameterValue, chainringTeethString.c_str(), sizeof(iotWebConfChainringTeethParameterValue));
}

uint16_t SettingsManager::getSprocketTeeth() {
  return atoi(iotWebConfSprocketTeethParameterValue);
}

void SettingsManager::setSprocketTeeth(uint16_t sprocketTeeth) {
  std::string sprocketTeethString = std::to_string(sprocketTeeth);
  strncpy(iotWebConfSprocketTeethParameterValue, sprocketTeethString.c_str(), sizeof(iotWebConfSprocketTeethParameterValue));
}

bool SettingsManager::isVirtualShiftingEnabled() {
  return (strncmp(iotWebConfVirtualShiftingParameterValue, "selected", sizeof(iotWebConfVirtualShiftingParameterValue)) == 0);
}

void SettingsManager::setVirtualShiftingEnabled(bool enabled) {
  String virtualShiftingEnabled = "";
  if (enabled) {
    virtualShiftingEnabled = "selected";
  }
  strncpy(iotWebConfVirtualShiftingParameterValue, virtualShiftingEnabled.c_str(), sizeof(iotWebConfVirtualShiftingParameterValue));
}

std::string SettingsManager::getTrainerDeviceName() {
  return std::string(iotWebConfTrainerDeviceParameterValue);
}

void SettingsManager::setTrainerDeviceName(std::string trainerDevice) {
  strncpy(iotWebConfTrainerDeviceParameterValue, trainerDevice.c_str(), sizeof(iotWebConfTrainerDeviceParameterValue));
}

IotWebConfParameterGroup* SettingsManager::getIoTWebConfSettingsParameterGroup() {
  return &iotWebConfSettingsGroup;
}

std::string SettingsManager::getUsername() {
  return std::string(DEFAULT_USERNAME);
}

std::string SettingsManager::getAPPassword() {
  iotwebconf::Parameter* aPPasswordParameter = iotWebConf->getApPasswordParameter();
  std::string aPPassword = std::string(aPPasswordParameter->valueBuffer);
  if (aPPassword.length() == 0) {
    aPPassword = Utils::getHostName().c_str();
  }
  return aPPassword;
}

bool SettingsManager::isGradeSmoothingEnabled() {
  return (strncmp(iotWebConfGradeSmoothingParameterValue, "selected", sizeof(iotWebConfGradeSmoothingParameterValue)) == 0);
}

void SettingsManager::setGradeSmoothingEnabled(bool enabled) {
  String gradeSmoothingEnabled = "";
  if (enabled) {
    gradeSmoothingEnabled = "selected";
  }
  strncpy(iotWebConfGradeSmoothingParameterValue, gradeSmoothingEnabled.c_str(), sizeof(iotWebConfGradeSmoothingParameterValue));
}

uint16_t SettingsManager::getDifficulty() {
  uint16_t difficulty = atoi(iotWebConfDifficultyParameterValue);
  if (difficulty < 1) {
    difficulty = 100;
  }
  return difficulty;
}

void SettingsManager::setDifficulty(uint16_t difficulty) {
  std::string difficultyString = std::to_string(difficulty);
  strncpy(iotWebConfDifficultyParameterValue, difficultyString.c_str(), sizeof(iotWebConfDifficultyParameterValue));
}

bool SettingsManager::isFTMSEnabled() {
  return (strncmp(iotWebConfFTMSParameterValue, "selected", sizeof(iotWebConfFTMSParameterValue)) == 0);
}

void SettingsManager::setFTMSEnabled(bool enabled) {
  String fTMSEnabled = "";
  if (enabled) {
    fTMSEnabled = "selected";
  }
  strncpy(iotWebConfFTMSParameterValue, fTMSEnabled.c_str(), sizeof(iotWebConfFTMSParameterValue));
}
