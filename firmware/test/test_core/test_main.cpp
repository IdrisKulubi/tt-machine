// Host-side tests: pio test -e native
#include <unity.h>

#include <string.h>

#include "battery.h"
#include "command.h"
#include "kinematics.h"
#include "motion.h"
#include "tt_config.h"

using namespace tt;

void setUp() {}
void tearDown() {}

static bool parse(const char* s, Command& c) { return parseCommand(s, strlen(s), c); }

// ---------------- command parser (FR-1.3) ----------------
void test_clamp_unit() {
  TEST_ASSERT_EQUAL_FLOAT(0.5f, clampUnit(0.5f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, clampUnit(7.0f));
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, clampUnit(-7.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, clampUnit(NAN));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, clampUnit(INFINITY));
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, clampUnit(-INFINITY));
}

void test_parse_valid() {
  Command c;
  TEST_ASSERT_TRUE(parse("{\"vx\":0.5,\"vy\":-0.25,\"w\":0.1,\"hold\":true}", c));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, c.vx);
  TEST_ASSERT_EQUAL_FLOAT(-0.25f, c.vy);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, c.w);
  TEST_ASSERT_TRUE(c.hold);
}

void test_parse_clamps_out_of_range() {
  Command c;
  TEST_ASSERT_TRUE(parse("{\"vx\":99,\"vy\":-1e30,\"w\":1.0001}", c));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.vx);
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, c.vy);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.w);
}

void test_parse_missing_fields_default_zero() {
  Command c;
  TEST_ASSERT_TRUE(parse("{\"vx\":0.3}", c));
  TEST_ASSERT_EQUAL_FLOAT(0.3f, c.vx);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.vy);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.w);
  TEST_ASSERT_FALSE(c.hold);
}

void test_parse_rejects_malformed() {
  Command c;
  c.vx = 0.7f;
  TEST_ASSERT_FALSE(parse("", c));
  TEST_ASSERT_FALSE(parse("not json", c));
  TEST_ASSERT_FALSE(parse("{\"vx\":", c));
  TEST_ASSERT_FALSE(parse("[1,2,3]", c));
  TEST_ASSERT_FALSE(parse("42", c));
  TEST_ASSERT_FALSE(parse("{\"vx\":\"fast\"}", c));
  TEST_ASSERT_FALSE(parse("{\"vx\":0.1,\"hold\":\"yes\"}", c));
  TEST_ASSERT_FALSE(parse("{\"vx\":null,\"w\":{}}", c));
  TEST_ASSERT_EQUAL_FLOAT(0.7f, c.vx);  // untouched on failure
  TEST_ASSERT_FALSE(parseCommand(nullptr, 5, c));
}

void test_parse_rejects_oversize() {
  char big[400];
  memset(big, ' ', sizeof big);
  memcpy(big, "{\"vx\":0.1}", 10);
  Command c;
  TEST_ASSERT_FALSE(parseCommand(big, sizeof big, c));
}

// ---------------- kinematics (FR-1.4) ----------------
void test_ik_forward() {
  WheelSpeeds s = inverseKinematics(0.3f, 0, 0);
  for (int i = 0; i < 4; i++) TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.3f / cfg::WHEEL_R, s.w[i]);
}

void test_ik_strafe_left_pattern() {
  WheelSpeeds s = inverseKinematics(0, 0.3f, 0);
  float a = 0.3f / cfg::WHEEL_R;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -a, s.w[0]);  // FL -
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, a, s.w[1]);   // FR +
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, a, s.w[2]);   // RL +
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -a, s.w[3]);  // RR -
}

void test_ik_rotate_ccw_pattern() {
  WheelSpeeds s = inverseKinematics(0, 0, 1.0f);
  float a = cfg::LXY / cfg::WHEEL_R;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -a, s.w[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, a, s.w[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -a, s.w[2]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, a, s.w[3]);
}

void test_ik_scaling_preserves_direction_and_limit() {
  // Far beyond the limit: mixed forward + strafe + rotate.
  WheelSpeeds raw = {{0, 0, 0, 0}};
  WheelSpeeds s = inverseKinematics(2.0f, 1.5f, 6.0f);
  float m = 0;
  for (int i = 0; i < 4; i++) m = fmaxf(m, fabsf(s.w[i]));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, cfg::WMAX, m);
  // Direction check: ratios between wheels equal the unscaled ratios.
  const float k = cfg::LXY * 6.0f;
  raw.w[0] = (2.0f - 1.5f - k) / cfg::WHEEL_R;
  raw.w[1] = (2.0f + 1.5f + k) / cfg::WHEEL_R;
  const float f = s.w[1] / raw.w[1];
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, raw.w[0] * f, s.w[0]);
  TEST_ASSERT_TRUE(f < 1.0f && f > 0.0f);
}

