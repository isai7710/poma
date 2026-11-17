#include "PoemDisplay.h"

namespace {
// 2 = upright in the enclosure with the ribbon cable on the RIGHT (HARDWARE.md §4).
const uint8_t PANEL_ROTATION = 2;

// The converter keeps a 16 x 8 px area clear for the battery glyph, top
// right. These two numbers must match BATTERY_GLYPH in tools/render_poems.py.
const int16_t GLYPH_X = 275;
const int16_t GLYPH_Y = 5;
}  // namespace

PoemDisplay::PoemDisplay(int8_t dcPin, int8_t resetPin, int8_t csPin,
                         int8_t sramCsPin, int8_t busyPin, SPIClass &spi)
  : panel_(dcPin, resetPin, csPin, sramCsPin, busyPin, &spi) {}

void PoemDisplay::begin() {
  panel_.begin(THINKINK_MONO);
  panel_.setRotation(PANEL_ROTATION);
}

void PoemDisplay::drawPoem(PoemBitmap &bitmap) {
  panel_.clearBuffer();
  // The bitmap is deliberately non-const: a uint8_t* selects GFX's RAM
  // overload of drawBitmap rather than the PROGMEM one (HARDWARE.md §4.6).
  panel_.drawBitmap(0, 0, bitmap.data, PoemBitmap::WIDTH, PoemBitmap::HEIGHT, EPD_BLACK);
}

void PoemDisplay::drawBatteryGlyph(const BatteryReading &battery) {
  if (!battery.known) return;
  panel_.drawRect(GLYPH_X, GLYPH_Y, 14, 8, EPD_BLACK);          // body
  panel_.fillRect(GLYPH_X + 14, GLYPH_Y + 2, 2, 4, EPD_BLACK);  // terminal
  int16_t fill = battery.percent / 10;                          // 0..10 px: one pixel per 10 %
  if (fill > 0) panel_.fillRect(GLYPH_X + 2, GLYPH_Y + 2, fill, 4, EPD_BLACK);
}

bool PoemDisplay::refresh() {
  if (!canRefresh()) return false;
  panel_.display();  // ~2.5 s: a fixed delay, BUSY is not wired (§4.4)
  cooldown_.restart();
  return true;
}
