#include "ViewState.h"

namespace {
const char *const NVS_NAMESPACE = "poma";
const char *const NVS_KEY_VIEW = "view";
}  // namespace

void ViewState::begin(int dailyIndex) {
  dailyIndex_ = dailyIndex;
  prefs_.begin(NVS_NAMESPACE);
  viewIndex_ = constrain(prefs_.getInt(NVS_KEY_VIEW, dailyIndex_), 0, dailyIndex_);
}

int ViewState::nextIndex() const {
  return min(viewIndex_ + 1, dailyIndex_);
}

int ViewState::previousIndex() const {
  return max(viewIndex_ - 1, 0);
}

void ViewState::moveTo(int index) {
  viewIndex_ = index;
  prefs_.putInt(NVS_KEY_VIEW, viewIndex_);
}
