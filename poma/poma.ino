// poma — Daily Poem E-Paper Device
//
// Step 4 of Session 1: battery check, battery glyph, and the busy light.
//
// On boot: restore view_index from NVS and show that poem once the refresh
// cooldown allows. Then poll three buttons:
//   A  next poem       (ceiling: today's poem, so no peeking ahead)
//   B  previous poem   (floor: the first poem)
//   C  back to today
// A press changes the panel at most once, and never sooner than 180 s after
// the previous refresh. Presses during the cooldown are dropped, not queued.
//
// The NeoPixel is the busy light: dim red while the cooldown is running (a
// press would be dropped), dark when a press will be accepted.
//
// Before every refresh the battery is read. Below 3.4 V the refresh is
// skipped; otherwise a small battery glyph is drawn top right over the poem.
//
// Card requirements:
//   - FAT32. The ESP32 Arduino core is built without exFAT support, and
//     64 GB cards ship as exFAT, so a factory-fresh card will not mount.
//   - A /poems folder holding 001.bin ... NNN.bin, 4736 bytes each.

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Preferences.h>
#include "Adafruit_ThinkInk.h"

// --- Pins (HARDWARE.md §3) --------------------------------------------------
#define EPD_CS 9
#define EPD_DC 10
#define SD_CS 5
#define SRAM_CS -1    // what the library is told: do not use the SRAM (§5)
#define EPD_RESET -1  // tied to the Feather's reset line
#define EPD_BUSY -1   // not routed on the wing
// The SRAM chip is still physically on the bus even though the library
// ignores it, so we hold its chip select high ourselves.
#define WING_SRAM_CS 6

// Buttons: active LOW, switch to GND, internal pull-ups.
// A and B are the reverse of HARDWARE.md §7: the device sits the other way
// round from what that table assumed (rotation 2), so the actions were swapped.
#define BTN_A 11  // next poem
#define BTN_B 12  // previous poem
#define BTN_C 13  // return to today. Shares its pin with LED_BUILTIN.

#define SD_SPI_HZ 4000000  // core default; a poem reads in ~18 ms

// 2 = upright in the enclosure with the ribbon cable on the RIGHT
#define PANEL_ROTATION 2

// --- Bitmap format (HARDWARE.md §4.6) ---------------------------------------
const int16_t PANEL_W = 296;
const int16_t PANEL_H = 128;
const size_t POEM_BYTES = (PANEL_W / 8) * PANEL_H;  // 37 * 128 = 4736

static uint8_t poemBuf[POEM_BYTES];

// Session 1 only: "today" is hardcoded. The RTC supplies it in Session 2.
// It is 5 rather than 0 so there are earlier poems to browse back to.
const int DAY_INDEX = 5;

// --- Battery ----------------------------------------------------------------
// The Feather carries a MAX17048 fuel gauge on I2C (found at 0x36 by a bus
// scan). We read two of its registers directly; that is all we need from it,
// so there is no library. Register layout is from the MAX17048 datasheet:
//   0x02 VCELL  16 bits, 78.125 uV per count
//   0x04 SOC    16 bits, 1/256 % per count (so the high byte is whole percent)
#define FUEL_GAUGE_ADDR 0x36
#define GAUGE_REG_VCELL 0x02
#define GAUGE_REG_SOC 0x04

// HARDWARE.md §8: below this, do not refresh. A brownout halfway through the
// waveform leaves a corrupted image; skipping leaves the last good poem.
const int BATTERY_MIN_MV = 3400;

struct Battery {
  bool known;   // false = the gauge did not answer
  int mv;       // cell voltage in millivolts
  int percent;  // the gauge's state-of-charge estimate, clamped to 0..100
};

