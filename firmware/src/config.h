// The ONLY place for pins, PCA_ADDR, TEST_SPEED, TEST_MS and the motor table.
// Mirror every change in docs/hardware.md in the same commit.
// Pure constants: no Arduino includes, so host tests can include this file.
#pragma once
#include <stdint.h>

namespace cfg {

// ---- Board pins (docs/hardware.md) ------------------------------------------
constexpr uint8_t PIN_POWER_HOLD = 4;  // G4: must stay HIGH to keep the stick powered
constexpr uint8_t PIN_I2C_SDA = 32;    // Grove SDA
constexpr uint8_t PIN_I2C_SCL = 33;    // Grove SCL
constexpr uint32_t I2C_HZ = 400000;
constexpr uint32_t SERIAL_BAUD = 115200;

// ---- PCA9685 -----------------------------------------------------------------
constexpr uint8_t PCA_ADDR = 0x40;
constexpr uint16_t PCA_PWM_HZ = 1000;  // 1 kHz
constexpr uint16_t PCA_MAX_TICKS = 4095;

// ---- Motor model -------------------------------------------------------------
constexpr float D0 = 0.25f;         // deadband: duty below this does not move the motor
constexpr float SPEED_CAP = 0.40f;  // 40 percent cap on the commanded speed

// ---- Bring-up test run -------------------------------------------------------
constexpr float TEST_SPEED = 0.40f;  // must be <= SPEED_CAP (checked below)
constexpr uint32_t TEST_MS = 1000;   // every run auto-stops after this long

// ---- Motor table: PCA9685 channels and direction sign ------------------------
// Index 0..3 = FL, FR, RL, RR. Channels per docs/hardware.md.
// Only the REAR motors (2, 3) are wired at the moment: one L298N on ch6..ch11.
// Channel numbers were read from the board layout, not printed labels: if a motor
// does not move or the wrong one moves, fix the numbers HERE, never in code.
struct Motor {
  const char* name;
  uint8_t pwmCh;  // L298N ENA / ENB
  uint8_t in1Ch;  // IN1 / IN3
  uint8_t in2Ch;  // IN2 / IN4
  int8_t sign;    // +1 or -1: flips this motor's direction
};
constexpr uint8_t MOTOR_COUNT = 4;
constexpr Motor MOTORS[MOTOR_COUNT] = {
    {"FL", 0, 1, 2, +1},
    {"FR", 3, 4, 5, -1},
    {"RL", 6, 7, 8, +1},
    {"RR", 9, 10, 11, -1},
};

static_assert(TEST_SPEED > 0.0f && TEST_SPEED <= SPEED_CAP, "TEST_SPEED must be within the speed cap");
static_assert(SPEED_CAP <= 0.40f, "the bring-up cap must stay at 40 percent");
static_assert(TEST_MS > 0 && TEST_MS <= 1000, "bring-up runs must auto-stop within 1 s");

}  // namespace cfg
