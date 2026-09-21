/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <climits>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <unity.h>

#include "BLE_Definitions.h"
#include "CustomCharacteristicProtocol.h"
#include "Constants.h"
#include "DirConUUIDCodec.h"
#include "ScanResultProtocol.h"
#include "Zwift_Protocol_Messages.h"
#include "sensors/CscSensorData.h"
#include "sensors/FitnessMachineIndoorBikeData.h"
#include "sensors/HeartRateData.h"
#include "sensors/SensorDataFactory.h"
#include "ByteUtils.h"
#include "test.h"

namespace {

void assertSigned16RoundTrip(int16_t value) {
  uint8_t bytes[2];
  put_le16s(bytes, value);
  TEST_ASSERT_EQUAL_INT16(value, get_le16s(bytes));
}

void assertSigned32RoundTrip(int32_t value) {
  uint8_t bytes[4];
  put_le32s(bytes, value);
  TEST_ASSERT_EQUAL_INT32(value, get_le32s(bytes));
}

void assertUnsigned16RoundTrip(uint16_t value) {
  uint8_t bytes[2];
  put_le16(bytes, value);
  TEST_ASSERT_EQUAL_UINT16(value, get_le16(bytes));
}

size_t makeIndoorBikeData(int16_t resistance, int16_t power, uint8_t* bytes) {
  const uint16_t flags = FitnessMachineIndoorBikeDataFlags::InstantaneousCadencePresent |
                         FitnessMachineIndoorBikeDataFlags::ResistanceLevelPresent |
                         FitnessMachineIndoorBikeDataFlags::InstantaneousPowerPresent;
  put_le16(&bytes[0], flags);
  put_le16(&bytes[2], 2534);  // 25.34 km/h
  put_le16(&bytes[4], 176);   // 88 RPM in 0.5 RPM units
  put_le16s(&bytes[6], resistance);
  put_le16s(&bytes[8], power);
  return 10;
}

}  // namespace

void TestBleWireRoundTrip::test_factory_preserves_cached_parser_state(void) {
  std::shared_ptr<SensorData> retained;
  {
    SensorDataFactory factory;
    uint8_t powerPacket[8] = {};
    put_le16(powerPacket, 0x20);  // Crank revolution data present.
    put_le16s(powerPacket + 2, 200);
    put_le16(powerPacket + 4, 10);
    put_le16(powerPacket + 6, 1024);
    retained = factory.getSensorData(CYCLINGPOWERMEASUREMENT_UUID, "primary", powerPacket, sizeof(powerPacket));
    TEST_ASSERT_EQUAL_FLOAT(0, retained->getCadence());

    // Grow the cache while retaining the original parser and its previous sample.
    for (int i = 0; i < 32; ++i) {
      auto other = factory.getSensorData(CYCLINGPOWERMEASUREMENT_UUID, "other-" + std::to_string(i), powerPacket, sizeof(powerPacket));
      TEST_ASSERT_TRUE(other.get() != retained.get());
      TEST_ASSERT_EQUAL_FLOAT(0, other->getCadence());
    }

    put_le16s(powerPacket + 2, 225);
    put_le16(powerPacket + 4, 11);
    put_le16(powerPacket + 6, 2048);
    auto cached = factory.getSensorData(CYCLINGPOWERMEASUREMENT_UUID, "primary", powerPacket, sizeof(powerPacket));
    TEST_ASSERT_TRUE(cached.get() == retained.get());
    TEST_ASSERT_EQUAL_FLOAT(60, cached->getCadence());

    uint8_t heartPacket[] = {0, 123};
    auto heart = factory.getSensorData(HEARTCHARACTERISTIC_UUID, "primary", heartPacket, sizeof(heartPacket));
    TEST_ASSERT_TRUE(heart.get() != retained.get());
    TEST_ASSERT_EQUAL_INT(123, heart->getHeartRate());
  }
  TEST_ASSERT_EQUAL_INT(225, retained->getPower());
  TEST_ASSERT_EQUAL_FLOAT(60, retained->getCadence());
}

