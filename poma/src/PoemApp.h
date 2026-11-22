// PoemApp — the device's behaviour.
//
// It owns one of each part and is the only class that knows how they work
// together: what a button means, when to refresh, when the busy light is lit,
// what a low battery prevents, and what happens when the card fails. The
// parts do not know about each other. It is also the only class that prints
// to Serial.
//
// The sketch calls begin() once from setup() and update() from loop().

#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "BusyLight.h"
#include "Buttons.h"
#include "ErrorRecord.h"
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

  // One attempt to bring the panel up to date: battery -> card -> RAM ->
  // panel. Ends with a poem on the panel, an error screen on the panel, or
  // the panel left untouched, and prints which.
  void refreshPanel();

  // Lights the busy light and pushes the framebuffer to the panel.
  bool pushToPanel();

  Preferences prefs_;  // NVS: the only mutable storage. The card is read-only.

  FuelGauge gauge_;
  BusyLight light_;
  PoemLibrary library_;
  PoemDisplay display_;
  Buttons buttons_;
  ViewState state_;
  ErrorRecord errors_;

  PoemBitmap bitmap_;  // the poem most recently read from the card

  // Set if begin() could not count the poems. There is then no "today" and
  // nothing to browse: the buttons are off and only the error can be shown.
  const char *startupError_ = nullptr;

  bool needsRefresh_ = true;  // true at boot: we cannot know what the panel shows
};
