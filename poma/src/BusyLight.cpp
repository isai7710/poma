#include "BusyLight.h"

BusyLight::BusyLight(uint8_t pin)
  : pin_(pin) {}

void BusyLight::set(bool on) {
  Shown wanted = on ? Shown::On : Shown::Off;
  if (shown_ == wanted) return;
  rgbLedWrite(pin_, on ? LEVEL : 0, 0, 0);  // red only. rgbLedWrite() comes with the core.
  shown_ = wanted;
}
