// PoemBitmap — one pre-rendered poem, exactly as it sits in a .bin file.
//
// Format (HARDWARE.md §4.6): 1 bit per pixel, 37 bytes per row with no
// padding, 128 rows, top to bottom. The most significant bit is the leftmost
// pixel and 1 means black. tools/render_poems.py writes this format;
// PoemLibrary fills one of these from the card and PoemDisplay draws it.

#pragma once

#include <Arduino.h>

struct PoemBitmap {
  static constexpr int16_t WIDTH = 296;
  static constexpr int16_t HEIGHT = 128;
  static constexpr size_t BYTES = (WIDTH / 8) * HEIGHT;  // 37 * 128 = 4736

  uint8_t data[BYTES];
};