void TestBleWireRoundTrip::test_nimble_uuid_comparison_and_rendering(void) {
  const NimBLEUUID uuid16(static_cast<uint16_t>(0x180D));
  const NimBLEUUID different16(static_cast<uint16_t>(0x180F));
  TEST_ASSERT_TRUE(uuid16 == NimBLEUUID(static_cast<uint16_t>(0x180D)));
  TEST_ASSERT_TRUE(uuid16 != different16);
  TEST_ASSERT_EQUAL_STRING("0x180d", uuid16.toString().c_str());

  const NimBLEUUID uuid32(static_cast<uint32_t>(0x12345678));
  const NimBLEUUID different32(static_cast<uint32_t>(0x12345679));
  TEST_ASSERT_TRUE(uuid32 == NimBLEUUID(static_cast<uint32_t>(0x12345678)));
  TEST_ASSERT_TRUE(uuid32 != different32);
  TEST_ASSERT_EQUAL_STRING("0x12345678", uuid32.toString().c_str());

  const NimBLEUUID uuid128("12345678-9abc-def0-1234-56789abcdef0");
  const NimBLEUUID different128("12345678-9abc-def0-1234-56789abcdef1");
  TEST_ASSERT_TRUE(uuid128 == NimBLEUUID("12345678-9abc-def0-1234-56789abcdef0"));
  TEST_ASSERT_TRUE(uuid128 != different128);
  TEST_ASSERT_EQUAL_STRING("12345678-9abc-def0-1234-56789abcdef0", uuid128.toString().c_str());

  const NimBLEUUID assigned16(static_cast<uint16_t>(0x180D));
  const NimBLEUUID base128("0000180d-0000-1000-8000-00805f9b34fb");
  TEST_ASSERT_TRUE(assigned16 == base128);
  TEST_ASSERT_TRUE(base128 == assigned16);
}

void TestBleWireRoundTrip::test_dircon_uuid_round_trip(void) {
  const uint8_t expectedFtmsBytes[] = {0x00, 0x00, 0x18, 0x26, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb};
  const uint8_t expectedHeartRateBytes[] = {0x00, 0x00, 0x18, 0x0d, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb};
  const uint8_t ftmsValueBytes[] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x26, 0x18, 0x00, 0x00};
  const uint8_t heartRateValueBytes[] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x0d, 0x18, 0x00, 0x00};

  std::vector<uint8_t> encoded;
  DirConUUIDCodec::appendValueBytes(ftmsValueBytes, encoded);
  TEST_ASSERT_EQUAL_UINT(sizeof(expectedFtmsBytes), encoded.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedFtmsBytes, encoded.data(), sizeof(expectedFtmsBytes));

  encoded.clear();
  DirConUUIDCodec::appendValueBytes(heartRateValueBytes, encoded);
  TEST_ASSERT_EQUAL_UINT(sizeof(expectedHeartRateBytes), encoded.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedHeartRateBytes, encoded.data(), sizeof(expectedHeartRateBytes));

  const uint8_t expectedCustomBytes[] = {0x77, 0x77, 0x62, 0x77, 0x78, 0x77, 0x77, 0x74, 0x44, 0x66, 0x89, 0x66, 0x65, 0x50, 0x00, 0x00};
  const uint8_t customValueBytes[] = {0x00, 0x00, 0x50, 0x65, 0x66, 0x89, 0x66, 0x44, 0x74, 0x77, 0x77, 0x78, 0x77, 0x62, 0x77, 0x77};
  encoded.clear();
  DirConUUIDCodec::appendValueBytes(customValueBytes, encoded);
  TEST_ASSERT_EQUAL_UINT(sizeof(expectedCustomBytes), encoded.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedCustomBytes, encoded.data(), sizeof(expectedCustomBytes));
}

