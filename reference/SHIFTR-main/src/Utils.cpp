#include <Arduino.h>
#include <Config.h>
#include <Utils.h>

uint64_t Utils::macAddress = ESP.getEfuseMac();

std::string Utils::getMacAddressString() {
  static char macAddressString[18];
  sprintf(macAddressString, "%02X-%02X-%02X-%02X-%02X-%02X", (uint8_t)(macAddress), (uint8_t)(macAddress >> 8), (uint8_t)(macAddress >> 16), (uint8_t)(macAddress >> 24), (uint8_t)(macAddress >> 32), (uint8_t)(macAddress >> 40));
  std::string returnString = macAddressString;
  return returnString;
}

std::string Utils::getSerialNumberString() {
  static char serialNumberString[16];
  sprintf(serialNumberString, "%i%i%i", (uint8_t)(macAddress >> 24), (uint8_t)(macAddress >> 32), (uint8_t)(macAddress >> 40));
  std::string returnString = serialNumberString;
  return returnString;
}

std::string Utils::getDeviceName() {
  static char deviceName[strlen(DEVICE_NAME_PREFIX) + 8];
  sprintf(deviceName, "%s %02X%02X%02X", DEVICE_NAME_PREFIX, (uint8_t)(macAddress >> 24), (uint8_t)(macAddress >> 32), (uint8_t)(macAddress >> 40));
  deviceName[strlen(DEVICE_NAME_PREFIX) + 7] = 0x00;
  std::string returnString = deviceName;
  return returnString;
}

std::string Utils::getHostName() {
  std::string returnString = Utils::getDeviceName();
  std::replace(returnString.begin(), returnString.end(), 0x20, 0x2D);
  return returnString;
}

std::string Utils::getFQDN() {
  std::string returnString = Utils::getHostName();
  returnString.append(".local");
  return returnString;
}

std::string Utils::getHexString(uint8_t* data, size_t length) {
  static char hexNumber[3];
  std::string hexString = "[";
  for (size_t index = 0; index < length; index++) {
    sprintf(hexNumber, "%02X", data[index]);
    hexString.append(hexNumber);
    if (index < (length - 1)) {
      hexString.append(" ");
    }
  }
  hexString.append("]");
  return hexString;
}

std::string Utils::getHexString(std::vector<uint8_t> data) {
  return Utils::getHexString(data.data(), data.size());
}

std::string Utils::getHexString(std::vector<uint8_t>* data) {
  return Utils::getHexString(data->data(), data->size());
}

std::vector<uint8_t> Utils::getVectorFromStruct(void* data, size_t length) {
  std::vector<uint8_t> returnVector;
  for (size_t index = 0; index < length; index++) {
    returnVector.push_back(((uint8_t*)data)[index]);
  }
  return returnVector;
}