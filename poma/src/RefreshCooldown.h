// RefreshCooldown — the 180-second rule, and nothing else.
//
// HARD CONSTRAINT (HARDWARE.md §4.5): the panel may be refreshed at most once
// per 180 seconds. This class is the clock for that rule; PoemDisplay owns one
// and consults it before every refresh.
//
// BOOT COUNTS AS A REFRESH. The timestamp starts at 0, so the first refresh
// cannot happen until the device has been up for 180 s. Without a clock that
// survives reset we cannot know how recently the previous run refreshed (you
// may have just pressed reset, or re-uploaded), so we assume the worst.
// Session 2 replaces this with a timestamp that survives sleep, and this
// class is the only place that will need to change.

#pragma once

#include <Arduino.h>

class RefreshCooldown {
public:
  static constexpr uint32_t PERIOD_MS = 180000;

  // 0 = a refresh is allowed now.
  uint32_t remainingMs() const {
    // Unsigned subtraction stays correct when millis() wraps at 49 days.
    uint32_t since = millis() - lastRefreshMs_;
    return since >= PERIOD_MS ? 0 : PERIOD_MS - since;
  }

  // Call immediately after a refresh.
  void restart() {
    lastRefreshMs_ = millis();
  }

private:
  uint32_t lastRefreshMs_ = 0;
};