void test_ik_within_limit_untouched() {
  WheelSpeeds s = inverseKinematics(0.1f, 0.1f, 0.1f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, (0.1f - 0.1f - cfg::LXY * 0.1f) / cfg::WHEEL_R, s.w[0]);
}

// ---------------- PWM shaping (FR-1.10) ----------------
void test_shape_zero_and_tiny() {
  TEST_ASSERT_EQUAL_UINT16(0, shapeWheel(0, 1).duty);
  TEST_ASSERT_EQUAL_UINT16(0, shapeWheel(0.1f, 1).duty);
  TEST_ASSERT_EQUAL_UINT16(0, shapeWheel(NAN, 1).duty);
}

void test_shape_above_deadband_and_max() {
  MotorDrive lo = shapeWheel(cfg::WMAX * 0.05f, 1);
  TEST_ASSERT_TRUE(lo.duty > cfg::D0 * 4095.0f);
  TEST_ASSERT_EQUAL_UINT16(4095, shapeWheel(cfg::WMAX, 1).duty);
  TEST_ASSERT_EQUAL_UINT16(4095, shapeWheel(cfg::WMAX * 5, 1).duty);
}

void test_shape_direction_and_sign() {
  TEST_ASSERT_TRUE(shapeWheel(5, +1).forward);
  TEST_ASSERT_FALSE(shapeWheel(5, -1).forward);
  TEST_ASSERT_FALSE(shapeWheel(-5, +1).forward);
  TEST_ASSERT_TRUE(shapeWheel(-5, -1).forward);
}

void test_default_speed_limit_is_above_deadband() {
  // SR-4: the default 40 percent limit must still move the robot.
  TEST_ASSERT_EQUAL_FLOAT(0.4f, cfg::speedLimit());
  Motion m;
  MotionInput in;
  in.hasCmd = true; in.nowMs = 100; in.lastCmdMs = 100;
  in.batt = BatteryState::Ok;
  in.cmd.vx = 1.0f;
  MotionOutput out;
  for (int i = 0; i < 400; i++) out = m.step(in, 0.005f);  // let the ramp finish
  MotorDrive d = shapeWheel(out.wheels.w[0], 1);
  TEST_ASSERT_TRUE(d.duty > cfg::D0 * 4095.0f);
  TEST_ASSERT_TRUE(d.duty < 4095);
}

void test_slew_limiter() {
  SlewLimiter l;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.004f, l.step(1.0f, 0.8f, 0.005f));
  for (int i = 0; i < 1000; i++) l.step(0.3f, 0.8f, 0.005f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.3f, l.value);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.296f, l.step(0.0f, 0.8f, 0.005f));
  l.reset();
  TEST_ASSERT_EQUAL_FLOAT(0.0f, l.value);
}

// ---------------- watchdog (SR-1, FR-1.2) ----------------
void test_watchdog() {
  TEST_ASSERT_FALSE(watchdogAlive(false, 1000, 1000));  // never got a command
  TEST_ASSERT_TRUE(watchdogAlive(true, 1000, 1000));
  TEST_ASSERT_TRUE(watchdogAlive(true, 1299, 1000));
  TEST_ASSERT_FALSE(watchdogAlive(true, 1300, 1000));   // exactly 300 ms: stopped
  TEST_ASSERT_FALSE(watchdogAlive(true, 5000, 1000));
}

void test_watchdog_millis_wrap() {
  uint32_t last = 0xFFFFFF00u;
  TEST_ASSERT_TRUE(watchdogAlive(true, last + 100, last));
  TEST_ASSERT_FALSE(watchdogAlive(true, last + 300, last));  // wraps past zero
  TEST_ASSERT_TRUE(watchdogAlive(true, 10, 20));             // stamped slightly in the future
}

