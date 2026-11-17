#include "Buttons.h"

Buttons::Buttons(uint8_t pinA, uint8_t pinB, uint8_t pinC)
  : pins_{ pinA, pinB, pinC } {}

void Buttons::begin() {
  for (uint8_t pin : pins_) pinMode(pin, INPUT_PULLUP);
}

Button Buttons::poll() {
  Button pressed = Button::None;
  for (int i = 0; i < COUNT; i++) {
    bool down = digitalRead(pins_[i]) == LOW;
    if (down && !wasDown_[i]) pressed = (Button)i;  // pins_ is in A, B, C order
    wasDown_[i] = down;
  }
  return pressed;
}
