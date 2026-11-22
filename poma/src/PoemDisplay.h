// PoemDisplay — everything that puts pixels on the e-paper panel.
//
// The draw methods only touch the library's RAM framebuffer. Nothing reaches
// the panel until refresh(), and refresh() is the ONLY place in this firmware
// that calls the library's display(). It refuses, rather than waits, if the
// cooldown has not elapsed, so no caller can break the 180 s limit by mistake.
//
// The panel object is private, so no other class can reach display() at all.

#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_ThinkInk.h"

#include "FuelGauge.h"
#include "PoemBitmap.h"
#include "RefreshCooldown.h"

class PoemDisplay {
public:
  // Same argument order as the Adafruit panel classes.
  PoemDisplay(int8_t dcPin, int8_t resetPin, int8_t csPin, int8_t sramCsPin,
              int8_t busyPin, SPIClass &spi);

  // Talks to the controller but does not refresh.
  void begin();

  // Clears the framebuffer and draws the poem. Call this first.
  void drawPoem(PoemBitmap &bitmap);

  // Draws over the poem, in the top-right area the converter keeps clear.
  // It is a snapshot: it shows the charge at the moment of this refresh and
  // does not change until the next one. An empty outline means under 10 %.
  // Draws nothing if the reading is unknown, rather than a guess.
  void drawBatteryGlyph(const BatteryReading &battery);

  // Clears the framebuffer and draws one centred line of text, in place of a
  // poem (HARDWARE.md §8). At 12 px per character the panel holds 24
  // characters; anything longer runs off the edge.
  void drawError(const char *message);

  // Pushes the framebuffer to the panel. Blocks for ~2.5 s. Returns false,
  // having done nothing, if the cooldown is still running.
  bool refresh();

  bool canRefresh() const {
    return cooldown_.remainingMs() == 0;
  }
  uint32_t cooldownRemainingMs() const {
    return cooldown_.remainingMs();
  }

private:
  // Panel class and mode are fixed by HARDWARE.md §4. Do not change them
  // without reading §4.1.
  ThinkInk_290_Grayscale4_EAAMFGN panel_;
  RefreshCooldown cooldown_;
};