// ---------------- battery (FR-1.8) ----------------
static BatteryState feed(BatteryMonitor& b, float v, int n) {
  BatteryState s = b.state();
  for (int i = 0; i < n; i++) s = b.update(v);
  return s;
}

void test_battery_stops_until_window_full() {
  BatteryMonitor b;
  TEST_ASSERT_EQUAL((int)BatteryState::Stop, (int)b.state());
  for (uint32_t i = 0; i < cfg::BATT_WINDOW - 1; i++)
    TEST_ASSERT_EQUAL((int)BatteryState::Stop, (int)b.update(8.0f));
  TEST_ASSERT_EQUAL((int)BatteryState::Ok, (int)b.update(8.0f));
}

void test_battery_usb_only_refuses() {
  BatteryMonitor b;
  TEST_ASSERT_EQUAL((int)BatteryState::Stop, (int)feed(b, 0.0f, 200));
  TEST_ASSERT_EQUAL((int)BatteryState::Stop, (int)feed(b, NAN, 200));
}

void test_battery_thresholds_and_hysteresis() {
  BatteryMonitor b;
  TEST_ASSERT_EQUAL((int)BatteryState::Ok, (int)feed(b, 8.0f, 100));
  TEST_ASSERT_EQUAL((int)BatteryState::Limited, (int)feed(b, 6.5f, 100));
  TEST_ASSERT_EQUAL((int)BatteryState::Limited, (int)feed(b, 6.7f, 100));  // inside hysteresis band
  TEST_ASSERT_EQUAL((int)BatteryState::Ok, (int)feed(b, 6.85f, 100));
  TEST_ASSERT_EQUAL((int)BatteryState::Stop, (int)feed(b, 6.1f, 100));
  TEST_ASSERT_EQUAL((int)BatteryState::Stop, (int)feed(b, 6.3f, 100));     // below stop + 0.2
  TEST_ASSERT_EQUAL((int)BatteryState::Limited, (int)feed(b, 6.45f, 100));
}

void test_battery_sag_spike_is_averaged() {
  BatteryMonitor b;
  feed(b, 7.5f, 100);
  // A 100 ms sag to 5 V (5 samples of 50) must not trip the stop.
  TEST_ASSERT_EQUAL((int)BatteryState::Ok, (int)feed(b, 5.0f, 5));
}

void test_battery_adc_conversion() {
  // 8.4 V pack -> about 2.08 V at the pin.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 8.4f, batteryVoltsFromMilliVolts(2084.0f));
}

// ---------------- heading hold (FR-1.5) ----------------
void test_wrap_angle() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -3.0f, wrapAngle(3.2831853f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, wrapAngle(-3.2831853f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, wrapAngle(NAN));
}

void test_heading_hold() {
  HeadingHold h;
  h.ref = 0.0f;
  // Drifted CW by 0.1 rad (yaw negative): command CCW.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, cfg::HOLD_KP * 0.1f, h.apply(true, 0.0f, -0.1f));
  // Big error is capped.
  TEST_ASSERT_EQUAL_FLOAT(-cfg::HOLD_W_MAX, h.apply(true, 0.0f, 2.0f));
  // Rotation commanded: pass through and re-reference.
  TEST_ASSERT_EQUAL_FLOAT(0.5f, h.apply(true, 0.5f, 1.0f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, h.ref);
  // Hold off: pass through.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, h.apply(false, 0.0f, 3.0f));
}

// ---------------- motion controller (safety paths) ----------------
static MotionInput liveInput(uint32_t now) {
  MotionInput in;
  in.hasCmd = true;
  in.nowMs = now;
  in.lastCmdMs = now;
  in.batt = BatteryState::Ok;
  in.cmd.vx = 1.0f;
  return in;
}

static float maxAbs(const MotionOutput& o) {
  float m = 0;
  for (int i = 0; i < 4; i++) m = fmaxf(m, fabsf(o.wheels.w[i]));
  return m;
}

void test_motion_no_command_means_stop() {
  Motion m;
  MotionInput in;
  in.batt = BatteryState::Ok;
  MotionOutput o = m.step(in, 0.005f);
  TEST_ASSERT_FALSE(o.driving);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, maxAbs(o));
  TEST_ASSERT_EQUAL((int)StopReason::NoCommand, (int)o.reason);
  // Default input (battery unknown) also refuses to drive.
  MotionInput fresh;
  fresh.hasCmd = true;
  TEST_ASSERT_FALSE(m.step(fresh, 0.005f).driving);
}

