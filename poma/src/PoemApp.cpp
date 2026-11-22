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

const char *const NVS_NAMESPACE = "poma";
}  // namespace

// The wiring: which pin, bus and storage each part is given.
PoemApp::PoemApp()
  : gauge_(Wire, board::FUEL_GAUGE_ADDR),
    light_(board::NEOPIXEL),
    library_(board::SD_CS, SPI),
    display_(board::EPD_DC, board::EPD_RESET, board::EPD_CS, board::EPD_SRAM_CS,
             board::EPD_BUSY, SPI),
    buttons_(board::BTN_A, board::BTN_B, board::BTN_C),
    state_(prefs_),
    errors_(prefs_) {}

void PoemApp::begin() {
  board::deselectSpiDevices();
  buttons_.begin();
  // Written explicitly because a NeoPixel can keep its last colour across a
  // reset. update() takes it over from here.
  light_.set(false);
  Wire.begin();  // for the fuel gauge

  Serial.begin(115200);
  // Development only: give the USB serial monitor up to 4 s to attach.
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 4000;) delay(10);
  Serial.println("\n--- poma step 5: error screen ---");

  BatteryReading battery = gauge_.read();
  if (battery.known) {
    Serial.printf("Battery: %d mV, %d %%\n", battery.mv, battery.percent);
  } else {
    Serial.println("Battery: gauge did not answer");
  }

  prefs_.begin(NVS_NAMESPACE);
  errors_.begin();
  if (errors_.showing()) Serial.println("The panel is showing an error screen from an earlier run.");

  // begin() talks to the controller but does not refresh. It runs even if
  // the card turns out to be bad, because the error screen needs the panel.
  display_.begin();

  // How many poems are there? Needed once, to turn the day into an index.
  int count = 0;
  startupError_ = library_.count(count);
  if (startupError_) {
    Serial.printf("ERROR: %s. Buttons are off until the next reset.\n", startupError_);
  } else {
    state_.begin(DAY_INDEX % count);
    Serial.printf("%d poems. Today is poem %d. Restored view: poem %d.\n", count,
                  state_.dailyIndex() + 1, state_.viewIndex() + 1);
  }

  Serial.printf("First refresh in %lu s (boot counts as a refresh).\n",
                (unsigned long)(display_.cooldownRemainingMs() + 999) / 1000);
}

void PoemApp::update() {
  delay(BUTTON_SAMPLE_MS);

  // Lit for as long as the cooldown is running.
  light_.set(!display_.canRefresh());

  if (!startupError_) {
    Button button = buttons_.poll();
    if (button != Button::None) onButton(button);
  }

  // The single place a refresh is started. PoemDisplay enforces the cooldown
  // again on its own; checking here just avoids touching the card for a
  // refresh that would be refused.
  if (needsRefresh_ && display_.canRefresh()) {
    needsRefresh_ = false;  // one attempt per request, even if it fails
    refreshPanel();
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

  // If an error screen is up, the panel is not showing the poem view_index
  // says it is, so a press is a retry even when the index does not change.
  bool alreadyShown = target == state_.viewIndex() && !errors_.showing();

  if (alreadyShown) {
    // Already at the floor, the ceiling, or today. No change, so no refresh.
    Serial.printf("Button %s: already there, nothing to do\n", name);
  } else if (uint32_t ms = display_.cooldownRemainingMs()) {
    // Dropped, not queued (HARDWARE.md §7): acknowledging it on the panel
    // would itself cost a refresh.
    Serial.printf("Button %s: ignored, cooldown has %lu s left\n", name,
                  (unsigned long)(ms + 999) / 1000);
  } else {
    Serial.printf("Button %s: poem %d -> %d\n", name, state_.viewIndex() + 1, target + 1);
    if (target != state_.viewIndex()) state_.moveTo(target);
    needsRefresh_ = true;
  }
}

void PoemApp::refreshPanel() {
  uint32_t t0 = millis();

  // The battery check comes first and covers the error screen too: an error
  // screen is a refresh like any other, and a brownout would corrupt it.
  BatteryReading battery = gauge_.read();
  if (battery.known) {
    Serial.printf("Battery: %d mV, %d %%\n", battery.mv, battery.percent);
    if (battery.mv < BATTERY_MIN_MV) {
      Serial.println("Battery too low to refresh. Panel left untouched.");
      return;
    }
  } else {
    // Deliberately not an error: the gauge may not answer with no battery
    // plugged in, and then we are on USB power, where a brownout is not the
    // risk. The refresh goes ahead without a glyph.
    Serial.println("Battery: gauge did not answer, refreshing without a glyph");
  }

  const char *err = startupError_;
  if (!err) err = library_.load(state_.viewIndex(), bitmap_);

  if (!err) {
    display_.drawPoem(bitmap_);
    display_.drawBatteryGlyph(battery);
    if (!pushToPanel()) return;
    errors_.clear();
    Serial.printf("Showing poem %d (%lu ms). Next refresh allowed in 180 s.\n",
                  state_.viewIndex() + 1, millis() - t0);
    return;
  }

  // The card failed (HARDWARE.md §8). One error refresh, not a loop: if this
  // message is already on the panel, leave it there.
  if (errors_.isShowing(err)) {
    Serial.printf("ERROR: %s. Already on the panel, so it is left untouched.\n", err);
    return;
  }
  display_.drawError(err);
  if (!pushToPanel()) return;
  // Recorded after the refresh, not before: if power is lost in between, the
  // cost is one repeated error refresh rather than an error never shown.
  errors_.set(err);
  Serial.printf("ERROR: %s. Error screen shown (%lu ms).\n", err, millis() - t0);
}

bool PoemApp::pushToPanel() {
  if (!display_.canRefresh()) {
    Serial.println("Refresh refused: cooldown still running.");  // update() checks first
    return false;
  }
  // The light comes on now, not after: refresh() blocks for 2.5 s, and this
  // is the only sign for that long that the press was accepted.
  light_.set(true);
  return display_.refresh();
}
