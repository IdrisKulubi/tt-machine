// Pure bring-up logic: speed cap, deadband, direction pins, and the serial
// command state machine. No Arduino calls, so it is unit tested on the host.
#pragma once
#include <math.h>
#include <stdint.h>

#include "config.h"

namespace bu {

// ---- Motor maths -------------------------------------------------------------
inline float clampUnit(float v) {
  if (isnan(v)) return 0.0f;
  if (v > 1.0f) return 1.0f;
  if (v < -1.0f) return -1.0f;
  return v;
}

// Clamp to [-1, 1], then limit the magnitude to the 40 percent cap. NaN becomes 0.
inline float capSpeed(float s) {
  s = clampUnit(s);
  if (s > cfg::SPEED_CAP) s = cfg::SPEED_CAP;
  if (s < -cfg::SPEED_CAP) s = -cfg::SPEED_CAP;
  return s;
}

struct MotorOut {
  uint16_t ticks;  // ENA/ENB PWM, 0..4095
  bool in1;
  bool in2;
};

// speed in [-1, 1] (capped here), sign is the motor's direction constant.
// Deadband: any non-zero speed maps to duty = D0 + (1 - D0) * |speed|, so it is above D0.
// Zero speed: ENA off and both direction pins low (stopped).
inline MotorOut motorOutput(float speed, int8_t sign) {
  const float s = capSpeed(speed);
  if (s == 0.0f) return {0, false, false};
  const float duty = cfg::D0 + (1.0f - cfg::D0) * fabsf(s);
  const float t = duty * cfg::PCA_MAX_TICKS;
  const uint16_t ticks = t >= cfg::PCA_MAX_TICKS ? cfg::PCA_MAX_TICKS : static_cast<uint16_t>(t + 0.5f);
  const bool forward = (s * sign) > 0.0f;
  return {ticks, forward, !forward};
}

// ---- Serial command state machine -------------------------------------------
// Keys: s scan, 0..3 select motor, f forward, r reverse, x stop, ? help.
// Rules: one motor at a time, every run auto-stops after TEST_MS, and select or
// scan stop a running motor first.
enum class Result : uint8_t {
  None,         // nothing to do (unknown key, whitespace)
  Selected,     // motor selected (outputs must be stopped)
  Started,      // run started: drive the selected motor at speed()
  Stopped,      // x pressed (outputs must be stopped)
  AutoStopped,  // TEST_MS elapsed (outputs must be stopped)
  NeedSelect,   // f/r with no motor selected
  HwMissing,    // f/r while the PCA9685 is not found
  Scan,         // run an I2C scan (outputs must be stopped)
  Help,         // print help
  BadMotor,     // digit outside the motor table
};

class Bringup {
 public:
  Result onKey(char c, uint32_t nowMs, bool hwOk) {
    switch (c) {
      case 'x':
      case 'X':
        running_ = false;
        return Result::Stopped;
      case 's':
      case 'S':
        running_ = false;
        return Result::Scan;
      case '?':
      case 'h':
      case 'H':
        return Result::Help;  // does not touch a running motor
      case 'f':
      case 'F':
        return start(+1, nowMs, hwOk);
      case 'r':
      case 'R':
        return start(-1, nowMs, hwOk);
      default:
        break;
    }
    if (c >= '0' && c <= '9') {
      running_ = false;  // always stop before changing the selection
      const int idx = c - '0';
      if (idx >= cfg::MOTOR_COUNT) return Result::BadMotor;
      selected_ = idx;
      return Result::Selected;
    }
    return Result::None;
  }

  // Call every loop. Returns AutoStopped once when the run time is over.
  Result tick(uint32_t nowMs) {
    if (running_ && static_cast<uint32_t>(nowMs - startMs_) >= cfg::TEST_MS) {
      running_ = false;
      return Result::AutoStopped;
    }
    return Result::None;
  }

  bool running() const { return running_; }
  int selected() const { return selected_; }  // -1 if none
  float speed() const { return speed_; }      // signed, already within the cap

 private:
  Result start(int dir, uint32_t nowMs, bool hwOk) {
    if (selected_ < 0) return Result::NeedSelect;
    if (!hwOk) {
      running_ = false;
      return Result::HwMissing;
    }
    speed_ = capSpeed(dir * cfg::TEST_SPEED);
    startMs_ = nowMs;
    running_ = true;  // only ever the single selected motor
    return Result::Started;
  }

  int selected_ = -1;
  bool running_ = false;
  float speed_ = 0;
  uint32_t startMs_ = 0;
};

}  // namespace bu
