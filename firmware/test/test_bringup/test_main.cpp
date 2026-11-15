// Host tests for the bring-up logic: pio test -e native
#include <unity.h>

#include <initializer_list>

#include "bringup_logic.h"
#include "config.h"

using namespace bu;

void setUp() {}
void tearDown() {}

static const float kTolerance = 1e-6f;

// ---- clamp and cap ----
void test_clamp_unit() {
  TEST_ASSERT_EQUAL_FLOAT(0.5f, clampUnit(0.5f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, clampUnit(9.0f));
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, clampUnit(-9.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, clampUnit(NAN));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, clampUnit(INFINITY));
}

void test_cap_is_forty_percent() {
  TEST_ASSERT_EQUAL_FLOAT(0.4f, cfg::SPEED_CAP);
  TEST_ASSERT_EQUAL_FLOAT(0.4f, capSpeed(1.0f));
  TEST_ASSERT_EQUAL_FLOAT(-0.4f, capSpeed(-1.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.4f, capSpeed(500.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.2f, capSpeed(0.2f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, capSpeed(NAN));
}

// ---- deadband and PWM ----
void test_zero_speed_is_off() {
  MotorOut o = motorOutput(0.0f, +1);
  TEST_ASSERT_EQUAL_UINT16(0, o.ticks);
  TEST_ASSERT_FALSE(o.in1);
  TEST_ASSERT_FALSE(o.in2);
}

void test_any_nonzero_speed_is_above_deadband() {
  const float deadbandTicks = cfg::D0 * cfg::PCA_MAX_TICKS;
  const float speeds[] = {0.001f, 0.01f, 0.1f, 0.4f, 1.0f};
  for (float s : speeds) TEST_ASSERT_TRUE(motorOutput(s, +1).ticks > deadbandTicks);
}

void test_max_output_stays_under_cap() {
  // Full command is capped to 0.4: duty = 0.25 + 0.75 * 0.4 = 0.55.
  const uint16_t expected = static_cast<uint16_t>((cfg::D0 + (1.0f - cfg::D0) * 0.4f) * cfg::PCA_MAX_TICKS + 0.5f);
  TEST_ASSERT_EQUAL_UINT16(expected, motorOutput(1.0f, +1).ticks);
  TEST_ASSERT_EQUAL_UINT16(expected, motorOutput(-100.0f, +1).ticks);
  TEST_ASSERT_TRUE(motorOutput(1.0f, +1).ticks < cfg::PCA_MAX_TICKS);
}

void test_nan_speed_is_off() {
  MotorOut o = motorOutput(NAN, +1);
  TEST_ASSERT_EQUAL_UINT16(0, o.ticks);
  TEST_ASSERT_FALSE(o.in1);
  TEST_ASSERT_FALSE(o.in2);
}

// ---- direction pins ----
void test_direction_plus_sign() {
  MotorOut f = motorOutput(0.3f, +1);
  TEST_ASSERT_TRUE(f.in1);
  TEST_ASSERT_FALSE(f.in2);
  MotorOut r = motorOutput(-0.3f, +1);
  TEST_ASSERT_FALSE(r.in1);
  TEST_ASSERT_TRUE(r.in2);
}

void test_direction_minus_sign_is_flipped() {
  MotorOut f = motorOutput(0.3f, -1);
  TEST_ASSERT_FALSE(f.in1);
  TEST_ASSERT_TRUE(f.in2);
  MotorOut r = motorOutput(-0.3f, -1);
  TEST_ASSERT_TRUE(r.in1);
  TEST_ASSERT_FALSE(r.in2);
}

void test_direction_pins_never_both_high() {
  const float speeds[] = {-1.0f, -0.4f, -0.01f, 0.0f, 0.01f, 0.4f, 1.0f};
  for (float s : speeds)
    for (int sign : {-1, 1}) {
      MotorOut o = motorOutput(s, static_cast<int8_t>(sign));
      TEST_ASSERT_FALSE(o.in1 && o.in2);
    }
}

// ---- config sanity ----
void test_config_motor_table() {
  TEST_ASSERT_EQUAL_UINT8(4, cfg::MOTOR_COUNT);
  bool used[16] = {};
  for (int i = 0; i < cfg::MOTOR_COUNT; i++) {
    const cfg::Motor& m = cfg::MOTORS[i];
    TEST_ASSERT_TRUE(m.sign == 1 || m.sign == -1);
    for (uint8_t ch : {m.pwmCh, m.in1Ch, m.in2Ch}) {
      TEST_ASSERT_TRUE(ch < 16);
      TEST_ASSERT_FALSE(used[ch]);  // no channel shared between motors
      used[ch] = true;
    }
  }
}

void test_config_rear_motors_match_wiring() {
  // Rear L298N: ch6 ENA, ch7 IN1, ch8 IN2 | ch9 ENB, ch10 IN3, ch11 IN4.
  TEST_ASSERT_EQUAL_UINT8(6, cfg::MOTORS[2].pwmCh);
  TEST_ASSERT_EQUAL_UINT8(7, cfg::MOTORS[2].in1Ch);
  TEST_ASSERT_EQUAL_UINT8(8, cfg::MOTORS[2].in2Ch);
  TEST_ASSERT_EQUAL_UINT8(9, cfg::MOTORS[3].pwmCh);
  TEST_ASSERT_EQUAL_UINT8(10, cfg::MOTORS[3].in1Ch);
  TEST_ASSERT_EQUAL_UINT8(11, cfg::MOTORS[3].in2Ch);
}

void test_config_test_values() {
  TEST_ASSERT_TRUE(cfg::TEST_SPEED <= cfg::SPEED_CAP);
  TEST_ASSERT_EQUAL_UINT32(1000, cfg::TEST_MS);
  TEST_ASSERT_EQUAL_UINT8(0x40, cfg::PCA_ADDR);
}

// ---- command state machine ----
void test_run_needs_selection() {
  Bringup b;
  TEST_ASSERT_EQUAL((int)Result::NeedSelect, (int)b.onKey('f', 100, true));
  TEST_ASSERT_FALSE(b.running());
}

void test_select_then_forward() {
  Bringup b;
  TEST_ASSERT_EQUAL((int)Result::Selected, (int)b.onKey('2', 0, true));
  TEST_ASSERT_EQUAL(2, b.selected());
  TEST_ASSERT_EQUAL((int)Result::Started, (int)b.onKey('f', 10, true));
  TEST_ASSERT_TRUE(b.running());
  TEST_ASSERT_EQUAL_FLOAT(cfg::TEST_SPEED, b.speed());
}

void test_reverse_is_negative() {
  Bringup b;
  b.onKey('3', 0, true);
  TEST_ASSERT_EQUAL((int)Result::Started, (int)b.onKey('r', 0, true));
  TEST_ASSERT_EQUAL_FLOAT(-cfg::TEST_SPEED, b.speed());
}

void test_auto_stop_after_test_ms() {
  Bringup b;
  b.onKey('2', 0, true);
  b.onKey('f', 5000, true);
  TEST_ASSERT_EQUAL((int)Result::None, (int)b.tick(5000));
  TEST_ASSERT_EQUAL((int)Result::None, (int)b.tick(5000 + cfg::TEST_MS - 1));
  TEST_ASSERT_TRUE(b.running());
  TEST_ASSERT_EQUAL((int)Result::AutoStopped, (int)b.tick(5000 + cfg::TEST_MS));
  TEST_ASSERT_FALSE(b.running());
  TEST_ASSERT_EQUAL((int)Result::None, (int)b.tick(99999));  // fires once
}

void test_auto_stop_survives_millis_wrap() {
  Bringup b;
  b.onKey('2', 0, true);
  const uint32_t start = 0xFFFFFF00u;
  b.onKey('f', start, true);
  TEST_ASSERT_EQUAL((int)Result::None, (int)b.tick(start + 200));
  TEST_ASSERT_EQUAL((int)Result::AutoStopped, (int)b.tick(start + cfg::TEST_MS));  // wrapped past 0
}

void test_x_stops() {
  Bringup b;
  b.onKey('2', 0, true);
  b.onKey('f', 0, true);
  TEST_ASSERT_EQUAL((int)Result::Stopped, (int)b.onKey('x', 10, true));
  TEST_ASSERT_FALSE(b.running());
}

void test_select_while_running_stops_first() {
  Bringup b;
  b.onKey('2', 0, true);
  b.onKey('f', 0, true);
  TEST_ASSERT_EQUAL((int)Result::Selected, (int)b.onKey('3', 10, true));
  TEST_ASSERT_FALSE(b.running());  // one motor at a time: nothing keeps running
  TEST_ASSERT_EQUAL(3, b.selected());
}

void test_scan_while_running_stops() {
  Bringup b;
  b.onKey('2', 0, true);
  b.onKey('f', 0, true);
  TEST_ASSERT_EQUAL((int)Result::Scan, (int)b.onKey('s', 10, true));
  TEST_ASSERT_FALSE(b.running());
}

void test_restart_while_running_is_still_one_motor_and_restarts_timer() {
  Bringup b;
  b.onKey('2', 0, true);
  b.onKey('f', 0, true);
  b.onKey('r', 900, true);  // new run: timer restarts, still the same single motor
  TEST_ASSERT_EQUAL(2, b.selected());
  TEST_ASSERT_EQUAL((int)Result::None, (int)b.tick(900 + cfg::TEST_MS - 1));
  TEST_ASSERT_EQUAL((int)Result::AutoStopped, (int)b.tick(900 + cfg::TEST_MS));
}

void test_bad_motor_digit_rejected() {
  Bringup b;
  b.onKey('2', 0, true);
  b.onKey('f', 0, true);
  TEST_ASSERT_EQUAL((int)Result::BadMotor, (int)b.onKey('4', 10, true));
  TEST_ASSERT_FALSE(b.running());
  TEST_ASSERT_EQUAL((int)Result::BadMotor, (int)b.onKey('9', 10, true));
  TEST_ASSERT_EQUAL(2, b.selected());  // selection unchanged
}

void test_unknown_and_whitespace_keys_ignored() {
  Bringup b;
  b.onKey('2', 0, true);
  b.onKey('f', 0, true);
  TEST_ASSERT_EQUAL((int)Result::None, (int)b.onKey('\n', 1, true));
  TEST_ASSERT_EQUAL((int)Result::None, (int)b.onKey('\r', 1, true));
  TEST_ASSERT_EQUAL((int)Result::None, (int)b.onKey('z', 1, true));
  TEST_ASSERT_TRUE(b.running());  // noise does not stop it, auto-stop still will
}

void test_help_does_not_stop_run() {
  Bringup b;
  b.onKey('2', 0, true);
  b.onKey('f', 0, true);
  TEST_ASSERT_EQUAL((int)Result::Help, (int)b.onKey('?', 1, true));
  TEST_ASSERT_TRUE(b.running());
}

void test_hardware_missing_refuses_run() {
  Bringup b;
  b.onKey('2', 0, false);
  TEST_ASSERT_EQUAL((int)Result::HwMissing, (int)b.onKey('f', 0, false));
  TEST_ASSERT_FALSE(b.running());
}

void test_run_speed_always_within_cap() {
  Bringup b;
  b.onKey('0', 0, true);
  b.onKey('f', 0, true);
  TEST_ASSERT_TRUE(fabsf(b.speed()) <= cfg::SPEED_CAP + kTolerance);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_clamp_unit);
  RUN_TEST(test_cap_is_forty_percent);
  RUN_TEST(test_zero_speed_is_off);
  RUN_TEST(test_any_nonzero_speed_is_above_deadband);
  RUN_TEST(test_max_output_stays_under_cap);
  RUN_TEST(test_nan_speed_is_off);
  RUN_TEST(test_direction_plus_sign);
  RUN_TEST(test_direction_minus_sign_is_flipped);
  RUN_TEST(test_direction_pins_never_both_high);
  RUN_TEST(test_config_motor_table);
  RUN_TEST(test_config_rear_motors_match_wiring);
  RUN_TEST(test_config_test_values);
  RUN_TEST(test_run_needs_selection);
  RUN_TEST(test_select_then_forward);
  RUN_TEST(test_reverse_is_negative);
  RUN_TEST(test_auto_stop_after_test_ms);
  RUN_TEST(test_auto_stop_survives_millis_wrap);
  RUN_TEST(test_x_stops);
  RUN_TEST(test_select_while_running_stops_first);
  RUN_TEST(test_scan_while_running_stops);
  RUN_TEST(test_restart_while_running_is_still_one_motor_and_restarts_timer);
  RUN_TEST(test_bad_motor_digit_rejected);
  RUN_TEST(test_unknown_and_whitespace_keys_ignored);
  RUN_TEST(test_help_does_not_stop_run);
  RUN_TEST(test_hardware_missing_refuses_run);
  RUN_TEST(test_run_speed_always_within_cap);
  return UNITY_END();
}
