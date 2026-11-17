#include "Board.h"

namespace board {

void deselectSpiDevices() {
  const uint8_t pins[] = { EPD_CS, SD_CS, WING_SRAM_CS };
  for (uint8_t pin : pins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
}

}  // namespace board
