# Hardware reference

Single source of truth for pins, constants and kinematics. If hardware changes, change this file in the same commit.

## Boards

- **M5StickC Plus2**: ESP32-PICO-V3-02. It is **not** an ESP32-S3 (no native USB camera or S3 features). Internal use: G4 power hold, G21/G22 IMU and RTC. Grove port: G32 = SDA, G33 = SCL. Header: G0, G25/G36, G26, G32, G33, 3V3, 5V, GND.
- **PCA9685** 16-channel PWM over I2C (address 0x40). **Power its VCC from the stick's 3V3, not 5 V.** The module's I2C pull-ups go to VCC, and 5 V would overstress the ESP32 pins. L298N inputs accept 3.3 V logic.
- **L298N x2**: remove the ENA/ENB jumpers (PWM goes there). Keep the 5V-EN jumper. Leave the 5V terminal unconnected. Expect the L298N to drop about 1.5 to 2 V, so motors see less than battery voltage.
- **LM2596 buck**: set to 5.10 V with a multimeter before connecting the stick.

## PCA9685 channel map

| Motor | Driver | Channels (PWM, IN-a, IN-b) |
|---|---|---|
| FL | L298N #1, A side | 0, 1, 2 |
| FR | L298N #1, B side | 3, 4, 5 |
| RL | L298N #2, A side | 6, 7, 8 |
| RR | L298N #2, B side | 9, 10, 11 |

Starting direction signs: FL +1, FR -1, RL +1, RR -1 (right side is mirrored). Adjust by test, never by rewiring.

The motor table lives in `firmware/src/config.h` (`MOTORS`, index 0..3 = FL, FR, RL, RR). That file is the only place for these channels and signs.

**Current wiring state (bring-up):** only the rear motors are wired, to one L298N: ch6 to ENA, ch7 to IN1, ch8 to IN2 (RL), ch9 to ENB, ch10 to IN3, ch11 to IN4 (RR). The channel numbers were read from the board layout, not from printed labels, so they may need a fix in `config.h` after the first motor test. The front motors (0 and 1 in the bring-up sketch) are not wired yet.

## Power

2S2P pack: four genuine 18650 3000 mAh cells, two 2-slot holders each wired in series, joined in parallel through a 2S 20A balancing BMS. Cells must be charged and matched within about 0.05 V before paralleling. Chain: BMS P+ then 7.5 A blade fuse then rocker switch (then latching E-stop later) then the +VBAT rail. All grounds meet at BMS P- (star ground). Typical draw about 1.65 A, peak about 5.5 A. Battery voltage is read on G36 through a 100k / 33k divider.

## Constants (firmware config)

| Name | Value | Note |
|---|---|---|
| R | 0.040 m | Wheel radius (80 mm wheels) |
| LXY | 0.176 | (lx + ly) in metres. **Placeholder. Measure on the real chassis and update.** |
| WMAX | 18.8 rad/s | Wheel speed limit used for scaling |
| D0 | 0.25 | Motor deadband (fraction of PWM below which the motor does not turn) |
| WATCHDOG_MS | 300 | Stop if no valid command in this time |
| Motion loop | 200 Hz | Own task on core 1 |
| PWM | 1 kHz via `pca.setPWMFreq(1000)` | Tune if motors whine |
| PCA_ADDR | 0x40 | PCA9685 I2C address |
| I2C | G32 SDA, G33 SCL, 400 kHz | Grove port |
| PIN_POWER_HOLD | G4, held HIGH | Keeps the stick powered (set first in `setup()`) |
| SPEED_CAP | 0.40 | Bring-up cap: command is clamped to [-1, 1], then limited to 40 percent, then mapped through the deadband: duty = D0 + (1 - D0) * abs(speed), so full command gives duty 0.55 (2252 of 4095) |
| TEST_SPEED | 0.40 | Speed used by the bring-up `f` and `r` keys (must not exceed SPEED_CAP) |
| TEST_MS | 1000 | Every bring-up run auto-stops after this many ms |
| Serial | 115200 baud | Bring-up keys: `s` scan, `0`-`3` select motor, `f`/`r` run, `x` stop, `?` help |

The bring-up firmware reads `D0`, `PCA_ADDR`, `I2C`, `PIN_POWER_HOLD`, `SPEED_CAP`, `TEST_SPEED`, `TEST_MS`, the PWM frequency and the motor table from `firmware/src/config.h`. `R`, `LXY`, `WMAX`, `WATCHDOG_MS` and the motion loop belong to the later drive firmware and are not in that file yet.

## Mecanum inverse kinematics

Wheel order FL, FR, RL, RR. +vx forward, +vy left, +w counter-clockwise.

```
w1 = (vx - vy - (lx+ly) * wz) / r     FL
w2 = (vx + vy + (lx+ly) * wz) / r     FR
w3 = (vx + vy - (lx+ly) * wz) / r     RL
w4 = (vx - vy + (lx+ly) * wz) / r     RR
```

If any |w| exceeds WMAX, scale all four by the same factor.

Roller check: wheels on the same diagonal must have rollers leaning the same way. Confirm with a strafe test. If strafing is wrong but forward is right, a wheel is on the wrong side.

## Protocol (firmware side)

- Robot to hub, JSON text frames every 50 ms: `{"vbat":7.9,"yaw":12.3,"estop":false}`
- Hub to robot: `{"vx":0.0,"vy":0.0,"w":0.0,"hold":false}` with values from -1 to 1.
- Any malformed frame is ignored. Silence for 300 ms means stop.
