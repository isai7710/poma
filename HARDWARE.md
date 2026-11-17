# HARDWARE.md — Daily Poem E-Paper Device

Reference for the hardware stack, verified findings, and known traps.
Written after a hands-on bring-up session. **Findings marked VERIFIED were
observed on the physical device. Findings marked ASSUMED have not been tested.**

---

## 1. Project Summary

A small, rechargeable, fully offline e-paper device that displays one short
poem per day. No networking. The ESP32 spends >99.9% of its time in deep
sleep, waking on an RTC alarm to change the poem, or on a button press to
browse previous poems.

Poems are **pre-rendered to 1-bit bitmaps on a developer machine** and stored
on a microSD card. The firmware does no text layout - it reads a bitmap and
blits it.

---

## 2. Hardware Inventory

| Component | Product                                                                       | Link                                  |
| --------- | ----------------------------------------------------------------------------- | ------------------------------------- |
| MCU       | Adafruit Feather ESP32-S3 (8MB Flash with w.FL, has MAX17048 battery monitor) | https://www.adafruit.com/product/5885 |
| Display   | Adafruit 2.9" Grayscale eInk FeatherWing (SSD1680)                            | https://www.adafruit.com/product/4777 |
| RTC       | Adafruit DS3231 Precision RTC - STEMMA QT                                     | https://www.adafruit.com/product/5188 |
| Battery   | LiPo (3.7V 1200mAh)                                                           | https://www.adafruit.com/product/258  |
| Coin cell | CR1220 (RTC backup, **not included with RTC**)                                | -                                     |
| Storage   | microSD card (must be FAT32)                                                  | -                                     |

### Reference documentation

- Display guide: https://learn.adafruit.com/adafruit-2-9-eink-display-breakouts-and-featherwings
- RTC guide: https://learn.adafruit.com/adafruit-ds3231-precision-rtc-breakout
- Adafruit_EPD: https://github.com/adafruit/Adafruit_EPD
- Adafruit_GFX: https://github.com/adafruit/adafruit-gfx-library
- RTClib: https://github.com/adafruit/RTClib

---

## 3. Pin Map

```cpp
// --- eInk FeatherWing (fixed by PCB traces) ---
#define EPD_CS     9    // VERIFIED
#define EPD_DC    10    // VERIFIED
#define SD_CS      5    // VERIFIED
#define SRAM_CS   -1    // VERIFIED — deliberately NOT 6. See §5.
#define EPD_RESET -1    // VERIFIED — tied to Feather RESET, not separately routed
#define EPD_BUSY  -1    // VERIFIED — not routed on the FeatherWing
#define EPD_SPI   &SPI

// --- Buttons on the FeatherWing (active LOW, switch to GND) ---
#define BTN_A     11    // Previous poem
#define BTN_B     12    // Next poem
#define BTN_C     13    // Return to today

// --- DS3231 alarm wire (user-soldered or hand wired) ---
#define RTC_SQW   18    // A0 on the Feather silkscreen. ASSUMED mapping.
```

### Pin notes

- **Do not use the `#if defined(ESP32)` pin ladder** from
  `examples/FeatherWingGray4/FeatherWingGray4.ino`. `ESP32` is very likely
  defined for the ESP32-S3 as well, which would select pins 32/15/33 intended
  for the classic ESP32 Feather. Use the explicit defines above.
- **GPIO 13 is shared with `LED_BUILTIN`** on Feather boards. I removed the LED
  with a soldering iron.
- **ESP32-S3 RTC-capable GPIOs are 0–21.** All four wake sources (3 buttons +
  SQW) fall in this range, so they can share one `ext1` wake mask.
- SPI pins (SCK 36 / MOSI 35 / MISO 37) are outside the RTC range. This is
  fine — they are not wake sources.
- **VERIFY:** that `A0` maps to GPIO 18 on this board, against the official
  ESP32-S3 Feather pinout page.

---

## 4. Display: Critical Configuration

This configuration is **VERIFIED working**. Do not change it without reading
§4.1, which documents a multi-hour debugging session.

```cpp
#include "Adafruit_ThinkInk.h"

ThinkInk_290_Grayscale4_EAAMFGN display(EPD_DC, EPD_RESET, EPD_CS,
                                        SRAM_CS, EPD_BUSY, EPD_SPI);

display.begin(THINKINK_MONO);
display.setRotation(2);   // upright = ribbon cable on the RIGHT
```

- Panel: **296 x 128**, 4-level grayscale capable, SSD1680 controller.
- Operating mode: **MONO only.** See §4.2 for why.

### 4.1 Panel class selection — THIS IS THE BIG TRAP

