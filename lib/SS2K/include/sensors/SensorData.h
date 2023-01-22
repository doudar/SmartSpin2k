/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <climits>
#include <cmath>
#include <cstdint>
#include <string>

class SensorData {
 public:
  /**
   * @brief Constructor
   */
  explicit SensorData(std::string id) : id(id) {}

  /**
   * @brief Get the Id.
   * @return The unique identifier of the sensor.
   */
  std::string getId();

  /**
   * @brief Does this sensor have Heartrate data?
   * @return True if there is Heartrate data present.
   */
  virtual bool hasHeartRate() = 0;

  /**
   * @brief Does this sensor have Cadence data?
   * @return True if there is Cadence data present.
   */
  virtual bool hasCadence() = 0;

  /**
   * @brief Does this sensor have Power data?
   * @return True if there is Power data present.
   */
  virtual bool hasPower() = 0;

  /**
   * @brief Does this sensor have Speed data?
   * @return True if there is Speed data present.
   */
  virtual bool hasSpeed() = 0;

  /**
   * @brief Does this sensor have Resistance data?
   * @return True if there is Resistance data present.
   */
  virtual bool hasResistance() = 0;

  /**
   * @brief Get the Heartrate data.
   * @details hasHeartRateData must be called first to check for the availability of data.
   * @return The Heartrate data or INT_MIN if the data is not present.
   */
  virtual int getHeartRate() = 0;

  /**
   * @brief Get the Cadence data.
   * @details hasCadenceData must be called first to check for the availability of data.
   * @return The Cadence data or NAN if the data is not present.
   */
  virtual float getCadence() = 0;

  /**
   * @brief Get the Power data.
   * @details hasPowerData must be called first to check for the availability of data.
   * @return The Power data or INT_MIN if the data is not present.
   */
  virtual int getPower() = 0;

  /**
   * @brief Get the Speed data.
   * @details hasSpeedData must be called first to check for the availability of data.
   * @return The Speed data or NAN if the data is not present.
   */
  virtual float getSpeed() = 0;

  /**
   * @brief Get the resistance data.
   * @details hasResistanceData must be called first to check for the availability of data.
   * @return The Resistance data or INT_MIN if the data is not present.
   */
  virtual int getResistance() = 0;

  /**
   * @brief Decodes the sensor data and stores the parsed Heartrate, Cadence, Power and Speed.
   * @param [in] data The sensor data.
   * @param [in] length The length of the data in bytes.
   */
  virtual void decode(uint8_t *data, size_t length) = 0;

 private:
  std::string id;
};
