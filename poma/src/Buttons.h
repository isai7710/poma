// Buttons — the three buttons on the wing, polled and debounced.
//
// It reports which button was pressed. What a button means is PoemApp's
// decision, so remapping the buttons never touches this class.

#pragma once

#include <Arduino.h>

enum class Button : int8_t {
  None = -1,
  A,
  B,
  C
};

class Buttons {
public:
  Buttons(uint8_t pinA, uint8_t pinB, uint8_t pinC);

  // Active LOW, switch to GND, internal pull-ups.
  void begin();

  // Returns a button that went down since the last call, or Button::None.
  // Call it every ~20 ms. Sampling that slowly is the debounce: contact
  // bounce lasts a few ms, so at most one sample can land inside it.
  Button poll();

private:
  static constexpr int COUNT = 3;

  const uint8_t pins_[COUNT];
  bool wasDown_[COUNT] = { false, false, false };
};
