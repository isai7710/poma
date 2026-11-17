// BusyLight — the Feather's NeoPixel, used as a single on/off indicator.
//
// Lit = the refresh cooldown is running, so a button press would be dropped.
// It is lit, rather than dark, during the cooldown because the device is
// ready almost all day and the light should be dark for almost all of it.
//
// This class only switches the light. PoemApp decides when.

#pragma once

#include <Arduino.h>

class BusyLight {
public:
  // Brightness when lit, 1..255. 1 is the dimmest there is; raise it if that
  // is too dim to see through the enclosure.
  static constexpr uint8_t LEVEL = 1;

  explicit BusyLight(uint8_t pin);

  // Safe to call on every pass of loop(): it only writes when the state changes.
  void set(bool on);

private:
  enum class Shown : uint8_t { Unknown, Off, On };

  const uint8_t pin_;
  Shown shown_ = Shown::Unknown;  // Unknown, so that the first call always writes
};
