/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <climits>
#include <cstring>

#include <unity.h>

#include "BLE_Definitions.h"
#include "CustomCharacteristicProtocol.h"
#include "DirConUUIDCodec.h"
#include "Zwift_Protocol_Messages.h"
#include "sensors/CscSensorData.h"
#include "sensors/FitnessMachineIndoorBikeData.h"
#include "sensors/HeartRateData.h"
#include "ByteUtils.h"
#include "test.h"

namespace {

void assertSigned16RoundTrip(int16_t value) {
  uint8_t bytes[2] = {0, 0};
  put_le16s(bytes, value);
  TEST_ASSERT_EQUAL_INT16(value, get_le16s(bytes));
}

void assertSigned32RoundTrip(int32_t value) {
  uint8_t bytes[4] = {0, 0, 0, 0};
  put_le32s(bytes, value);
  TEST_ASSERT_EQUAL_INT32(value, get_le32s(bytes));
}

void assertUnsigned16RoundTrip(uint16_t value) {
  uint8_t bytes[2] = {0, 0};
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

  for (uint8_t id = BLE_firmwareUpdateURL; id <= BLE_allSettings; ++id) {
    const CustomCharacteristicValueFormat format = customCharacteristicValueFormat(id);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(CustomUnknown, format, "custom characteristic is missing a wire format");
    ++formatCounts[format];

    switch (format) {
      case CustomBoolean: {
        const uint8_t positive = 1;
        TEST_ASSERT_EQUAL_UINT8(1, positive);
        break;
      }
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
      case CustomBooleanWriteStringRead: {
        const uint8_t enabled = 1;
        const char logMessage[] = "log payload";
        TEST_ASSERT_EQUAL_UINT8(1, enabled);
        TEST_ASSERT_EQUAL_STRING("log payload", logMessage);
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
  TEST_ASSERT_EQUAL_UINT(1, formatCounts[CustomBooleanWriteStringRead]);
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
  TEST_ASSERT_EQUAL_UINT8(45, static_cast<uint8_t>(45));
  TEST_ASSERT_EQUAL_UINT8(90, static_cast<uint8_t>(90));

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
