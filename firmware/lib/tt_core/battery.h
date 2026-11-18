// Battery state with 1 s averaging and hysteresis (FR-1.8). Pure logic.
#pragma once
#include <math.h>
#include <stdint.h>

#include "tt_config.h"

namespace tt {

enum class BatteryState : uint8_t { Ok = 0, Limited = 1, Stop = 2 };

inline const char* batteryStateName(BatteryState s) {
  switch (s) {
    case BatteryState::Ok: return "ok";
    case BatteryState::Limited: return "limited";
    default: return "low";
  }
}

// Divider on G36: 100k top, 33k bottom. VBAT_CAL is set once against a multimeter.
inline float batteryVoltsFromMilliVolts(float adcMilliVolts) {
  return adcMilliVolts / 1000.0f * cfg::VBAT_DIVIDER * cfg::VBAT_CAL;
}

// State the motion code uses. Without the divider fitted the reading is noise,
// so report Limited (drive allowed, visibly flagged) instead of the measurement.
inline BatteryState batteryGate(BatteryState measured, bool senseFitted) {
  return senseFitted ? measured : BatteryState::Limited;
}

class BatteryMonitor {
 public:
  // Add one raw sample (volts). Returns the state after this sample.
  // Starts in Stop and stays there until the 1 s window is full, so the robot
  // refuses to drive on USB power only or before the first readings.
  BatteryState update(float volts) {
    if (!(volts >= 0.0f) || isinf(volts)) volts = 0.0f;  // NaN, negative, inf: treat as dead
    buf_[head_] = volts;
    head_ = (head_ + 1) % cfg::BATT_WINDOW;
    if (count_ < cfg::BATT_WINDOW) count_++;
    if (count_ < cfg::BATT_WINDOW) return state_ = BatteryState::Stop;

    float sum = 0;
    for (uint32_t i = 0; i < cfg::BATT_WINDOW; i++) sum += buf_[i];
    avg_ = sum / cfg::BATT_WINDOW;

    const float up = cfg::BATT_LIMIT_V + cfg::BATT_HYST_V;
    switch (state_) {
      case BatteryState::Ok:
        if (avg_ < cfg::BATT_STOP_V) state_ = BatteryState::Stop;
        else if (avg_ < cfg::BATT_LIMIT_V) state_ = BatteryState::Limited;
        break;
      case BatteryState::Limited:
        if (avg_ < cfg::BATT_STOP_V) state_ = BatteryState::Stop;
        else if (avg_ >= up) state_ = BatteryState::Ok;
        break;
      case BatteryState::Stop:
        if (avg_ >= up) state_ = BatteryState::Ok;
        else if (avg_ >= cfg::BATT_STOP_V + cfg::BATT_HYST_V) state_ = BatteryState::Limited;
        break;
    }
    return state_;
  }
  BatteryState state() const { return state_; }
  float average() const { return avg_; }

 private:
  float buf_[cfg::BATT_WINDOW] = {};
  uint32_t head_ = 0, count_ = 0;
  float avg_ = 0;
  BatteryState state_ = BatteryState::Stop;
};

}  // namespace tt
