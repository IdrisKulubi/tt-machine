// TT-Coach motor bring-up sketch (task 001). USB serial only: no Wi-Fi, no network.
// Runs ONE motor at a time at a capped speed and always auto-stops after TEST_MS.
// All decisions live in bringup_logic.h (unit tested); this file only touches hardware.
#include <Adafruit_PWMServoDriver.h>
#include <Arduino.h>
#include <Wire.h>

#include "bringup_logic.h"
#include "config.h"

static Adafruit_PWMServoDriver pca(cfg::PCA_ADDR, Wire);
static bu::Bringup state;
static bool pcaOk = false;

static void setChannelOff(uint8_t ch) { pca.setPWM(ch, 0, 4096); }  // full OFF
static void setChannelOn(uint8_t ch) { pca.setPWM(ch, 4096, 0); }   // full ON

// Zero ALL motors, not only the selected one.
static void stopAll() {
  if (!pcaOk) return;
  for (uint8_t i = 0; i < cfg::MOTOR_COUNT; i++) {
    setChannelOff(cfg::MOTORS[i].pwmCh);
    setChannelOff(cfg::MOTORS[i].in1Ch);
    setChannelOff(cfg::MOTORS[i].in2Ch);
  }
}

static void drive(int idx, float speed) {
  const cfg::Motor& m = cfg::MOTORS[idx];
  const bu::MotorOut o = bu::motorOutput(speed, m.sign);
  // Direction first, enable last, so the motor never starts in an undefined direction.
  if (o.in1) setChannelOn(m.in1Ch); else setChannelOff(m.in1Ch);
  if (o.in2) setChannelOn(m.in2Ch); else setChannelOff(m.in2Ch);
  if (o.ticks == 0) setChannelOff(m.pwmCh); else pca.setPWM(m.pwmCh, 0, o.ticks);
}

static void printHelp() {
  Serial.println(F("--- TT-Coach motor bring-up (USB only) ---"));
  Serial.println(F("s     scan I2C"));
  Serial.println(F("0..3  select motor (0 FL, 1 FR, 2 RL, 3 RR)"));
  Serial.println(F("f / r run selected motor forward / reverse, auto-stops"));
  Serial.println(F("x     stop"));
  Serial.println(F("?     this help"));
  Serial.printf("cap %.0f%%, test speed %.2f, run %u ms, one motor at a time\n", cfg::SPEED_CAP * 100,
                cfg::TEST_SPEED, (unsigned)cfg::TEST_MS);
  Serial.printf("selected: %s\n", state.selected() < 0 ? "none" : cfg::MOTORS[state.selected()].name);
}

static bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void scanI2c() {
  Serial.println(F("I2C scan:"));
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    if (probe(a)) {
      Serial.printf("  0x%02X%s\n", a, a == cfg::PCA_ADDR ? "  <- PCA9685" : "");
      found++;
    }
  }
  if (found == 0) Serial.println(F("  nothing found: check SDA/SCL and PCA9685 power (3V3)"));
}

void setup() {
  pinMode(cfg::PIN_POWER_HOLD, OUTPUT);
  digitalWrite(cfg::PIN_POWER_HOLD, HIGH);  // keep the stick powered
  Serial.begin(cfg::SERIAL_BAUD);
  Wire.begin(cfg::PIN_I2C_SDA, cfg::PIN_I2C_SCL, cfg::I2C_HZ);

  pcaOk = probe(cfg::PCA_ADDR);
  if (pcaOk) {
    pca.begin();
    pca.setPWMFreq(cfg::PCA_PWM_HZ);
    stopAll();  // nothing runs until the first command
  }
  delay(300);  // setup only, not in the run path
  Serial.println();
  Serial.println(pcaOk ? F("PCA9685 found, all outputs off.")
                       : F("PCA9685 NOT FOUND: run is disabled. Press s to scan."));
  printHelp();
}

void loop() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    switch (state.onKey(c, millis(), pcaOk)) {
      case bu::Result::Selected:
        stopAll();
        Serial.printf("selected %s (%d). Press f or r.\n", cfg::MOTORS[state.selected()].name,
                      state.selected());
        break;
      case bu::Result::Started:
        stopAll();  // guarantee only one motor is ever live
        drive(state.selected(), state.speed());
        Serial.printf("%s %s at %.2f for %u ms\n", cfg::MOTORS[state.selected()].name,
                      state.speed() > 0 ? "FORWARD" : "REVERSE", fabsf(state.speed()),
                      (unsigned)cfg::TEST_MS);
        break;
      case bu::Result::Stopped:
        stopAll();
        Serial.println(F("stopped"));
        break;
      case bu::Result::Scan:
        stopAll();
        scanI2c();
        break;
      case bu::Result::Help:
        printHelp();
        break;
      case bu::Result::NeedSelect:
        Serial.println(F("select a motor first (0..3)"));
        break;
      case bu::Result::HwMissing:
        stopAll();
        Serial.println(F("PCA9685 not found: refusing to run"));
        break;
      case bu::Result::BadMotor:
        stopAll();
        Serial.println(F("no such motor (0..3)"));
        break;
      default:
        break;
    }
  }

  if (state.tick(millis()) == bu::Result::AutoStopped) {
    stopAll();
    Serial.println(F("auto-stop"));
  }
}
