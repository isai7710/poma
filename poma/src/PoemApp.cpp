#include "PoemApp.h"

#include <SPI.h>
#include <Wire.h>

#include "Board.h"

namespace {
// Session 1 only: "today" is hardcoded. The RTC supplies it in Session 2.
// It is 5 rather than 0 so there are earlier poems to browse back to.
const int DAY_INDEX = 5;

// HARDWARE.md §8: below this, do not refresh. A brownout halfway through the
// waveform leaves a corrupted image; skipping leaves the last good poem.
const int BATTERY_MIN_MV = 3400;

// How often the buttons are sampled. This period is the debounce: see Buttons.
const uint32_t BUTTON_SAMPLE_MS = 20;
}  // namespace

// The wiring: which pin and which bus each part is given.
PoemApp::PoemApp()
  : gauge_(Wire, board::FUEL_GAUGE_ADDR),
    light_(board::NEOPIXEL),
    library_(board::SD_CS, SPI),
    display_(board::EPD_DC, board::EPD_RESET, board::EPD_CS, board::EPD_SRAM_CS,
             board::EPD_BUSY, SPI),
    buttons_(board::BTN_A, board::BTN_B, board::BTN_C) {}

void PoemApp::begin() {
  board::deselectSpiDevices();
  buttons_.begin();
  // Dark until we know begin() succeeded. Written explicitly because a
  // NeoPixel can keep its last colour across a reset.
  light_.set(false);
  Wire.begin();  // for the fuel gauge

  Serial.begin(115200);
  // Development only: give the USB serial monitor up to 4 s to attach.
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 4000;) delay(10);
  Serial.println("\n--- poma step 4: battery and busy light ---");

  BatteryReading battery = gauge_.read();
  if (battery.known) {
    Serial.printf("Battery: %d mV, %d %%\n", battery.mv, battery.percent);
  } else {
    Serial.println("Battery: gauge did not answer");
  }

  // How many poems are there? Needed once, to turn the day into an index.
  int count = 0;
  const char *err = library_.count(count);
  if (err) {
    // Leaving the panel alone keeps the last good poem on it (§8).
    Serial.printf("ERROR: %s. Panel left untouched.\n", err);
    return;
  }

  state_.begin(DAY_INDEX % count);
  Serial.printf("%d poems. Today is poem %d. Restored view: poem %d.\n", count,
                state_.dailyIndex() + 1, state_.viewIndex() + 1);

  display_.begin();

  Serial.printf("First refresh in %lu s (boot counts as a refresh).\n",
                (unsigned long)(display_.cooldownRemainingMs() + 999) / 1000);
  ready_ = true;
}

void PoemApp::update() {
  delay(BUTTON_SAMPLE_MS);
  if (!ready_) return;

  // Lit for as long as a press would be dropped.
  light_.set(!display_.canRefresh());

  Button button = buttons_.poll();
  if (button != Button::None) onButton(button);

  // The single place a refresh is started. PoemDisplay enforces the cooldown
  // again on its own; checking here just avoids touching the card for a
  // refresh that would be refused.
  if (needsRefresh_ && display_.canRefresh()) {
    needsRefresh_ = false;  // one attempt per request, even if it fails
    uint32_t t0 = millis();
    const char *err = showPoem(state_.viewIndex());
    if (err) {
      Serial.printf("ERROR: %s. Panel left untouched.\n", err);
    } else {
      Serial.printf("Showing poem %d (%lu ms). Next refresh allowed in 180 s.\n",
                    state_.viewIndex() + 1, millis() - t0);
    }
  }
}

// What each button means (HARDWARE.md §7). This is the only place that says.
void PoemApp::onButton(Button button) {
  int target = state_.viewIndex();
  const char *name = "?";
  switch (button) {
    case Button::A:
      name = "A (next)";
      target = state_.nextIndex();
      break;
    case Button::B:
      name = "B (today)";
      target = state_.dailyIndex();
      break;
    case Button::C:
      name = "C (previous)";
      target = state_.previousIndex();
      break;
    case Button::None:
      return;
  }

  if (target == state_.viewIndex()) {
    // Already at the floor, the ceiling, or today. No change, so no refresh.
    Serial.printf("Button %s: already there, nothing to do\n", name);
  } else if (uint32_t ms = display_.cooldownRemainingMs()) {
    // Dropped, not queued (HARDWARE.md §7): acknowledging it on the panel
    // would itself cost a refresh.
    Serial.printf("Button %s: ignored, cooldown has %lu s left\n", name,
                  (unsigned long)(ms + 999) / 1000);
  } else {
    Serial.printf("Button %s: poem %d -> %d\n", name, state_.viewIndex() + 1, target + 1);
    state_.moveTo(target);
    needsRefresh_ = true;
  }
}

const char *PoemApp::showPoem(int index) {
  BatteryReading battery = gauge_.read();
  if (battery.known) {
    Serial.printf("Battery: %d mV, %d %%\n", battery.mv, battery.percent);
    if (battery.mv < BATTERY_MIN_MV) return "Battery too low to refresh";
  } else {
    // Deliberately not an error: the gauge may not answer with no battery
    // plugged in, and then we are on USB power, where a brownout is not the
    // risk. The refresh goes ahead without a glyph.
    Serial.println("Battery: gauge did not answer, refreshing without a glyph");
  }

  const char *err = library_.load(index, bitmap_);
  if (err) return err;

  display_.drawPoem(bitmap_);
  display_.drawBatteryGlyph(battery);

  // The light comes on now, not after: refresh() blocks for 2.5 s, and this
  // is the only sign for that long that the press was accepted.
  if (display_.canRefresh()) light_.set(true);
  if (!display_.refresh()) return "Refresh refused";  // update() checks the cooldown first
  return nullptr;
}
