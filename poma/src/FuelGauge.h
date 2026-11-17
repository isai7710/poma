// FuelGauge — reads the battery from the Feather's MAX17048 over I2C.
//
// It only reports. What to do about a low battery is PoemApp's decision.

#pragma once

#include <Arduino.h>
#include <Wire.h>

struct BatteryReading {
  bool known;   // false = the gauge did not answer
  int mv;       // cell voltage in millivolts
  int percent;  // the gauge's state-of-charge estimate, clamped to 0..100
};

class FuelGauge {
public:
  // The caller owns the bus and must have called wire.begin() before read().
  FuelGauge(TwoWire &wire, uint8_t address);

  BatteryReading read();

private:
  bool readRegister(uint8_t reg, uint16_t &value);

  TwoWire &wire_;
  const uint8_t address_;
};
