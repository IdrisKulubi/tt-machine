// Mecanum inverse kinematics, PWM shaping and slew limiting. Pure functions.
// Order FL, FR, RL, RR. +vx forward, +vy left, +wz counter-clockwise.
#pragma once
#include <math.h>
#include <stdint.h>

#include "tt_config.h"

namespace tt {

struct WheelSpeeds {
  float w[4];  // rad/s
};

// FR-1.4: if any wheel would exceed WMAX, scale all four by the same factor
// so the direction of travel is preserved.
inline WheelSpeeds inverseKinematics(float vx, float vy, float wz) {
  const float k = cfg::LXY * wz;
  WheelSpeeds s;
  s.w[0] = (vx - vy - k) / cfg::WHEEL_R;
  s.w[1] = (vx + vy + k) / cfg::WHEEL_R;
  s.w[2] = (vx + vy - k) / cfg::WHEEL_R;
  s.w[3] = (vx - vy + k) / cfg::WHEEL_R;
  float m = 0;
  for (int i = 0; i < 4; i++) m = fmaxf(m, fabsf(s.w[i]));
  if (m > cfg::WMAX) {
    const float f = cfg::WMAX / m;
    for (int i = 0; i < 4; i++) s.w[i] *= f;
  }
  return s;
}

struct MotorDrive {
  uint16_t duty;  // 0..4095 (PCA9685 12-bit)
  bool forward;
};

// FR-1.10: duty = D0 + (1 - D0) * |w| / WMAX, so any non-trivial command is
// above the motor deadband. `sign` is the per-motor direction constant.
inline MotorDrive shapeWheel(float w, int8_t sign) {
  float u = fabsf(w) / cfg::WMAX;
  if (!(u >= cfg::MIN_DRIVE_FRACTION)) return {0, true};  // also catches NaN
  if (u > 1.0f) u = 1.0f;
  float d = (cfg::D0 + (1.0f - cfg::D0) * u) * 4095.0f;
  uint16_t duty = d >= 4095.0f ? 4095 : static_cast<uint16_t>(d + 0.5f);
  return {duty, (w * sign) >= 0};
}

// Moves `value` toward `target` by at most maxRate * dt.
struct SlewLimiter {
  float value = 0;
  float step(float target, float maxRate, float dt) {
    const float d = maxRate * dt;
    const float diff = target - value;
    if (diff > d) value += d;
    else if (diff < -d) value -= d;
    else value = target;
    return value;
  }
  void reset() { value = 0; }
};

}  // namespace tt
