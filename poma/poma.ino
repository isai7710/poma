// poma — Daily Poem E-Paper Device
//
// Step 3 of Session 1: buttons and persistent view_index.
//
// On boot: restore view_index from NVS and show that poem once the refresh
// cooldown allows. Then poll three buttons:
//   A  next poem       (ceiling: today's poem, so no peeking ahead)
//   B  previous poem   (floor: the first poem)
//   C  back to today
// A press changes the panel at most once, and never sooner than 180 s after
// the previous refresh. Presses during the cooldown are dropped, not queued.
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

// Card -> RAM -> panel, for one poem. The card is mounted and released
// inside this call, so it is never active while the panel is being driven
// (HARDWARE.md §5).
const char *showPoem(int index) {
  const char*err = mountCard();
  if (!err) err = loadPoem(index, poemBuf);
  SD.end();
  if (err) return err;

  drawPoem(poemBuf);
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

// --- Temporary diagnostics (removed in the battery step) --------------------
// Lists what answers on I2C so we know which fuel gauge this Feather carries
// before writing battery code against it. Read-only.
void scanI2C() {
  Wire.begin();
  Serial.print("I2C devices:");
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    const char *name = addr == 0x36   ? " (MAX17048 fuel gauge)"
                       : addr == 0x0B ? " (LC709203F fuel gauge)"
                       : addr == 0x68 ? " (DS3231 RTC)"
                                      : "";
    Serial.printf(" 0x%02X%s", addr, name);
  }
  Serial.println();
}

// With nothing pressed, every button must read HIGH. Button C is the one to
// watch: its pin also feeds the on-board LED, which loads the pull-up.
void reportIdleButtons() {
  const char *names[] = { "A", "B", "C" };
  for (int i = 0; i < 3; i++) {
    int lows = 0;
    for (int n = 0; n < 50; n++) {
      if (digitalRead(BUTTONS[i]) == LOW) lows++;
      delay(2);
    }
    Serial.printf("Button %s (GPIO %d) idle: %s\n", names[i], BUTTONS[i],
                  lows == 0 ? "HIGH, ok" : "reads LOW with nothing pressed - PROBLEM");
  }
}

void setup() {
  deselectAll();
  for (uint8_t pin : BUTTONS) pinMode(pin, INPUT_PULLUP);

  Serial.begin(115200);
  // Development only: give the USB serial monitor up to 4 s to attach.
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 4000;) delay(10);
  Serial.println("\n--- poma step 3: buttons and NVS ---");
  scanI2C();
  reportIdleButtons();

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
