// Command type, clamping and JSON parsing (FR-1.3). No hardware calls.
#pragma once
#include <ArduinoJson.h>
#include <math.h>
#include <stddef.h>

#include "tt_config.h"

namespace tt {

struct Command {
  float vx = 0, vy = 0, w = 0;  // unitless, [-1, 1]
  bool hold = false;
};

// Clamp to [-1, 1]. NaN becomes 0. Infinity becomes +/-1.
inline float clampUnit(float v) {
  if (isnan(v)) return 0.0f;
  if (v > 1.0f) return 1.0f;
  if (v < -1.0f) return -1.0f;
  return v;
}

// Parse {"vx":..,"vy":..,"w":..,"hold":..}. Returns false (and leaves `out`
// untouched) for anything malformed. Missing fields default to 0 / false.
// Out-of-range numbers are clamped, never trusted.
inline bool parseCommand(const char* data, size_t len, Command& out) {
  if (data == nullptr || len == 0 || len > cfg::MAX_FRAME_BYTES) return false;
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) return false;
  if (!doc.is<JsonObjectConst>()) return false;

  Command c;
  const char* keys[3] = {"vx", "vy", "w"};
  float* dst[3] = {&c.vx, &c.vy, &c.w};
  for (int i = 0; i < 3; i++) {
    JsonVariantConst v = doc[keys[i]];
    if (v.isNull()) continue;
    if (!v.is<float>()) return false;  // wrong type: reject the whole frame
    *dst[i] = clampUnit(v.as<float>());
  }
  JsonVariantConst h = doc["hold"];
  if (!h.isNull()) {
    if (!h.is<bool>()) return false;
    c.hold = h.as<bool>();
  }
  out = c;
  return true;
}

}  // namespace tt
