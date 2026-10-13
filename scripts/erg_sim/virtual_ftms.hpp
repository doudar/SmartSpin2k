/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Test-only FTMS GATT value/notification sink. The control decisions below are
// supplied unchanged from the selected firmware revision by virtual_ftms.py.
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

struct VirtualFtmsCharacteristic {
  std::vector<uint8_t> value;
  std::vector<std::vector<uint8_t>> notifications;
  void setValue(const uint8_t* data, size_t size) { value.assign(data, data + size); }
  void setValue(const std::vector<uint8_t>& data) { value = data; }
  std::vector<uint8_t> getValue() const { return value; }
  int getUUID() const { return 0; }
  void notify() { notifications.push_back(value); }
};
struct VirtualFtmsDirCon {
  template <typename Service, typename Characteristic>
  static void notifyCharacteristic(Service, Characteristic, const uint8_t*, size_t) {}
};
inline int virtualFtmsLogHex(const uint8_t*, int, char* out, int, int capacity) {
  if (capacity) out[0] = '\0';
  return 0;
}
class BLE_Fitness_Machine_Service {
 public:
  VirtualFtmsCharacteristic controlPoint, trainingStatus, status;
  VirtualFtmsCharacteristic* fitnessMachineControlPoint = &controlPoint;
  VirtualFtmsCharacteristic* fitnessMachineTrainingStatus = &trainingStatus;
  VirtualFtmsCharacteristic* fitnessMachineStatusCharacteristic = &status;
  void processFTMSWrite();
  void spinDown(int) {} // Notification-only homing sink; not a control decision.
} fitnessMachineService;

// Only the simulator's well-formed ERG session packets are supported here.
// This guard is NOT production malformed-packet validation: the production
// target-power handler currently reads its payload without checking length.
inline std::vector<uint8_t> virtualFtmsWrite(const std::vector<uint8_t>& packet) {
  if (packet.empty()) throw std::invalid_argument("empty virtual FTMS packet");
  const uint8_t opcode = packet[0];
  const size_t expected = opcode == 0x05 ? 3 : opcode == 0x08 ? 2 : 1;
  if ((opcode != 0x00 && opcode != 0x05 && opcode != 0x07 && opcode != 0x08) || packet.size() != expected)
    throw std::invalid_argument("virtual FTMS supports request/start/stop/target-power packets only");
  spinBLEServer.writeCache.push(std::string(packet.begin(), packet.end()));
  fitnessMachineService.processFTMSWrite();
  return fitnessMachineService.controlPoint.getValue();
}