void TestBleWireRoundTrip::test_all_custom_characteristic_formats(void) {
  unsigned formatCounts[CustomUnknown + 1] = {0};

  for (uint8_t id = BLE_firmwareUpdateURL; id <= BLE_gearRatios; ++id) {
    const CustomCharacteristicValueFormat format = customCharacteristicValueFormat(id);
    // 0x33 was the retired experimental rider-weight field. Keep the wire ID
    // reserved so a future field cannot accidentally reinterpret old writes.
    if (id == 0x33) {
      TEST_ASSERT_EQUAL(CustomUnknown, format);
      continue;
    }
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CustomUnknown, format, "custom characteristic is missing a wire format");
    ++formatCounts[format];

    switch (format) {
      case CustomBoolean:
        break;
      case CustomUnsigned16:
        assertUnsigned16RoundTrip(0xBEEF);
        break;
      case CustomSigned16:
        assertSigned16RoundTrip(12345);
        assertSigned16RoundTrip(-12345);
        break;
      case CustomSigned32:
        assertSigned32RoundTrip(123456789);
        assertSigned32RoundTrip(-123456789);
        break;
      case CustomString: {
        const char value[] = "round trip";
        uint8_t response[2 + sizeof(value)] = {cc_success, id};
        memcpy(&response[2], value, sizeof(value));
        TEST_ASSERT_EQUAL_STRING(value, reinterpret_cast<const char*>(&response[2]));
        break;
      }
      case CustomPowerTableRow: {
        uint8_t row[5] = {7, 0, 0, 0, 0};
        put_le16s(&row[1], 2345);
        put_le16s(&row[3], -2345);
        TEST_ASSERT_EQUAL_UINT8(7, row[0]);
        TEST_ASSERT_EQUAL_INT16(2345, get_le16s(&row[1]));
        TEST_ASSERT_EQUAL_INT16(-2345, get_le16s(&row[3]));
        break;
      }
      case CustomSettingsSnapshot: {
        uint8_t header[7] = {cc_success, id, 1, 0, 0, 0, 0};
        put_le16(&header[3], 257);
        put_le16(&header[5], 513);
        TEST_ASSERT_EQUAL_UINT16(257, get_le16(&header[3]));
        TEST_ASSERT_EQUAL_UINT16(513, get_le16(&header[5]));
        break;
      }
      case CustomScanResultStream: {
        const std::vector<uint8_t> body = ScanResultProtocol::makeDeviceBody("0x1818", "Power Meter");
        const std::vector<uint8_t> packet = ScanResultProtocol::makePacket(ScanResultProtocol::Event::Device, 0x1234, 0x5678, 0, 1, body.data(), body.size());
        TEST_ASSERT_EQUAL_UINT8(BLE_scanResults, packet[1]);
        TEST_ASSERT_EQUAL_UINT8(ScanResultProtocol::VERSION, packet[2]);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ScanResultProtocol::Event::Device), packet[3]);
        TEST_ASSERT_EQUAL_UINT16(0x1234, get_le16(&packet[4]));
        TEST_ASSERT_EQUAL_UINT16(0x5678, get_le16(&packet[6]));
        TEST_ASSERT_EQUAL_UINT8(6, packet[ScanResultProtocol::HEADER_LENGTH]);
        break;
      }
      case CustomBooleanWriteStringRead:
        break;
      case CustomGearRatios: {
        uint8_t value[3] = {0, 0, 0};
        put_le16(value + 1, 4545);
        TEST_ASSERT_EQUAL_UINT16(4545, get_le16(value + 1));
        break;
      }
      case CustomAction:
        // Action characteristics carry only operation and ID, so there is no value to round-trip.
        break;
      case CustomUnknown:
        TEST_FAIL_MESSAGE("unexpected custom characteristic format");
        break;
    }
  }

  TEST_ASSERT_EQUAL_UINT(6, formatCounts[CustomAction]);
  TEST_ASSERT_EQUAL_UINT(11, formatCounts[CustomBoolean]);
  TEST_ASSERT_EQUAL_UINT(14, formatCounts[CustomUnsigned16]);
  TEST_ASSERT_EQUAL_UINT(3, formatCounts[CustomSigned16]);
  TEST_ASSERT_EQUAL_UINT(3, formatCounts[CustomSigned32]);
  TEST_ASSERT_EQUAL_UINT(9, formatCounts[CustomString]);
  TEST_ASSERT_EQUAL_UINT(1, formatCounts[CustomPowerTableRow]);
  TEST_ASSERT_EQUAL_UINT(1, formatCounts[CustomSettingsSnapshot]);
  TEST_ASSERT_EQUAL_UINT(1, formatCounts[CustomScanResultStream]);
  TEST_ASSERT_EQUAL_UINT(1, formatCounts[CustomBooleanWriteStringRead]);
  TEST_ASSERT_EQUAL_UINT(1, formatCounts[CustomGearRatios]);
}

void TestBleWireRoundTrip::test_ftms_round_trip(void) {
  // Signed FTMS control-point values: target inclination, target resistance,
  // target power, simulation wind speed, and simulation grade.
  assertSigned16RoundTrip(1250);
  assertSigned16RoundTrip(-1250);
  assertSigned16RoundTrip(87);
  assertSigned16RoundTrip(-87);
  assertSigned16RoundTrip(350);
  assertSigned16RoundTrip(-350);
  assertSigned16RoundTrip(420);
  assertSigned16RoundTrip(-420);
  assertSigned16RoundTrip(1234);
  assertSigned16RoundTrip(-1234);

  // Unsigned FTMS control-point values: target cadence and simulation coefficients.
  assertUnsigned16RoundTrip(190);

  uint8_t payload[10];
  FitnessMachineIndoorBikeData positive;
  positive.decode(payload, makeIndoorBikeData(42, 321, payload));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.3f, positive.getSpeed());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 88.0f, positive.getCadence());
  TEST_ASSERT_EQUAL_INT(42, positive.getResistance());
  TEST_ASSERT_EQUAL_INT(321, positive.getPower());

  FitnessMachineIndoorBikeData negative;
  negative.decode(payload, makeIndoorBikeData(-42, -321, payload));
  TEST_ASSERT_EQUAL_INT(-42, negative.getResistance());
  TEST_ASSERT_EQUAL_INT(-321, negative.getPower());
}