bool readGauge(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(FUEL_GAUGE_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;  // false = keep the bus
  if (Wire.requestFrom((uint8_t)FUEL_GAUGE_ADDR, (size_t)2) != 2) return false;
  uint8_t high = Wire.read();  // two statements: the gauge sends the high
  uint8_t low = Wire.read();   // byte first, and the order must be fixed
  value = (uint16_t)high << 8 | low;
  return true;
}

Battery readBattery() {
  Battery b = { false, 0, 0 };
  uint16_t vcell, soc;
  if (readGauge(GAUGE_REG_VCELL, vcell) && readGauge(GAUGE_REG_SOC, soc)) {
    b.known = true;
    b.mv = vcell * 5UL / 64;  // 78.125 uV per count is exactly 5/64 mV
    b.percent = min(soc >> 8, 100);  // the gauge can report slightly over 100
  }
  return b;
}

// --- Busy light -------------------------------------------------------------
// The Feather's NeoPixel, at the dimmest level it has. Lit = the cooldown is
// running. It is lit, rather than dark, during the cooldown because the
// device is ready almost all day and a NeoPixel costs power only while lit.
// rgbLedWrite() and PIN_NEOPIXEL come with the ESP32 core; the core also
// switches the NeoPixel's power pin (GPIO 21) on before setup() runs.
const uint8_t BUSY_LIGHT_LEVEL = 1;  // 1..255. Raise it if 1 is too dim to see.

void setBusyLight(bool on) {
  static int8_t shown = -1;  // -1 = not written yet, so the first call writes
  if (shown == (int8_t)on) return;
  rgbLedWrite(PIN_NEOPIXEL, on ? BUSY_LIGHT_LEVEL : 0, 0, 0);  // red only
  shown = on;
}

// --- Storage ----------------------------------------------------------------
// Every function here returns nullptr on success or a short message on
// failure, sized to fit one line of the error screen (a later step).

// Drive every chip select on the shared SPI bus to "not selected".
// Must run before the first SPI transaction of any kind (HARDWARE.md §5).
void deselectAll() {
  const uint8_t pins[] = { EPD_CS, SD_CS, WING_SRAM_CS };
  for (uint8_t pin : pins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
}

const char *mountCard() {
  // format_f_empty defaults to false: a card that fails to mount is left
  // exactly as it was. We only ever open files for reading.
  if (!SD.begin(SD_CS, SPI, SD_SPI_HZ)) return "No SD card";
  return nullptr;
}

// True for exactly "NNN.bin". Rejects the clutter macOS leaves on FAT
// volumes (.DS_Store, ._001.bin, .Spotlight-V100) so it is never counted.
bool isPoemName(const char *name) {
  return strlen(name) == 7 && isdigit(name[0]) && isdigit(name[1]) && isdigit(name[2]) && strcasecmp(name + 3, ".bin") == 0;
}

// Counts poems/NNN.bin and checks the numbering runs 001..count with no gaps,
// because the day index maps straight onto a file number.
const char *countPoems(int &count) {
  count = 0;
  File dir = SD.open("/poems");
  if (!dir || !dir.isDirectory()) return "No /poems folder";

  int highest = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory() && isPoemName(f.name())) {
      count++;
      highest = max(highest, atoi(f.name()));
    }
  }
  if (count == 0) return "No poems on card";
  if (highest != count) return "Poem numbers have gaps";
  return nullptr;
}

// index is 0-based (it is what view_index holds); files are 1-based.
const char *loadPoem(int index, uint8_t *buf) {
  char path[24];
  snprintf(path, sizeof(path), "/poems/%03d.bin", index + 1);

  File f = SD.open(path, FILE_READ);
  if (!f) return "Poem file missing";
  if (f.size() != POEM_BYTES) return "Poem file wrong size";
  if (f.read(buf, POEM_BYTES) != POEM_BYTES) return "Poem read failed";
  return nullptr;
}

// --- Display ----------------------------------------------------------------
// Panel class and mode are fixed by HARDWARE.md §4. Do not change them
// without reading §4.1.
ThinkInk_290_Grayscale4_EAAMFGN display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS,
                                        EPD_BUSY, &SPI);

// HARD CONSTRAINT (HARDWARE.md §4.5): at most one refresh per 180 seconds.
const uint32_t REFRESH_COOLDOWN_MS = 180000;

// When the panel was last refreshed, in millis(). It starts at 0, which
// means BOOT COUNTS AS A REFRESH: the first refresh cannot happen until the
// device has been up for 180 s. Without a clock that survives reset we cannot
// know how recently the previous run refreshed (you may have just pressed
// reset, or re-uploaded), so we assume the worst. Session 2 replaces this
// with a timestamp that survives sleep.
static uint32_t lastRefreshMs = 0;

