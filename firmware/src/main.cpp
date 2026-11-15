// TT-Coach M1 firmware: thin hardware wrapper around lib/tt_core.
// All decisions (clamping, watchdog, limits, battery) live in tt_core and are unit tested.
// Core 1: 200 Hz motion task. Core 0: Wi-Fi, WebSocket, telemetry, screen.
#include <Adafruit_PWMServoDriver.h>
#include <ArduinoJson.h>
#include <M5StickCPlus2.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <Wire.h>

#include "battery.h"
#include "command.h"
#include "kinematics.h"
#include "motion.h"
#include "secrets.h"  // copy include/secrets.example.h to include/secrets.h
#include "tt_config.h"

using namespace tt;

static Adafruit_PWMServoDriver pca(cfg::PCA_ADDR, Wire);
static WebSocketsClient ws;
static bool pcaOk = false;

// Data shared between the two cores, guarded by a spinlock.
struct Shared {
  Command cmd;
  bool hasCmd = false;
  uint32_t lastCmdMs = 0;
  // written by the motion task
  float vbat = 0;
  float yawDeg = 0;
  bool estop = false;
  bool driving = false;
  BatteryState batt = BatteryState::Stop;
  StopReason reason = StopReason::NoCommand;
};
static Shared shared;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ---- PCA9685 output (only writes on change, to keep I2C traffic low) -------
static uint16_t lastDuty[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
static int8_t lastDir[4] = {-2, -2, -2, -2};

static void setMotor(int i, const MotorDrive& d) {
  const cfg::MotorCfg& m = cfg::MOTORS[i];
  if (d.duty == 0) {
    // Stop: both direction pins low (coast), enable fully off.
    if (lastDuty[i] != 0) {
      pca.setPWM(m.pwmCh, 0, 4096);
      pca.setPWM(m.in1Ch, 0, 4096);
      pca.setPWM(m.in2Ch, 0, 4096);
      lastDuty[i] = 0;
      lastDir[i] = -2;
    }
    return;
  }
  const int8_t dir = d.forward ? 1 : -1;
  if (dir != lastDir[i]) {
    pca.setPWM(m.in1Ch, d.forward ? 4096 : 0, d.forward ? 0 : 4096);  // full ON / full OFF
    pca.setPWM(m.in2Ch, d.forward ? 0 : 4096, d.forward ? 4096 : 0);
    lastDir[i] = dir;
  }
  if (d.duty != lastDuty[i]) {
    pca.setPWM(m.pwmCh, 0, d.duty >= 4095 ? 4096 : d.duty);  // 4096 in off = full on
    lastDuty[i] = d.duty;
  }
}

static void allStop() {
  if (!pcaOk) return;
  for (int i = 0; i < 4; i++) {
    lastDuty[i] = 0xFFFF;  // force a write
    setMotor(i, {0, true});
  }
}

// ---- Motion task: core 1, 200 Hz -------------------------------------------
static void motionTask(void*) {
  Motion motion;
  BatteryMonitor battery;
  float yaw = 0, gyroBias = 0;
  uint32_t lastBattMs = 0;
  TickType_t t = xTaskGetTickCount();
  const float dt = cfg::MOTION_PERIOD_MS / 1000.0f;

  for (;;) {
    // IMU yaw (gyro z, deg/s). NOTE: sign and units need the manual check in the PR notes.
    StickCP2.Imu.update();
    const float gz = StickCP2.Imu.getImuData().gyro.z;
    yaw += (gz - gyroBias) * DEG_TO_RAD * dt;

    const uint32_t now = millis();
    if (now - lastBattMs >= cfg::BATT_SAMPLE_MS) {
      lastBattMs = now;
      const float v = batteryVoltsFromMilliVolts(analogReadMilliVolts(cfg::VBAT_ADC_PIN));
      battery.update(v);
      portENTER_CRITICAL(&mux);
      shared.vbat = v;
      portEXIT_CRITICAL(&mux);
    }

    MotionInput in;
    portENTER_CRITICAL(&mux);
    in.cmd = shared.cmd;
    in.hasCmd = shared.hasCmd;
    in.lastCmdMs = shared.lastCmdMs;
    portEXIT_CRITICAL(&mux);
    in.nowMs = millis();  // read AFTER the command so a fresh command is never "in the past"
    in.estop = (digitalRead(cfg::ESTOP_PIN) == LOW);
    in.batt = battery.state();
    in.yawRad = yaw;

    MotionOutput out = motion.step(in, dt);

    if (pcaOk) {
      for (int i = 0; i < 4; i++) setMotor(i, shapeWheel(out.wheels.w[i], cfg::MOTORS[i].sign));
    }

    portENTER_CRITICAL(&mux);
    shared.yawDeg = yaw * RAD_TO_DEG;
    shared.estop = in.estop;
    shared.driving = out.driving;
    shared.batt = in.batt;
    shared.reason = out.reason;
    portEXIT_CRITICAL(&mux);

    vTaskDelayUntil(&t, pdMS_TO_TICKS(cfg::MOTION_PERIOD_MS));
  }
}

// ---- Network (core 0) ------------------------------------------------------
static void onWs(WStype_t type, uint8_t* payload, size_t len) {
  if (type != WStype_TEXT) return;
  Command c;
  if (!parseCommand(reinterpret_cast<const char*>(payload), len, c)) return;  // malformed: ignore
  const uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  shared.cmd = c;
  shared.hasCmd = true;
  shared.lastCmdMs = now;
  portEXIT_CRITICAL(&mux);
}

static const char* reasonText(StopReason r, bool batt) {
  switch (r) {
    case StopReason::None: return batt ? "LIMIT" : "DRIVE";
    case StopReason::NoCommand: return "LINK";
    case StopReason::EStop: return "ESTOP";
    default: return "BATT LOW";
  }
}

static void drawScreen(const Shared& s, bool wsUp) {
  auto& d = StickCP2.Display;
  d.startWrite();
  d.fillScreen(BLACK);
  d.setTextColor(WHITE, BLACK);
  d.setTextSize(2);
  d.setCursor(4, 4);
  d.printf("%.2f V", s.vbat);
  d.setCursor(4, 28);
  d.print(batteryStateName(s.batt));
  d.setCursor(4, 52);
  d.print(WiFi.status() == WL_CONNECTED ? (wsUp ? "HUB OK" : "NO HUB") : "NO WIFI");
  d.setCursor(4, 76);
  d.setTextColor(s.driving ? GREEN : RED, BLACK);
  d.print(reasonText(s.reason, s.batt == BatteryState::Limited));
  d.setTextSize(1);
  d.setTextColor(WHITE, BLACK);
  d.setCursor(4, 108);
  d.print(WiFi.localIP());
  if (!pcaOk) {
    d.setTextColor(RED, BLACK);
    d.setCursor(4, 120);
    d.print("PCA9685 NOT FOUND");
  }
  d.endWrite();
}

static bool wsUp = false;
static void onWsState(WStype_t type, uint8_t* p, size_t n) {
  if (type == WStype_CONNECTED) wsUp = true;
  if (type == WStype_DISCONNECTED) wsUp = false;
  onWs(type, p, n);
}

void setup() {
  auto cfgM5 = M5.config();
  StickCP2.begin(cfgM5);  // also latches power-hold (G4)
  StickCP2.Display.setRotation(1);

  // Outputs first: make sure nothing is driven before anything else runs.
  pinMode(cfg::ESTOP_PIN, INPUT_PULLUP);
  Wire.begin(cfg::I2C_SDA, cfg::I2C_SCL, 400000);
  Wire.beginTransmission(cfg::PCA_ADDR);
  pcaOk = (Wire.endTransmission() == 0);
  if (pcaOk) {
    pca.begin();
    pca.setPWMFreq(cfg::PCA_PWM_HZ);
    allStop();
  }

  // Gyro bias: robot is stationary and motors are off during boot.
  // (Kept simple: motion task starts with bias 0; see PR notes for the manual yaw check.)

  // Start the motion task BEFORE Wi-Fi so the watchdog is active from boot.
  xTaskCreatePinnedToCore(motionTask, "motion", 4096, nullptr, 5, nullptr, 1);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // power saving adds latency
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  ws.begin(HUB_HOST, HUB_PORT, HUB_PATH);
  ws.onEvent(onWsState);
  ws.setReconnectInterval(500);
  ws.enableHeartbeat(1000, 500, 2);  // drop a dead connection quickly
}

void loop() {
  ws.loop();

  static uint32_t tTel = 0, tScr = 0;
  const uint32_t now = millis();

  if (now - tTel >= cfg::TELEMETRY_PERIOD_MS) {
    tTel = now;
    Shared s;
    portENTER_CRITICAL(&mux);
    s = shared;
    portEXIT_CRITICAL(&mux);
    if (wsUp) {
      char b[160];
      snprintf(b, sizeof b, "{\"vbat\":%.2f,\"yaw\":%.1f,\"estop\":%s,\"batt\":\"%s\",\"alive\":%s}", s.vbat,
               s.yawDeg, s.estop ? "true" : "false", batteryStateName(s.batt),
               s.reason == StopReason::NoCommand ? "false" : "true");
      ws.sendTXT(b);
    }
  }

  if (now - tScr >= 250) {
    tScr = now;
    Shared s;
    portENTER_CRITICAL(&mux);
    s = shared;
    portEXIT_CRITICAL(&mux);
    drawScreen(s, wsUp);
  }
}