void TestBleWireRoundTrip::test_csc_round_trip(void) {
  CscMeasurement measurement;
  measurement.flags.wheelRevolutionDataPresent = 1;
  measurement.flags.crankRevolutionDataPresent = 1;
  measurement.cumulativeWheelRevolutions       = 0x89ABCDEFU;
  measurement.lastWheelEventTime               = 0x7654U;
  measurement.cumulativeCrankRevolutions       = 0x3210U;
  measurement.lastCrankEventTime               = 0xFEDCU;

  CscMeasurement::Buffer bytes;
  const size_t length = measurement.toByteArray(bytes);
  TEST_ASSERT_EQUAL_UINT(11, length);
  TEST_ASSERT_EQUAL_UINT8(0x03, bytes[0]);
  TEST_ASSERT_EQUAL_UINT32(0x89ABCDEFU, get_le32(&bytes[1]));
  TEST_ASSERT_EQUAL_UINT16(0x7654U, get_le16(&bytes[5]));
  TEST_ASSERT_EQUAL_UINT16(0x3210U, get_le16(&bytes[7]));
  TEST_ASSERT_EQUAL_UINT16(0xFEDCU, get_le16(&bytes[9]));

  CscSensorData decoded;
  measurement.cumulativeWheelRevolutions = 100;
  measurement.lastWheelEventTime         = 1000;
  measurement.cumulativeCrankRevolutions = 50;
  measurement.lastCrankEventTime         = 1000;
  decoded.decode(bytes.data(), measurement.toByteArray(bytes));

  measurement.cumulativeWheelRevolutions = 101;
  measurement.lastWheelEventTime         = 2024;
  measurement.cumulativeCrankRevolutions = 52;
  measurement.lastCrankEventTime         = 2024;
  decoded.decode(bytes.data(), measurement.toByteArray(bytes));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.542f, decoded.getSpeed());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, decoded.getCadence());
}

void TestBleWireRoundTrip::test_heart_rate_round_trip(void) {
  uint8_t payload[] = {0x00, 187};  // uint8 heart-rate format, matching the server characteristic
  HeartRateData decoded;
  decoded.decode(payload, sizeof(payload));
  TEST_ASSERT_EQUAL_INT(187, decoded.getHeartRate());
}

void TestBleWireRoundTrip::test_zwift_round_trip(void) {
  const uint64_t unsignedValues[] = {0, 1, 127, 128, 16384, 0xFFFFFFFFULL, UINT64_MAX};
  for (size_t i = 0; i < sizeof(unsignedValues) / sizeof(unsignedValues[0]); ++i) {
    uint8_t bytes[10] = {0};
    const size_t encodedLength = ZwiftProtocol::encodeUleb128(unsignedValues[i], bytes);
    uint64_t decoded           = 0;
    TEST_ASSERT_EQUAL_UINT(encodedLength, ZwiftProtocol::uleb128Length(unsignedValues[i]));
    TEST_ASSERT_EQUAL_UINT(encodedLength, ZwiftProtocol::decodeUleb128(bytes, encodedLength, &decoded));
    TEST_ASSERT_EQUAL_UINT64(unsignedValues[i], decoded);
  }

  const int64_t signedValues[] = {0, 1, -1, 123456, -123456, INT32_MAX, INT32_MIN};
  for (size_t i = 0; i < sizeof(signedValues) / sizeof(signedValues[0]); ++i) {
    uint8_t bytes[10]         = {0};
    const uint64_t wireValue  = ZwiftProtocol::encodeZigZag64(signedValues[i]);
    const size_t encodedLength = ZwiftProtocol::encodeUleb128(wireValue, bytes);
    uint64_t decodedWire      = 0;
    TEST_ASSERT_EQUAL_UINT(encodedLength, ZwiftProtocol::decodeUleb128(bytes, encodedLength, &decodedWire));
    TEST_ASSERT_EQUAL_INT64(signedValues[i], ZwiftProtocol::decodeZigZag64(decodedWire));
  }
}
