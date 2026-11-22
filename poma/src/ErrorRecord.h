// ErrorRecord — remembers, across resets, which error message is on the panel.
//
// HARDWARE.md §8: a failure gets one error refresh, not a loop. After the
// error screen has been drawn the message is saved in NVS. While the same
// failure persists, every later boot finds its message already recorded and
// leaves the panel alone. Showing a poem clears the record.
//
// It stores the message rather than a bare flag so that a different failure
// replaces a stale message, instead of leaving the wrong one on the panel.

#pragma once

#include <Arduino.h>
#include <Preferences.h>

class ErrorRecord {
public:
  // The caller owns the NVS handle and must have opened it before begin().
  explicit ErrorRecord(Preferences &prefs);

  // Loads the saved message, if there is one.
  void begin();

  // True if the panel is showing an error screen (any message).
  bool showing() const {
    return shown_[0] != '\0';
  }
  // True if the panel is already showing exactly this message.
  bool isShowing(const char *message) const;

  // Call after an error screen has been refreshed onto the panel.
  void set(const char *message);
  // Call after a poem has been refreshed onto the panel.
  void clear();

private:
  Preferences &prefs_;
  char shown_[32] = "";  // empty = no error on the panel
};
