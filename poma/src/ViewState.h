// ViewState — which poem is today's and which one is being shown.
//
// The state model of HARDWARE.md §7: two values. daily_index is today's poem;
// view_index is the poem on the panel, and it persists in NVS, which is the
// only mutable storage in the device (the card is read-only).
//
// Indexes are 0-based. The view can never go below the first poem or past
// today's, so there is no peeking ahead.

#pragma once

#include <Arduino.h>
#include <Preferences.h>

class ViewState {
public:
  // The caller owns the NVS handle and must have opened it before begin().
  explicit ViewState(Preferences &prefs);

  // Restores the saved view. On the first boot ever there is no saved value,
  // so the view starts on today's poem. A saved value that is out of range
  // for the card now inserted is clamped.
  void begin(int dailyIndex);

  int dailyIndex() const {
    return dailyIndex_;
  }
  int viewIndex() const {
    return viewIndex_;
  }

  // Where each action would land. These change nothing; the caller compares
  // the answer with viewIndex() and calls moveTo() if it decides to go there.
  int nextIndex() const;      // stops at today's poem
  int previousIndex() const;  // stops at the first poem

  // Changes the view and saves it to NVS.
  void moveTo(int index);

private:
  Preferences &prefs_;
  int dailyIndex_ = 0;
  int viewIndex_ = 0;
};