Adafruit product #4777 has shipped with multiple different panels over time
(IL0373, then SSD1680; ribbon revisions FPC-A005 and FPC-7519rev.b, the latter
with sub-revisions 0x44 and 0xca). `Adafruit_EPD` models these as **one C++
class per panel variant**.

**The SSD1680 stores a panel ID byte, but `Adafruit_EPD` never reads it back.**
The library therefore cannot auto-detect the panel; class selection is manual
and trial-and-error. This single fact explains most of the confusion in this
area.

A `ThinkInk_*` class bundles two separate things:

1. Panel identity data (RAM offset, buffer polarity, dimensions)
2. Mode implementations — a `THINKINK_MONO` branch and a `THINKINK_GRAYSCALE4` branch

**`ThinkInk_290_Grayscale4_FPC7519` has a stub mono branch.** Its
`THINKINK_MONO` path reuses `ssd1680_fpc7519_init_code`, which carries a
_4-level grayscale_ waveform. Driving mono content with a grayscale waveform
leaves pixels near threshold, so the panel's intrinsic per-pixel variation
becomes visible as **coarse random speckle across the whole display**.
This was observed directly on hardware.

`ThinkInk_290_Grayscale4_EAAMFGN` has a real, separate mono init sequence
(`ti_290mfgn_monofull_init_code`) and a dedicated mono LUT
(`ti_290mfgn_monofull_lut_code`). **In mono mode, EAAMFGN is the only working
choice**, regardless of which physical panel is fitted.

Note: this does _not_ establish which physical panel is present. Mono
waveforms have large margin (full-rail transitions), so a close-enough
waveform works fine. Grayscale has no such margin.

**If grayscale is ever needed**, the working recipe per
`examples/FeatherWingGray4/FeatherWingGray4.ino` is:

```cpp
ThinkInk_290_Grayscale4_FPC7519 display(...);
display.begin(THINKINK_GRAYSCALE4);
display.setColorBuffer(1, true);   // polarity flip for 0xca sub-revision
display.setBlackBuffer(0, true);
display.setRotation(0);
```

### 4.2 Why mono, not grayscale

From the panel class headers:

| Mode                  | `default_refresh_delay` |
| --------------------- | ----------------------- |
| `THINKINK_MONO`       | 800 ms                  |
| `THINKINK_GRAYSCALE4` | 9000 ms                 |

Grayscale costs roughly **11x the awake time per update**. Adafruit also
documents that SSD1680 4-level grayscale on this panel looks grainier than the
older IL0373 part. For a battery device showing black text, mono is correct.

### 4.3 Example files — which to trust

The `examples/` directory mixes three kinds of file:

- **Board-specific** (`FeatherWingTest`, `FeatherWingGray4`) — curated for one
  product, correct pins, quirk workarounds included. **Prefer these.**
- **Mode-generic** (`ThinkInk_mono`, `ThinkInk_gray4`) — menus listing every
  panel class as commented-out lines. Comments are loosely maintained and
  contain at least one scope error (listing FPC7519 as a mono option).
- **Feature-specific** — partial refresh, image reader, etc.

The learn guide's Arduino page is **stale** — it still names
`ThinkInk_290_Grayscale4_T5`, which is two hardware revisions out of date.

### 4.4 Measured timing

VERIFIED: every `display()` call in mono mode returned **2562–2564 ms**,
identical across panel classes, modes, and content.

This is a **hardcoded delay, not a measurement.** Because `EPD_BUSY` is not
routed on the FeatherWing, the library waits a fixed worst-case interval and
has no idea when the panel actually finished.

The BUSY signal is available on a solder pad on the wing. Wiring it to a spare
RTC-capable GPIO would let the driver poll for real completion and likely cut
awake time substantially. **Not yet done.** Worth revisiting during power
optimization.

### 4.5 Refresh frequency limit — HARD CONSTRAINT

**Do not refresh the display more than once per 180 seconds.** Adafruit warns
this may permanently damage the panel. Repeated rapid cycling causes thermal
and electrical stress and can produce permanent image retention.

Consequences for code:

- **Never put `display.display()` in a `loop()` with a short delay.** Several
  Adafruit example sketches do exactly this; they are wiring smoke-tests, not
  templates.
- All display work belongs in `setup()` or behind an enforced cooldown.
- Button presses must be rate-limited (see §7).
- Normal operation is ~365 refreshes/year, far below any lifetime limit. The
  constraint bites during _development_, not in production.

### 4.6 Drawing bitmaps

Use the public GFX API, **not** a `memcpy` into the framebuffer. The internal
buffer layout is library-private (two planes, inversion flags, `_xram_offset`)
and varies between panel classes.

```cpp
display.clearBuffer();
display.drawBitmap(0, 0, buf, 296, 128, EPD_BLACK);
display.display();
```

