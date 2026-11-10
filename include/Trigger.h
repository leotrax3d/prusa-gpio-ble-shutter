#pragma once
#include <stdint.h>
struct Trigger {
  bool raw, stable, ready = false;
  uint32_t changed;
  Trigger(bool initial, uint32_t now): raw(initial), stable(initial), changed(now) {}
  bool update(bool level, uint32_t now, bool active, uint32_t debounce) {
    if (level != raw) { raw = level; changed = now; }
    if (uint32_t(now - changed) < debounce) return false;
    stable = raw;
    if (stable != active) { ready = true; return false; }
    if (!ready) return false;
    ready = false;
    return true;
  }
  void reset(bool level, uint32_t now) { raw = stable = level; changed = now; ready = false; }
};
