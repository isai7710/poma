#include "ErrorRecord.h"

namespace {
const char *const NVS_KEY_ERROR = "error";
}  // namespace

ErrorRecord::ErrorRecord(Preferences &prefs)
  : prefs_(prefs) {}

void ErrorRecord::begin() {
  // isKey() first: asking for a key that does not exist logs an error.
  if (prefs_.isKey(NVS_KEY_ERROR)) {
    prefs_.getString(NVS_KEY_ERROR, shown_, sizeof(shown_));
  }
}

bool ErrorRecord::isShowing(const char *message) const {
  return strcmp(shown_, message) == 0;
}

void ErrorRecord::set(const char *message) {
  strlcpy(shown_, message, sizeof(shown_));
  prefs_.putString(NVS_KEY_ERROR, shown_);
}

void ErrorRecord::clear() {
  if (!showing()) return;  // the usual case: nothing to erase, so no flash write
  shown_[0] = '\0';
  prefs_.remove(NVS_KEY_ERROR);
}