Bitmap format: **296 / 8 = 37 bytes per row exactly** (no padding),
37 x 128 = **4,736 bytes per image**. MSB = leftmost pixel, 1 = black,
row-major, top to bottom. This matches `drawBitmap()`'s expected layout.

GFX has both PROGMEM and RAM overloads of `drawBitmap`. The RAM
overload (`uint8_t *bitmap`) is the one needed here.

---

## 5. SPI Bus Sharing — VERIFIED HAZARD

The eInk controller, the on-wing SPI SRAM, and the microSD socket **share
MOSI/MISO/SCK**. Chip select is the only thing keeping them apart.

**Observed failure:** with `SRAM_CS 6` and a microSD card inserted, the display
rendered with vertical banding. Setting `SRAM_CS -1` eliminated it completely.

**Cause:** with `SRAM_CS 6`, the framebuffer lives in the external SRAM and is
read back over SPI during `display()`. `SD_CS` (pin 5) was never driven, so an
inserted card floated on a shared bus and could contend on MISO during those
reads. Corrupted bytes in a row-packed framebuffer appear as vertical bands.

**Resolution: use `SRAM_CS -1`.** The ESP32-S3 has ample RAM, the framebuffer
is then never on the SPI bus at all, and `drawPixel()` becomes a memory write
instead of an SPI transaction (also faster).

**General rule for this project: every device on the shared SPI bus must be
explicitly deselected.** When adding SD support, ensure `SD_CS` is driven HIGH
when not in use and that the SD library releases the bus before `display()`.

---

## 6. RTC (DS3231)

- I²C via STEMMA QT. **STEMMA QT carries I²C only** — the SQW/INT pin is on the
  0.1" header and must be wired separately.
- Temperature-compensated, ~±2ppm (about a minute per year).
- **Requires a CR1220 coin cell** for backup across power loss. Not included
  with the board.
- Chosen over the ESP32's internal timer because the Feather ESP32-S3 likely
  lacks a 32.768 kHz crystal, leaving an uncompensated RC oscillator with
  drift plausibly in the 0.1–1%/day range.

### Setup gotchas (classic DS3231 traps)

1. **`writeSqwPinMode(DS3231_OFF)`** is required to make the SQW/INT pin act as
   an alarm interrupt rather than a square-wave output.
2. **`clearAlarm(1)` must be called after every wake.** If the alarm flag is
   not cleared, INT stays LOW and the ESP32 wakes immediately in a loop.
3. **SQW is open-drain** and needs a pull-up. **VERIFY** whether the Adafruit
   breakout populates one; if not, add an external 10k.
4. Setting the time from `__DATE__`/`__TIME__` uses _compile_ time, not upload
   time — expect a 10–30 second offset. Acceptable for a daily device.
5. **Check for an always-on power LED** on the breakout. A continuous few mA
   would dominate the power budget; the DS3231 itself draws tens of µA.

### Poem index derivation

The displayed poem is a **pure function of the date**, not an incrementing
counter:

```
index = (days since EPOCH_DATE) % poem_count
```

This is drift-free, survives missed wakes, and resumes correctly if the device
is powered off for a period.

`POEM_CHANGE_HOUR` should be a `#define` (recommended: 4, i.e. 4am — nobody is
watching, avoids changeover while reading).

---

## 7. Buttons, Sleep, and UI Behavior

### Button map

| Button | GPIO | Action          |
| ------ | ---- | --------------- |
| A      | 11   | Next poem       |
| B      | 12   | Return to today |
| C      | 13   | Previous poem   |

### State model

Two values. `daily_index` is derived from the RTC date (§6). `view_index`
persists in NVS.

| Event             | Effect                                          |
| ----------------- | ----------------------------------------------- |
| RTC alarm (daily) | `view_index = daily_index`, refresh             |
| Button A          | `view_index--` (floor 0), refresh               |
| Button B          | `view_index++` (ceiling `daily_index`), refresh |
| Button C          | `view_index = daily_index`, refresh             |

The ceiling prevents peeking at future poems. The daily alarm always returns
to today's poem, so the device self-heals if left on an old one.

### Design principles

- **No menus.** Each menu step costs a full refresh (2.5s + lifetime budget).
  One button press = one direct action = at most one refresh.
- **No "new poem available" notification.** Displaying a badge costs exactly
  the same refresh as displaying the poem itself, so it is strictly worse.
  On e-paper there is no such thing as a cheap visual hint.
- **Battery indicator is drawn inline** in the poem layout — no button, no
  extra refresh. The ESP32-S3 Feather has an I²C battery monitor.
- Deep browsing is not viable: the 180s cooldown means 5 presses = 15 minutes.
  Browsing is realistically for "what was yesterday's?"

### Deep sleep wake configuration