uint32_t cooldownRemainingMs() {
  // Unsigned subtraction stays correct when millis() wraps at 49 days.
  uint32_t since = millis() - lastRefreshMs;
  return since >= REFRESH_COOLDOWN_MS ? 0 : REFRESH_COOLDOWN_MS - since;
}

// The ONLY place in this firmware that may call display.display().
// It refuses, rather than waits, if the cooldown has not elapsed, so no
// caller can break the limit by mistake.
bool refreshPanel() {
  if (cooldownRemainingMs() > 0) return false;
  // Light up now, not after: display() blocks for 2.5 s, and this is the
  // only sign for that long that the press was accepted.
  setBusyLight(true);
  display.display();  // ~2.5 s: a fixed delay, BUSY is not wired (§4.4)
  lastRefreshMs = millis();
  return true;
}

// Draws into the library's RAM framebuffer only. Nothing reaches the panel
// until refreshPanel().
void drawPoem(uint8_t *bitmap) {
  display.clearBuffer();
  // `bitmap` is deliberately non-const: that selects GFX's RAM overload of
  // drawBitmap rather than the PROGMEM one (HARDWARE.md §4.6).
  display.drawBitmap(0, 0, bitmap, PANEL_W, PANEL_H, EPD_BLACK);
}

// The converter keeps a 16 x 8 px area clear for this, top right. These two
// numbers must match BATTERY_GLYPH in tools/render_poems.py.
const int16_t GLYPH_X = 275;
const int16_t GLYPH_Y = 5;

// Drawn over the poem, in the same coordinates as the bitmap. It is a
// snapshot: it shows the charge at the moment of this refresh and does not
// change until the next one. An empty outline means under 10 %.
void drawBatteryGlyph(const Battery &batt) {
  if (!batt.known) return;  // no reading: draw nothing rather than a guess
  display.drawRect(GLYPH_X, GLYPH_Y, 14, 8, EPD_BLACK);          // body
  display.fillRect(GLYPH_X + 14, GLYPH_Y + 2, 2, 4, EPD_BLACK);  // terminal
  int16_t fill = batt.percent / 10;  // 0..10 px: one pixel per 10 %
  if (fill > 0) display.fillRect(GLYPH_X + 2, GLYPH_Y + 2, fill, 4, EPD_BLACK);
}

// Battery -> card -> RAM -> panel, for one poem. The card is mounted and
// released inside this call, so it is never active while the panel is being
// driven (HARDWARE.md §5).
const char *showPoem(int index) {
  Battery batt = readBattery();
  if (batt.known) {
    Serial.printf("Battery: %d mV, %d %%\n", batt.mv, batt.percent);
    if (batt.mv < BATTERY_MIN_MV) return "Battery too low to refresh";
  } else {
    // Deliberately not an error: the gauge may not answer with no battery
    // plugged in, and then we are on USB power, where a brownout is not the
    // risk. The refresh goes ahead without a glyph.
    Serial.println("Battery: gauge did not answer, refreshing without a glyph");
  }

  const char *err = mountCard();
  if (!err) err = loadPoem(index, poemBuf);
  SD.end();
  if (err) return err;

  drawPoem(poemBuf);
  drawBatteryGlyph(batt);
  if (!refreshPanel()) return "Refresh refused";  // callers check the cooldown first
  return nullptr;
}

// --- Buttons ----------------------------------------------------------------
const uint8_t BUTTONS[] = { BTN_A, BTN_B, BTN_C };

// Returns the pin of a button that went down since the last call, or -1.
// Call it every ~20 ms. Sampling that slowly is the debounce: contact bounce
// lasts a few ms, so at most one sample can land inside it.
int pollButtons() {
  static bool wasDown[3] = { false, false, false };
  int pressed = -1;
  for (int i = 0; i < 3; i++) {
    bool down = digitalRead(BUTTONS[i]) == LOW;
    if (down && !wasDown[i]) pressed = BUTTONS[i];
    wasDown[i] = down;
  }
  return pressed;
}

