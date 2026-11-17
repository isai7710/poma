// Board — everything that is fixed by the PCBs.
//
// HARDWARE.md §3 is the authority for these numbers. Nothing else in the
// firmware names a GPIO: PoemApp reads them from here and hands them to the
// class that drives each part.

#pragma once

#include <Arduino.h>

namespace board {

// --- eInk FeatherWing -------------------------------------------------------
constexpr int8_t EPD_CS = 9;
constexpr int8_t EPD_DC = 10;
constexpr int8_t EPD_RESET = -1;    // tied to the Feather's reset line
constexpr int8_t EPD_BUSY = -1;     // not routed on the wing
constexpr int8_t EPD_SRAM_CS = -1;  // what the library is told: do not use the SRAM (§5)
// The SRAM chip is still physically on the bus even though the library
// ignores it, so we hold its chip select high ourselves.
constexpr uint8_t WING_SRAM_CS = 6;
constexpr uint8_t SD_CS = 5;

// --- Buttons on the wing: active LOW, switch to GND -------------------------
constexpr uint8_t BTN_A = 11;
constexpr uint8_t BTN_B = 12;
constexpr uint8_t BTN_C = 13;

// --- On the Feather itself --------------------------------------------------
// PIN_NEOPIXEL comes with the ESP32 core. The core also switches the
// NeoPixel's power pin (GPIO 21) on before setup() runs.
constexpr uint8_t NEOPIXEL = PIN_NEOPIXEL;
constexpr uint8_t FUEL_GAUGE_ADDR = 0x36;  // MAX17048, found by a bus scan

// Drive every chip select on the shared SPI bus to "not selected".
// Must run before the first SPI transaction of any kind (HARDWARE.md §5).
void deselectSpiDevices();

}  // namespace board
