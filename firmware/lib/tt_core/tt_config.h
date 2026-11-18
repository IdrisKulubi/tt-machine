// The ONE firmware config file: geometry, motor signs, limits, channels, thresholds.
// Mirror any change in docs/hardware.md in the same commit.
// Wi-Fi credentials and the hub address are NOT here: see include/secrets.h (git-ignored).
#pragma once
#include <stdint.h>

namespace tt {
namespace cfg {

// ---- Geometry and drive (metres, rad/s) -------------------------------------
constexpr float WHEEL_R = 0.040f;  // 80 mm wheels
constexpr float LXY = 0.176f;      // lx + ly. PLACEHOLDER: measure on the chassis.
constexpr float WMAX = 18.8f;      // wheel speed limit, rad/s
constexpr float D0 = 0.25f;        // motor deadband (fraction of PWM that does not move the motor)
constexpr float MIN_DRIVE_FRACTION = 0.02f;  // below this wheel fraction the motor is off

// ---- Motor wiring: PCA9685 channels and direction signs ---------------------
// Order everywhere: FL, FR, RL, RR. Change signs here, never by rewiring.
// "Straight plug" wiring (tested on the robot 2026-11-18): each L298N header
// ENA IN1 IN2 IN3 IN4 ENB goes to 6 neighbouring channels, front 0..5, rear 6..11,
// except rear IN4 is on ch12 (ch10 signal path is dead). Signs confirmed by test.
struct MotorCfg {
  uint8_t pwmCh, in1Ch, in2Ch;
  int8_t sign;
};
constexpr MotorCfg MOTORS[4] = {
    {0, 1, 2, +1},    // FL
    {5, 3, 4, -1},    // FR
    {6, 7, 8, +1},    // RL
    {11, 9, 12, -1},  // RR (IN4 on ch12, ch10 unused)
};
constexpr uint8_t PCA_ADDR = 0x40;
constexpr uint16_t PCA_PWM_HZ = 1000;

// ---- Command scaling: client values are unitless in [-1, 1] -----------------
constexpr float VX_MAX = 0.75f;  // m/s at command 1.0 and full speed
constexpr float VY_MAX = 0.60f;  // m/s (strafe, roller slip)
constexpr float WZ_MAX = 2.0f;   // rad/s. ASSUMPTION, tune on the robot.

// Speed limit (SR-4). 40 percent by default. Full speed only by editing this flag
// and reflashing. It is deliberately NOT a switch on the phone page.
constexpr bool FULL_SPEED_ENABLED = false;
constexpr float SPEED_LIMIT_DEFAULT = 0.40f;
constexpr float speedLimit() { return FULL_SPEED_ENABLED ? 1.0f : SPEED_LIMIT_DEFAULT; }

// Acceleration limits (FR-1.9). Angular value is an ASSUMPTION.
constexpr float ACCEL_LIN = 0.8f;  // m/s^2
constexpr float ACCEL_ANG = 4.0f;  // rad/s^2

// ---- Safety timing -----------------------------------------------------------
constexpr uint32_t WATCHDOG_MS = 300;
constexpr uint32_t MOTION_PERIOD_MS = 5;  // 200 Hz
constexpr uint32_t TELEMETRY_PERIOD_MS = 50;
constexpr uint32_t MAX_FRAME_BYTES = 256;

// ---- Heading hold (FR-1.5) ---------------------------------------------------
constexpr float HOLD_KP = 2.5f;     // rad/s per rad of yaw error
constexpr float HOLD_W_MAX = 1.0f;  // rad/s correction cap

// ---- Battery (FR-1.8) --------------------------------------------------------
constexpr float BATT_LIMIT_V = 6.6f;  // below: speed capped
constexpr float BATT_STOP_V = 6.2f;   // below: refuse to drive
constexpr float BATT_HYST_V = 0.2f;   // recovery margin, prevents flicker
constexpr float BATT_LIMITED_SPEED = 0.5f;
constexpr uint32_t BATT_SAMPLE_MS = 20;       // 50 Hz
constexpr uint32_t BATT_WINDOW = 50;          // 50 samples = 1 s average
constexpr float VBAT_DIVIDER = (100.0f + 33.0f) / 33.0f;  // 100k / 33k
constexpr float VBAT_CAL = 1.000f;  // calibrate ONCE against a multimeter
constexpr uint8_t VBAT_ADC_PIN = 36;
// BENCH-ONLY, TEMPORARY (owner approved 2026-11-18): the 100k/33k divider on G36
// is not fitted yet, so the reading is meaningless. While false, the battery is
// treated as "limited" instead of measured: NO low-battery stop, only the BMS
// cutoff protects the cells. Wheels-up testing only. Set true once the divider
// is fitted and checked (about 1.8-2.1 V on G36), then remove this note.
constexpr bool BATT_SENSE_FITTED = false;
static_assert(BATT_SENSE_FITTED || !FULL_SPEED_ENABLED, "no full speed without battery sensing");
constexpr uint8_t ESTOP_PIN = 26;   // INPUT_PULLUP, contact closes to GND when pressed

// ---- Grove I2C ---------------------------------------------------------------
constexpr uint8_t I2C_SDA = 32;
constexpr uint8_t I2C_SCL = 33;

}  // namespace cfg
}  // namespace tt
