// PoemLibrary — the poems on the microSD card. Strictly read-only.
//
// Card requirements:
//   - FAT32. The ESP32 Arduino core is built without exFAT support, and
//     64 GB cards ship as exFAT, so a factory-fresh card will not mount.
//   - A /poems folder holding 001.bin ... NNN.bin, 4736 bytes each.
//
// Both public methods mount the card, do their work, and release it again
// before returning. That is what guarantees the card is never active while
// the panel is being driven on the shared SPI bus (HARDWARE.md §5).
//
// Both return nullptr on success or a short message on failure, sized to fit
// one line of the error screen (a later step).

#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "PoemBitmap.h"

class PoemLibrary {
public:
  PoemLibrary(uint8_t csPin, SPIClass &spi);

  // Counts poems/NNN.bin and checks the numbering runs 001..count with no
  // gaps, because the day index maps straight onto a file number.
  const char *count(int &count);

  // index is 0-based (it is what view_index holds); files are 1-based.
  const char *load(int index, PoemBitmap &bitmap);

private:
  const char *mount();
  const char *countMounted(int &count);
  const char *loadMounted(int index, PoemBitmap &bitmap);
  static bool isPoemName(const char *name);

  const uint8_t csPin_;
  SPIClass &spi_;
};
