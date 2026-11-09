#include "Adafruit_ThinkInk.h"

#define EPD_CS 9
#define EPD_DC 10
#define SRAM_CS -1
#define EPD_RESET -1
#define EPD_BUSY -1
#define EPD_SPI &SPI

ThinkInk_290_Grayscale4_EAAMFGN display(EPD_DC, EPD_RESET, EPD_CS,
                                        SRAM_CS, EPD_BUSY, EPD_SPI);

// One full refresh to a solid colour, timed.
void solidRefresh(uint16_t color, const char *label) {
  display.clearBuffer();
  display.fillRect(0, 0, display.width(), display.height(), color);
  uint32_t t0 = millis();
  display.display();
  uint32_t t1 = millis();
  Serial.printf("  %-8s refresh: %lu ms\n", label, t1 - t0);
}

void setup() {
  Serial.begin(115200);
  delay(4000);
  Serial.println("--- Experiment 0b: conditioning + test pattern ---");

  display.begin(THINKINK_MONO);
  Serial.printf("Panel: %d x %d\n", display.width(), display.height());

  // --- Conditioning: 3 full black/white cycles ---
  for (int i = 0; i < 3; i++) {
    Serial.printf("Cycle %d\n", i + 1);
    solidRefresh(EPD_BLACK, "black");
    delay(30000);
    solidRefresh(EPD_WHITE, "white");
    delay(30000);
  }

  // --- Test pattern ---
  Serial.println("Test pattern");
  display.clearBuffer();
  int w = display.width();
  int h = display.height();

  display.fillRect(0, 0, w / 2, h, EPD_BLACK);      // solid black half
  display.fillRect(w / 2, 0, w / 2, h, EPD_WHITE);  // solid white half

  // 1px checkerboard in a patch on the white side — stresses edge crispness
  for (int y = 20; y < 60; y++)
    for (int x = w / 2 + 20; x < w / 2 + 60; x++)
      if ((x + y) & 1) display.drawPixel(x, y, EPD_BLACK);

  // A thin black frame so you can see the panel's true addressable edges
  display.drawRect(0, 0, w, h, EPD_BLACK);

  uint32_t t0 = millis();
  display.display();
  uint32_t t1 = millis();
  Serial.printf("  pattern refresh: %lu ms\n", t1 - t0);
  Serial.println("Done.");
}

void loop() {}
