// Motion controller: the single validated path from command to wheel speeds.
// Pure logic. Hardware wrappers in src/main.cpp only turn the output into PWM.
#pragma once
#include <math.h>
#include <stdint.h>

#include "battery.h"
#include "command.h"
#include "kinematics.h"
#include "tt_config.h"

namespace tt {

// SR-1 / FR-1.2: alive only if a command has arrived and it is younger than the timeout.
// Handles uint32 millis() wrap and a command stamped slightly "in the future".
inline bool watchdogAlive(bool hasCmd, uint32_t nowMs, uint32_t lastCmdMs,
                          uint32_t timeoutMs = cfg::WATCHDOG_MS) {
  if (!hasCmd) return false;
  const int32_t age = static_cast<int32_t>(nowMs - lastCmdMs);
  if (age < 0) return true;  // stamped after `nowMs` was read: fresh
  return static_cast<uint32_t>(age) < timeoutMs;
}

inline float wrapAngle(float a) {
  constexpr float kPi = 3.14159265358979f;
  if (!(fabsf(a) < 1000.0f)) return 0.0f;  // NaN or absurd: no correction
  while (a > kPi) a -= 2.0f * kPi;
  while (a < -kPi) a += 2.0f * kPi;
  return a;
}

struct HeadingHold {
  float ref = 0;
  // Returns the angular speed to use. While rotation is commanded (or hold is
  // off) the reference follows the current yaw; otherwise it is held.
  float apply(bool hold, float wCmd, float yaw) {
    if (hold && fabsf(wCmd) < 1e-3f) {
      float c = cfg::HOLD_KP * wrapAngle(ref - yaw);
      if (c > cfg::HOLD_W_MAX) c = cfg::HOLD_W_MAX;
      if (c < -cfg::HOLD_W_MAX) c = -cfg::HOLD_W_MAX;
      return c;
    }
    ref = yaw;
    return wCmd;
  }
};

enum class StopReason : uint8_t { None = 0, NoCommand, EStop, BatteryLow };

struct MotionInput {
  Command cmd;
  bool hasCmd = false;
  uint32_t nowMs = 0;
  uint32_t lastCmdMs = 0;
  bool estop = false;
  BatteryState batt = BatteryState::Stop;
  float yawRad = 0;  // CCW positive
};

struct MotionOutput {
  WheelSpeeds wheels = {{0, 0, 0, 0}};  // rad/s, already direction-preserving scaled
  bool driving = false;
  StopReason reason = StopReason::NoCommand;
};

inline float effectiveSpeedLimit(BatteryState b) {
  float f = cfg::speedLimit();
  if (b == BatteryState::Limited && f > cfg::BATT_LIMITED_SPEED) f = cfg::BATT_LIMITED_SPEED;
  return f;
}

class Motion {
 public:
  MotionOutput step(const MotionInput& in, float dt) {
    MotionOutput out;
    StopReason r = StopReason::None;
    if (in.estop) r = StopReason::EStop;
    else if (in.batt == BatteryState::Stop) r = StopReason::BatteryLow;
    else if (!watchdogAlive(in.hasCmd, in.nowMs, in.lastCmdMs)) r = StopReason::NoCommand;

    if (r != StopReason::None) {
      // Fail-safe: zero at once, no ramp. Forget any ramp state so a later
      // command starts from rest.
      lvx_.reset(); lvy_.reset(); lw_.reset();
      hold_.ref = in.yawRad;
      out.reason = r;
      return out;
    }

    const float f = effectiveSpeedLimit(in.batt);
    const float vx = lvx_.step(clampUnit(in.cmd.vx) * cfg::VX_MAX * f, cfg::ACCEL_LIN, dt);
    const float vy = lvy_.step(clampUnit(in.cmd.vy) * cfg::VY_MAX * f, cfg::ACCEL_LIN, dt);
    float w = lw_.step(clampUnit(in.cmd.w) * cfg::WZ_MAX * f, cfg::ACCEL_ANG, dt);
    w = hold_.apply(in.cmd.hold, w, in.yawRad);

    out.wheels = inverseKinematics(vx, vy, w);
    out.driving = true;
    out.reason = StopReason::None;
    return out;
  }

 private:
  SlewLimiter lvx_, lvy_, lw_;
  HeadingHold hold_;
};

}  // namespace tt