void test_motion_watchdog_stops_without_ramp() {
  Motion m;
  MotionInput in = liveInput(1000);
  MotionOutput o;
  for (int i = 0; i < 200; i++) { in.nowMs = 1000 + i * 5; in.lastCmdMs = in.nowMs; o = m.step(in, 0.005f); }
  TEST_ASSERT_TRUE(o.driving);
  TEST_ASSERT_TRUE(maxAbs(o) > 1.0f);
  // Commands stop; last one was at lastCmdMs. 300 ms later: zero immediately.
  in.nowMs = in.lastCmdMs + 300;
  o = m.step(in, 0.005f);
  TEST_ASSERT_FALSE(o.driving);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, maxAbs(o));
  TEST_ASSERT_EQUAL((int)StopReason::NoCommand, (int)o.reason);
}

void test_motion_restart_after_failsafe_ramps_from_rest() {
  Motion m;
  MotionInput in = liveInput(1000);
  for (int i = 0; i < 200; i++) { in.nowMs = 1000 + i * 5; in.lastCmdMs = in.nowMs; m.step(in, 0.005f); }
  in.nowMs += 1000;  // link lost
  m.step(in, 0.005f);
  in.lastCmdMs = in.nowMs;  // link back
  MotionOutput o = m.step(in, 0.005f);
  TEST_ASSERT_TRUE(o.driving);
  TEST_ASSERT_TRUE(maxAbs(o) < 0.5f);  // first tick after restart is a tiny step, not the old speed
}

void test_motion_estop_and_battery_stop() {
  Motion m;
  MotionInput in = liveInput(1000);
  in.estop = true;
  MotionOutput o = m.step(in, 0.005f);
  TEST_ASSERT_FALSE(o.driving);
  TEST_ASSERT_EQUAL((int)StopReason::EStop, (int)o.reason);
  in.estop = false;
  in.batt = BatteryState::Stop;
  o = m.step(in, 0.005f);
  TEST_ASSERT_FALSE(o.driving);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, maxAbs(o));
  TEST_ASSERT_EQUAL((int)StopReason::BatteryLow, (int)o.reason);
}

void test_motion_default_speed_cap() {
  Motion m;
  MotionInput in = liveInput(1000);
  MotionOutput o;
  for (int i = 0; i < 1000; i++) o = m.step(in, 0.005f);
  const float expect = cfg::VX_MAX * cfg::speedLimit() / cfg::WHEEL_R;
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, expect, maxAbs(o));
  TEST_ASSERT_TRUE(maxAbs(o) < cfg::WMAX);
}

void test_motion_command_is_clamped_again() {
  Motion m;
  MotionInput in = liveInput(1000);
  in.cmd.vx = 50.0f;  // should never happen after parsing, but the controller must not trust it
  MotionOutput o;
  for (int i = 0; i < 1000; i++) o = m.step(in, 0.005f);
  const float expect = cfg::VX_MAX * cfg::speedLimit() / cfg::WHEEL_R;
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, expect, maxAbs(o));
}

void test_motion_acceleration_limited() {
  Motion m;
  MotionInput in = liveInput(1000);
  MotionOutput o = m.step(in, 0.005f);
  // One 5 ms tick at 0.8 m/s^2 = 0.004 m/s.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.004f / cfg::WHEEL_R, maxAbs(o));
}

void test_motion_heading_hold_corrects() {
  Motion m;
  MotionInput in;
  in.hasCmd = true; in.nowMs = 1000; in.lastCmdMs = 1000;
  in.batt = BatteryState::Ok;
  in.cmd.hold = true;
  in.yawRad = 0.0f;
  m.step(in, 0.005f);  // establishes reference 0
  in.yawRad = -0.1f;   // drifted clockwise
  MotionOutput o = m.step(in, 0.005f);
  // CCW correction: FL, RL negative; FR, RR positive.
  TEST_ASSERT_TRUE(o.wheels.w[0] < 0 && o.wheels.w[2] < 0);
  TEST_ASSERT_TRUE(o.wheels.w[1] > 0 && o.wheels.w[3] > 0);
}

