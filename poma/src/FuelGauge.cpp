#include "FuelGauge.h"

namespace {
// We read two registers directly; that is all we need from the chip, so there
// is no library. Register layout is from the MAX17048 datasheet.
const uint8_t REG_VCELL = 0x02;  // 16 bits, 78.125 uV per count
const uint8_t REG_SOC = 0x04;    // 16 bits, 1/256 % per count (high byte = whole percent)
}  // namespace

FuelGauge::FuelGauge(TwoWire &wire, uint8_t address)
  : wire_(wire), address_(address) {}

BatteryReading FuelGauge::read() {
  BatteryReading reading = { false, 0, 0 };
  uint16_t vcell, soc;
  if (readRegister(REG_VCELL, vcell) && readRegister(REG_SOC, soc)) {
    reading.known = true;
    reading.mv = vcell * 5UL / 64;         // 78.125 uV per count is exactly 5/64 mV
    reading.percent = min(soc >> 8, 100);  // the gauge can report slightly over 100
  }
  return reading;
}

bool FuelGauge::readRegister(uint8_t reg, uint16_t &value) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  if (wire_.endTransmission(false) != 0) return false;  // false = keep the bus
  if (wire_.requestFrom(address_, (size_t)2) != 2) return false;
  uint8_t high = wire_.read();  // two statements: the gauge sends the high
  uint8_t low = wire_.read();   // byte first, and the order must be fixed
  value = (uint16_t)high << 8 | low;
  return true;
}