// --- State (HARDWARE.md §7) -------------------------------------------------
Preferences prefs;                // NVS: the only mutable storage. The card is read-only.
static int dailyIndex;            // today's poem. Hardcoded for now.
static int viewIndex;             // the poem being shown. Persisted in NVS.
static bool ready = false;        // false = setup failed; loop() does nothing
static bool needsRefresh = true;  // true at boot: we cannot know what the panel shows

void onButton(int pin) {
  int target = viewIndex;
  const char *name = "?";
  if (pin == BTN_A) {
    name = "A (next)";
    target = min(viewIndex + 1, dailyIndex);
  }
  if (pin == BTN_B) {
    name = "B (previous)";
    target = max(viewIndex - 1, 0);
  }
  if (pin == BTN_C) {
    name = "C (today)";
    target = dailyIndex;
  }

  if (target == viewIndex) {
    // Already at the floor, the ceiling, or today. No change, so no refresh.
    Serial.printf("Button %s: already there, nothing to do\n", name);
  } else if (uint32_t ms = cooldownRemainingMs()) {
    // Dropped, not queued (HARDWARE.md §7): acknowledging it on the panel
    // would itself cost a refresh.
    Serial.printf("Button %s: ignored, cooldown has %lu s left\n", name,
                  (unsigned long)(ms + 999) / 1000);
  } else {
    Serial.printf("Button %s: poem %d -> %d\n", name, viewIndex + 1, target + 1);
    viewIndex = target;
    prefs.putInt("view", viewIndex);
    needsRefresh = true;
  }
}

void setup() {
  deselectAll();
  for (uint8_t pin : BUTTONS) pinMode(pin, INPUT_PULLUP);
  // Dark until we know setup succeeded. Written explicitly because a NeoPixel
  // can keep its last colour across a reset.
  setBusyLight(false);
  Wire.begin();  // for the fuel gauge

  Serial.begin(115200);
  // Development only: give the USB serial monitor up to 4 s to attach.
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 4000;) delay(10);
  Serial.println("\n--- poma step 4: battery and busy light ---");

  Battery batt = readBattery();
  if (batt.known) {
    Serial.printf("Battery: %d mV, %d %%\n", batt.mv, batt.percent);
  } else {
    Serial.println("Battery: gauge did not answer");
  }

  // How many poems are there? Needed once, to turn the day into an index.
  int count = 0;
  const char *err = mountCard();
  if (!err) err = countPoems(count);
  SD.end();
  if (err) {
    // Leaving the panel alone keeps the last good poem on it (§8).
    Serial.printf("ERROR: %s. Panel left untouched.\n", err);
    return;
  }

  dailyIndex = DAY_INDEX % count;
  prefs.begin("poma");
  // First boot ever: no saved value, so start on today's poem. The clamp
  // covers a saved value that is out of range for the card now inserted.
  viewIndex = constrain(prefs.getInt("view", dailyIndex), 0, dailyIndex);
  Serial.printf("%d poems. Today is poem %d. Restored view: poem %d.\n", count,
                dailyIndex + 1, viewIndex + 1);

  // begin() talks to the controller but does not refresh.
  display.begin(THINKINK_MONO);
  display.setRotation(PANEL_ROTATION);

  Serial.printf("First refresh in %lu s (boot counts as a refresh).\n",
                (unsigned long)(cooldownRemainingMs() + 999) / 1000);
  ready = true;
}

void loop() {
  delay(20);  // button sampling period; see pollButtons()
  if (!ready) return;

  // Lit for as long as a press would be dropped. Only writes on a change.
  setBusyLight(cooldownRemainingMs() > 0);

  int pin = pollButtons();
  if (pin >= 0) onButton(pin);

  // The single place a refresh is started. refreshPanel() enforces the
  // cooldown again on its own; checking here just avoids touching the card
  // for a refresh that would be refused.
  if (needsRefresh && cooldownRemainingMs() == 0) {
    needsRefresh = false;  // one attempt per request, even if it fails
    uint32_t t0 = millis();
    const char *err = showPoem(viewIndex);
    if (err) {
      Serial.printf("ERROR: %s. Panel left untouched.\n", err);
    } else {
      Serial.printf("Showing poem %d (%lu ms). Next refresh allowed in 180 s.\n",
                    viewIndex + 1, millis() - t0);
    }
  }
}
