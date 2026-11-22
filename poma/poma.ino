// poma — Daily Poem E-Paper Device
//
// Session 1 firmware: show one pre-rendered poem from the SD card and browse
// earlier ones with three buttons.
//
// On boot: restore view_index from NVS and show that poem once the refresh
// cooldown allows. Then poll three buttons:
//   A  next poem       (ceiling: today's poem, so no peeking ahead)
//   B  back to today
//   C  previous poem   (floor: the first poem)
// A press changes the panel at most once, and never sooner than 180 s after
// the previous refresh. Presses during the cooldown are dropped, not queued.
//
// The NeoPixel is the busy light: dim red while the cooldown is running (a
// press would be dropped), dark when a press will be accepted.
//
// Before every refresh the battery is read. Below 3.4 V the refresh is
// skipped; otherwise a small battery glyph is drawn top right over the poem.
//
// If the card fails (missing, no /poems folder, bad file) a one-line error
// screen replaces the poem. Each failure gets one error refresh, not a loop:
// the message on the panel is remembered in NVS and not drawn again.
//
// The code lives in src/, one class per part:
//   PoemApp          the behaviour: ties the parts below together
//   PoemDisplay      the e-paper panel, and the only caller of display()
//   RefreshCooldown  the 180 s rule
//   PoemLibrary      the poems on the SD card (read-only)
//   PoemBitmap       the 4736-byte bitmap format
//   ViewState        daily_index and view_index, persisted in NVS
//   ErrorRecord      which error message is on the panel, persisted in NVS
//   Buttons          the three buttons, polled and debounced
//   FuelGauge        the MAX17048 battery monitor
//   BusyLight        the NeoPixel
//   Board            pin numbers (HARDWARE.md §3)

#include "src/PoemApp.h"

PoemApp app;

void setup() {
  app.begin();
}

void loop() {
  app.update();
}
