// PoemApp — the device's behaviour.
//
// It owns one of each part and is the only class that knows how they work
// together: what a button means, when to refresh, when the busy light is lit,
// and what a low battery prevents. The parts do not know about each other.
// It is also the only class that prints to Serial.
//
// The sketch calls begin() once from setup() and update() from loop().

#pragma once

#include <Arduino.h>

#include "BusyLight.h"
#include "Buttons.h"
#include "FuelGauge.h"
#include "PoemBitmap.h"
#include "PoemDisplay.h"
#include "PoemLibrary.h"
#include "ViewState.h"

class PoemApp {
public:
  PoemApp();

  void begin();
  void update();

private:
  void onButton(Button button);

  // Battery -> card -> RAM -> panel, for one poem. Returns nullptr on success
  // or a short message saying why the panel was left untouched.
  const char *showPoem(int index);

  FuelGauge gauge_;
  BusyLight light_;
  PoemLibrary library_;
  PoemDisplay display_;
  Buttons buttons_;
  ViewState state_;

  PoemBitmap bitmap_;         // the poem most recently read from the card
  bool ready_ = false;        // false = begin() failed; update() does nothing
  bool needsRefresh_ = true;  // true at boot: we cannot know what the panel shows
};