void test_motion_battery_limited_caps_speed() {
  // Only meaningful when full speed is enabled; at the 40 percent default the cap is already lower.
  TEST_ASSERT_TRUE(effectiveSpeedLimit(BatteryState::Limited) <= cfg::BATT_LIMITED_SPEED);
  TEST_ASSERT_TRUE(effectiveSpeedLimit(BatteryState::Limited) <= effectiveSpeedLimit(BatteryState::Ok));
}

// ---------------- motor wiring (docs/hardware.md, tested on robot) ----------------
void test_config_motor_map_matches_robot() {
  const uint8_t expect[4][3] = {{0, 1, 2}, {5, 3, 4}, {6, 7, 8}, {11, 9, 12}};
  const int8_t signs[4] = {+1, -1, +1, -1};
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_UINT8(expect[i][0], cfg::MOTORS[i].pwmCh);
    TEST_ASSERT_EQUAL_UINT8(expect[i][1], cfg::MOTORS[i].in1Ch);
    TEST_ASSERT_EQUAL_UINT8(expect[i][2], cfg::MOTORS[i].in2Ch);
    TEST_ASSERT_EQUAL_INT8(signs[i], cfg::MOTORS[i].sign);
    TEST_ASSERT_NOT_EQUAL(10, cfg::MOTORS[i].in2Ch);  // ch10 signal path is dead
  }
}

void test_battery_gate() {
  // Fitted: the measured state passes through unchanged (low still refuses).
  TEST_ASSERT_EQUAL(BatteryState::Stop, batteryGate(BatteryState::Stop, true));
  TEST_ASSERT_EQUAL(BatteryState::Ok, batteryGate(BatteryState::Ok, true));
  // Not fitted: never Ok (stays visibly flagged), never trusts the noise.
  TEST_ASSERT_EQUAL(BatteryState::Limited, batteryGate(BatteryState::Stop, false));
  TEST_ASSERT_EQUAL(BatteryState::Limited, batteryGate(BatteryState::Ok, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_clamp_unit);
  RUN_TEST(test_parse_valid);
  RUN_TEST(test_parse_clamps_out_of_range);
  RUN_TEST(test_parse_missing_fields_default_zero);
  RUN_TEST(test_parse_rejects_malformed);
  RUN_TEST(test_parse_rejects_oversize);
  RUN_TEST(test_ik_forward);
  RUN_TEST(test_ik_strafe_left_pattern);
  RUN_TEST(test_ik_rotate_ccw_pattern);
  RUN_TEST(test_ik_scaling_preserves_direction_and_limit);
  RUN_TEST(test_ik_within_limit_untouched);
  RUN_TEST(test_shape_zero_and_tiny);
  RUN_TEST(test_shape_above_deadband_and_max);
  RUN_TEST(test_shape_direction_and_sign);
  RUN_TEST(test_default_speed_limit_is_above_deadband);
  RUN_TEST(test_slew_limiter);
  RUN_TEST(test_watchdog);
  RUN_TEST(test_watchdog_millis_wrap);
  RUN_TEST(test_battery_stops_until_window_full);
  RUN_TEST(test_battery_usb_only_refuses);
  RUN_TEST(test_battery_thresholds_and_hysteresis);
  RUN_TEST(test_battery_sag_spike_is_averaged);
  RUN_TEST(test_battery_adc_conversion);
  RUN_TEST(test_wrap_angle);
  RUN_TEST(test_heading_hold);
  RUN_TEST(test_motion_no_command_means_stop);
  RUN_TEST(test_motion_watchdog_stops_without_ramp);
  RUN_TEST(test_motion_restart_after_failsafe_ramps_from_rest);
  RUN_TEST(test_motion_estop_and_battery_stop);
  RUN_TEST(test_motion_default_speed_cap);
  RUN_TEST(test_motion_command_is_clamped_again);
  RUN_TEST(test_motion_acceleration_limited);
  RUN_TEST(test_motion_heading_hold_corrects);
  RUN_TEST(test_motion_battery_limited_caps_speed);
  RUN_TEST(test_config_motor_map_matches_robot);
  RUN_TEST(test_battery_gate);
  return UNITY_END();
}