All four wake sources (3 buttons + SQW) are active-LOW and RTC-capable, so
they share one mask:

```cpp
esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
```

**TRAP: internal pullups and pulldowns do not work when RTC peripherals are
powered down**, which is the deep-sleep default. Either keep the domain
powered (`esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)`,
costs µA) or rely on ESP-IDF's automatic HOLD on wakeup IOs. **Verify by
measurement over hours, not seconds** — this fails intermittently.
Three external 10k pull-ups solve it permanently if flaky.

### Refresh cooldown

Store the last refresh timestamp in RTC slow memory, which survives deep sleep:

```cpp
RTC_DATA_ATTR uint64_t last_refresh_us = 0;
```

On wake, if under 180s since the last refresh, sleep again without refreshing.
A too-soon press produces no visible feedback — this is accepted, since
feedback would itself require a refresh.

**VERIFY:** that `esp_timer_get_time()` keeps counting across deep sleep on the
ESP32-S3. Cheap test: sleep 10s, wake, print it.

---

## 8. Storage and Failure Behavior

- **SD card is strictly read-only.** All mutable state lives in ESP32 NVS
  (Arduino `Preferences` library). This removes write corruption, mid-write
  brownouts, and filesystem repair from the failure surface.
- Poems: `poems/001.bin` … `poems/NNN.bin`, 4,736 bytes each. ~1.7 MB per year.
- File count is determined at boot.

### Failure handling

- On failure (no card, missing directory, missing file, wrong file size):
  render a minimal single-line GFX error screen, set a flag in NVS. If the flag
  is already set, sleep without refreshing — one error refresh, not a loop.
- **Check battery voltage before any refresh.** Below ~3.4V, skip the refresh
  and sleep. A brownout mid-waveform freezes particles in transit, leaving a
  corrupted image until the next successful refresh.
- Because e-paper is bistable, "do nothing" leaves the last good poem visible
  indefinitely. The device degrades into a static page rather than a corrupted
  one. **Design for this — it is the best property of the technology.**

---

## 9. Mental Model: Where Problems Live

Three layers. Knowing which one a symptom belongs to tells you where to look.

| Layer                               | Responsibility                                    | Symptom signature                                 |
| ----------------------------------- | ------------------------------------------------- | ------------------------------------------------- |
| `Adafruit_GFX`                      | coordinates, text, shapes                         | wrong position or size                            |
| `Adafruit_EPD` / `Adafruit_SSD1680` | framebuffer, SPI transport, `display()`           | corruption, shifts, missing data                  |
| `ThinkInk_*` panel class            | init sequence, waveform LUT, RAM offset, polarity | speckle, inversion, wrong gray levels, 8px strips |

"Content is correct but rendering quality is wrong" is the panel-class
signature.

### What `display()` actually does

1. Power up — reset (software reset here, since `EPD_RESET` is -1), then
   register writes: driver output control, data entry mode, border waveform,
   temperature sensor source, waveform LUT if the class supplies one.
2. Set RAM window and address counter.
3. Write RAM — `EPD_DC` LOW for the command byte, HIGH for the data stream;
   the framebuffer goes out over SPI (~4.7 KB, single-digit ms).
4. Trigger update — charge pump on, waveform clocked out through source/gate
   drivers. **This is where all the time and current go.**
5. Wait for busy — polls the BUSY pin, or (here) sleeps a fixed delay.
6. Power down — controller deep-sleep command, charge pump off.

---

## 10. Open Items / To Verify

| Item                                                   | Status                        |
| ------------------------------------------------------ | ----------------------------- |
| `A0` → GPIO 18 mapping on ESP32-S3 Feather             | ASSUMED                       |
| `SD_CS` = 5 correct on this board                      | ASSUMED, untested             |
| SD + EPD coexisting while both in active use           | UNTESTED                      |
| Deep-sleep wake with `EPD_RESET` tied to Feather reset | UNTESTED                      |
| Pull-up behavior on wake pins during deep sleep        | UNTESTED — measure over hours |
| `esp_timer_get_time()` persistence across deep sleep   | UNVERIFIED                    |
| SQW pull-up present on Adafruit DS3231 breakout        | UNVERIFIED                    |
| Actual deep-sleep current                              | UNMEASURED                    |
| `drawBitmap` RAM vs PROGMEM overload                   | UNVERIFIED                    |
| Wiring the BUSY pad to reduce awake time               | NOT DONE — optimization       |

---

## 11. Content Constraint

**Poems must be public domain.** Source from Project Gutenberg. US copyright
generally means pre-1930 publication is safe. Dickinson, Whitman, Blake,
Keats, Poe, Rossetti, Hopkins, early Frost and Yeats are all fine.
Do not scrape contemporary poetry.
