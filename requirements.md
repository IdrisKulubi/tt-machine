# TT-Coach v1: Requirements

Status: draft 2 (merged with TT-Coach_v1_Engineering_Plan.pdf Rev 1.0, Nov 2026) · Owner: Idris Kulubi · Read `AGENTS.md` before writing any code.

## 1. Product in one paragraph

TT-Coach is a small mobile robot that helps a single player train table tennis. It drives on a Mecanum base, watches the player with **Camera A** on the robot, scores each stroke (form, consistency), tracks the ball and its speed with a second, fixed **Camera B** on a tripod, and shows everything on a simple dashboard. v1 is a proof of concept: it must work for one player, one table, indoors, and be easy for one person to set up.

**Design principle: simple to use.** One robot, one power switch, one phone or laptop to open the dashboard. Anything that adds a second device or a manual calibration step needs a strong reason. The only second device accepted for v1 is the static Camera B (see section 3.1); its calibration is a one-time step per venue.

### 1.1 The two cameras

| | Camera A (robot) | Camera B (static) |
|---|---|---|
| Job | Watch the player: pose, stroke scoring, follow control | Watch the table: ball detection, tracking, speed, bounces, landing points |
| Mount | On a mast on the robot, lens about 579 mm high, tilted about 10 degrees up, 2.5 to 3.5 m behind the player and 30 to 45 degrees off the player's axis | Tripod, about 1.6 m, fixed, sees the whole table (2.74 x 1.525 m) |
| Hardware (plan) | Pi Camera Module 3 Wide (102 degree HFOV) on a Pi Zero 2 W, H.264 hardware encode | USB webcam 1080p60 (MJPEG) on the edge computer |
| Why separate | Needs to move with the player | A moving camera cannot give metric 3D ball position (depth is unobservable and ego-motion corrupts the state). A fixed frame plus known table geometry can. |

Ball speed and landing points come from Camera B only. Camera A is never used for ball metrics.

## 2. Users

| User | Needs |
|---|---|
| Player (main user) | Switch on, open a page, train. See stroke feedback after each rally or drill. |
| Builder (Idris) | Clear logs, tests, safe bring-up, easy tuning of constants without reflashing everything. |

## 3. Scope and milestones

| Milestone | Name | Outcome | Priority |
|---|---|---|---|
| M1 | Drives | Battery-powered Mecanum base driven from a phone joystick page, with a safe stop. | Must |
| M2 | Sees | Camera stream on the dashboard with live pose skeleton and stroke detection. | Must |
| M3 | Demo | Stroke scoring plus ball tracking shown on the dashboard for a live drill. | Must |
| M4 | Autonomy | Robot repositions itself using table and player position. | Should |
| M5 | Polish | Session history, exports, nicer UI. | Could |

Out of scope for v1: ball feeding or launching, multi-player, outdoor use, cloud accounts, competitive match scoring, any safety-critical use around children without an adult present, spin estimation, stereo ball tracking, wheel encoders and wheel-speed PID, on-robot inference.

Engineering plan schedule (8 weeks): M1 drives (teleop strafe with watchdog and E-stop), M2 sees (live skeleton overlay), M3 demo (follow plus ball tracking plus stroke scores in a real session). **Conflict to resolve:** the plan puts autonomous follow in M3, this file puts it in M4. See OD-5.

### 3.1 Supporting build items (not features, but needed)

- Camera A video host: Raspberry Pi Zero 2 W running MediaMTX (the ESP32 cannot host a camera: no USB host, no H.264 encoder, too little Wi-Fi throughput).
- PCA9685 I2C PWM board on the Grove port (the stick has too few free pins for two L298N drivers).
- Edge computer (laptop or PC with an NVIDIA RTX GPU, at least 6 GB VRAM) running the hub and the vision workers. Jetson Orin Nano class is the fallback.
- Dedicated 2.4 GHz router on its own channel, no internet needed. The edge computer sits on Ethernet so only the robot uses Wi-Fi.
- Camera B tripod, orange 40+ mm balls, 2S Li-ion balance charger.

## 4. Functional requirements

### M1: Drives

| ID | Requirement | Acceptance check |
|---|---|---|
| FR-1.1 | Drive in any direction and rotate (forward, back, strafe, turn, and mixes) from a phone page with a joystick for movement and a second control for rotation. | Strafe test: robot moves sideways in a straight line within 10 degrees over 1 m. |
| FR-1.2 | The robot stops within 300 ms when commands stop arriving (lost Wi-Fi, closed page, app crash). | Pull the phone's Wi-Fi mid-drive: wheels stop. Measured stop time at most 300 ms after the last command. |
| FR-1.3 | Command format is `{"vx":..,"vy":..,"w":..,"hold":..}` with values from -1 to 1. Unknown or out-of-range values are clamped, never trusted. | Unit test on the command parser. |
| FR-1.4 | Wheel speeds are scaled proportionally when any wheel would exceed its limit, so the direction of travel is preserved. | Unit test on the kinematics function. |
| FR-1.5 | Optional heading hold using the IMU: when `hold` is true and no rotation is commanded, the robot keeps its yaw. | Drive straight 3 m: drift under 10 degrees. |
| FR-1.6 | The stick shows battery voltage and connection status on its screen. | Visual check. |
| FR-1.7 | Per-motor direction signs and the geometry constant `LXY` are changeable in one place, with no code changes elsewhere. | Constants live in one config file. |
| FR-1.8 | Low battery: below the cutoff voltage the robot refuses to drive and says so on screen and on the page. Plan values: below 6.6 V limit speed to 50 percent; at 6.2 V stop and sound an alarm. | Test with a bench supply. |
| FR-1.9 | Acceleration limiter in firmware: at most 0.8 m/s^2, so the tall camera mast does not whip. Speed caps: 0.75 m/s forward, 0.6 m/s strafe. | Unit test on the limiter; step command on the robot, mast wobble decays in under 0.5 s. |
| FR-1.10 | PWM shaping: `duty = D0 + (1 - D0) * abs(i) / max` with `D0` about 0.25 and `max` about 18.8 rad/s, so low commands overcome motor stiction. Wheel signs are set during bring-up. | Unit test on the shaping function; four-direction check on the robot. |
| FR-1.11 | Firmware motion task runs at 200 Hz on core 1; networking and telemetry on core 0. Telemetry (vbat, yaw, estop, wheel PWM) is sent at 20 Hz or better. | Log timestamps. |
| FR-1.12 | The firmware reads the E-stop auxiliary contact (G26) and reports `estop` in telemetry. The E-stop cuts the motor rail only; the logic stays up and logs the event. | Press E-stop mid-drive: motors dead at once, telemetry shows `estop=1`. |
| FR-1.13 | Tilt servo on a spare PCA9685 channel is optional (camera tilt). | Visual check, if fitted. |

### M2: Sees

| ID | Requirement | Acceptance check |
|---|---|---|
| FR-2.1 | Camera video reaches the dashboard with low delay (see NFR-2). | Latency measurement with a blinking light. |
| FR-2.2 | 17-keypoint body pose is estimated for the player at 15 frames per second or better. | FPS counter in logs. |
| FR-2.3 | Keypoints are smoothed (One-Euro filter) before any scoring. | Unit test on the filter. |
| FR-2.4 | A stroke state machine finds the start, contact and end of forehand and backhand strokes from pose. | Replay of recorded clips finds at least 90 percent of strokes. |
| FR-2.5 | The dashboard shows live video with the skeleton drawn on it. | Visual check. |
| FR-2.6 | Camera A runs 1280x720 at 60 fps H.264 on the Pi (about 4 Mbit/s) via MediaMTX; the edge computer reads it over RTSP and the browser gets it over WebRTC (WHEP). Not HLS. | Stream plays in the dashboard; stream stats in logs. |
| FR-2.7 | Pose runs on the edge GPU (YOLOv8s-Pose, 640 px, FP16, about 10 ms). Design target is 60 fps; FR-2.2's 15 fps is the floor. Keypoints below confidence 0.5 are ignored. | FPS counter in logs. |
| FR-2.8 | Camera A lens distortion is removed with a calibration lookup table before pose. | Calibration file loads; undistorted frame is straight at the edges. |
| FR-2.9 | Stroke state machine rules (initial): ready = furthest backswing before the wrist-speed peak; strike = peak wrist speed; finish = highest wrist point after strike. Forehand or backhand is decided by which side of the body midline the wrist is on at strike. | Unit tests on synthetic keypoint sequences. |
| FR-2.10 | Every frame carries a timestamp, and overlays are locked to video by frame timestamp. | Overlay does not drift from the video in a replay. |
| FR-2.11 | Camera B (USB, MJPEG, 1080p60, fixed exposure about 1/1000 s) is read with buffer size 1, newest frame only. If Camera B is missing, everything except ball metrics still works and the dashboard says so. | Unplug Camera B: no crash, dashboard shows "ball camera offline". |

### M3: Demo

| ID | Requirement | Acceptance check |
|---|---|---|
| FR-3.1 | Each stroke gets a score from joint angles compared with a reference range (elbow, shoulder, hip rotation, follow-through). The score and the reasons for it are shown. | Score and per-joint breakdown appear for every detected stroke. |
| FR-3.2 | The ball is detected in 2D (background subtraction plus a small detector) and tracked across frames. | Ball track drawn on replay clips. |
| FR-3.3 | The table is found in the image (corner detection) so image positions can be mapped to table positions. | Table outline drawn and stable while the robot is still. |
| FR-3.4 | The dashboard lists the session: strokes, scores, simple trends. | Visual check. |
| FR-3.5 | Raw recordings can be saved locally for offline replay and testing. | Replay tool runs the full pipeline on a saved clip. |
| FR-3.6 | Scoring metrics and initial bands (to be re-fitted, see FR-3.12): elbow at finish 90 to 120 deg (shoulder 6, elbow 8, wrist 10); shoulder elevation at finish 70 to 110 deg (hip 12, shoulder 6, elbow 8); knee flexion at ready 130 to 155 deg (hip 12, knee 14, ankle 16); trunk rotation (shoulders 5 and 6 vs hips 11 and 12) over the ready to strike range. Score is the weighted distance from each band mapped to 0 to 100, with a short text hint per metric. Angles use `atan2` so they are stable near 0 and 180 deg. | Unit tests on the angle function and the scorer. |
| FR-3.7 | **Camera B calibration:** intrinsics from a 9x6 checkerboard, then the four table corners give a homography from image to table plane in metres (table 2.74 x 1.525 m). Calibration is stored in a file and reused until the camera moves. | Reprojected corner error reported; table outline drawn on Camera B view. |
| FR-3.8 | **Ball detection on Camera B:** MOG2 background subtraction gated by orange hue, then a small detector (YOLO-nano class) confirms candidates. | Detection recall on recorded rallies (T9). |
| FR-3.9 | **Ball state and speed:** an extended Kalman filter with state [px, py, pz, vx, vy, vz], gravity and drag (kd about 0.14 per metre), spin treated as process noise. Height is pinned to the table plane at each detected bounce. During occlusion (net, body) the filter predicts only and the prediction is drawn dotted. The dashboard shows ball speed in m/s. | Unit tests on the filter with synthetic trajectories; speed shown on replay. |
| FR-3.10 | Bounce and net events are detected and tagged; landing points are recorded and shown as a per-session heatmap. | Landing points within 10 cm of chalk marks (T9). |
| FR-3.11 | **Fusion:** pose and ball results from the two cameras are merged into one timestamped telemetry frame (see section 8.1). The two cameras are time-aligned on the edge computer clock; the largest allowed skew is 1 frame at 60 fps. | Log of per-frame skew. |
| FR-3.12 | Reference stroke set: record about 200 labelled strokes per type with a local coach to re-fit the bands. | Dataset exists outside git (see AGENTS.md). |
| FR-3.13 | Session store: SQLite for sessions, strokes and scores; Parquet for telemetry and clip metadata. Review mode replays stored clips with the same overlays. | Replay a stored session in the dashboard. |
| FR-3.14 | Dashboard live view (large-touch, phone friendly): video with skeleton and ball overlays, last stroke type and score with per-joint breakdown, ball speed, stroke count, average score, on-table percentage, robot battery, strafe speed, heading hold, E-stop state, wheel PWM, link latency, and a "link lost" badge. | Visual check. |

### M4 and later (not for first build)

Reposition to keep the player and table in frame (follow control, see FR-4.x). Stereo Camera B for a second view and spin estimate from logo or seam. Encoder motors and wheel-speed PID. On-robot inference.

### M4: Autonomy (follow control)

| ID | Requirement | Acceptance check |
|---|---|---|
| FR-4.1 | Outer loop on the edge computer at 50 Hz: error between the player's hip-centre x in the image and the image centre, through a PD controller with a deadband (about 4 percent), sets strafe `vy`. | Unit test on the controller. |
| FR-4.2 | Rate limit 0.8 m/s^2 and speed cap 0.6 m/s apply to follow commands too. The firmware still validates everything (FR-1.2, FR-1.3). | Same tests as M1. |
| FR-4.3 | If the player is lost for more than 0.5 s, set `vy` to 0 (hold position) and keep streaming. | Cover the player: robot stops. |
| FR-4.4 | The dashboard shows a clear "autonomous" indicator and a one-tap switch back to manual. | Visual check. |
| FR-4.5 | Heading hold from the IMU runs during follow: yaw drift under 3 deg over a 3 m strafe out and back. | T2. |

## 5. Non-functional requirements

| ID | Requirement | Target |
|---|---|---|
| NFR-1 | Control latency, phone to wheels | Under 100 ms typical |
| NFR-2 | Vision latency, camera to dashboard overlay | Under 100 ms budget (design aim about 85 ms) |
| NFR-3 | Always use the newest frame or command. Never queue old ones. | Queues of size 1, drop old |
| NFR-4 | Runtime on one charge | At least 2 hours of mixed use (design estimate 2.9 h) |
| NFR-5 | Setup by one person | Under 5 minutes from switch-on to driving |
| NFR-6 | No cloud dependency | Works with no internet; the robot or the phone hosts the network |
| NFR-7 | Reproducible builds | One command to build firmware, one to start the backend, one to run tests |
| NFR-8 | Budget | Stay within the BOM in the engineering plan (KSh 48,895 including 10 percent contingency: robot 33,050, field kit 11,400) unless the owner approves a change |
| NFR-9 | Telemetry latency, frame arrival on the edge to overlay data | Under 100 ms (design estimate about 27 ms) |
| NFR-10 | Video overlay latency, glass to overlay, p95 | Under 120 ms (budget 85 ms: exposure 12, ISP and encode 18, Wi-Fi 12, decode 8, pose 12, track and score 8, fan-out and browser paint 16, design estimates) |
| NFR-11 | Link rates | Commands 50 Hz, robot telemetry 20 Hz, UI overlays 30 Hz, Camera A 60 fps, Camera B 60 fps |
| NFR-12 | Camera A image stability while strafing at 0.5 m/s | Optical-flow jitter at most 1.5 px RMS at 720p (mast isolated with TPU bobbins, target natural frequency 15 to 20 Hz) |
| NFR-13 | Mechanical | Overall about 257 x 232 x 601 mm, mass about 1.25 kg, CG about 105 mm; tipping margin at least 10x at capped acceleration |
| NFR-14 | Ball camera robustness | Orange balls, dark backdrop behind Camera B's view, fixed exposure so bright backgrounds do not lose the ball |
| NFR-15 | Every capture and queue drops stale data. No stage may block on another. | Stop the dashboard: robot and vision keep running; no memory growth |

## 6. Safety requirements

| ID | Requirement |
|---|---|
| SR-1 | Watchdog stop (FR-1.2) is mandatory and tested before any other motion feature. |
| SR-2 | Fuse in the battery positive line (7.5 A blade fuse after the BMS). The battery is never connected without it. The fuse must ride through the about 5.5 A start-up peak. |
| SR-3 | Hardware emergency stop (latching, normally-closed) is added before the robot runs near people. It breaks the motor rail only; logic and compute stay powered. Software stop is not a replacement. Test: E-stop kills motor power within one second. |
| SR-8 | Three separate power rails: raw motor rail (E-stop breakable), 5.10 V logic rail, 5.15 V compute rail (Pi). Motor noise must not reach the ESP32 or the Pi. Set both bucks with a meter before connecting any load. Star ground at the BMS P-. Decoupling: 470 uF low-ESR plus 100 nF on each L298N VS, 0.1 uF ceramic across every motor terminal. |
| SR-9 | Do not power anything off the L298N onboard 5 V pin. Remove the ENA and ENB jumpers; keep the 5 V-EN jumper (VS under 12 V). Do not power the PCA9685 from 5 V; use the stick's 3V3 (see `docs/hardware.md`). |
| SR-10 | Bring-up order is mandatory: pack, BMS and fuse measured at 7.4 to 8.4 V; bucks trimmed; stick and PCA9685 I2C scan (expect 0x40); motors with wheels off the ground; Pi on its own rail with no brown-out under stress; E-stop loop closed and tested. |
| SR-11 | Camera A mast is isolated from the deck through TPU bobbins (no metal path); acceleration is capped (FR-1.9) so the robot cannot tip. |
| SR-4 | Motor speed is limited by default (start at 40 percent). Full speed is an explicit setting. |
| SR-5 | The robot never drives on its own until M4, and then only with the watchdog and a visible "autonomous" indicator. |
| SR-6 | Battery safety: genuine cells, BMS with balancing, matched cells before paralleling, charge only on the proper charger, never unattended. |
| SR-7 | Camera data stays on the local network. No uploads. Anyone filmed must know. If children are filmed, an adult must be present and consent applies. |

## 7. Hardware baseline (what exists)

See `docs/hardware.md` for pins, constants and kinematics. In short: 2-deck acrylic 4WD chassis, four TT motors with 80 mm Mecanum wheels, two L298N drivers, PCA9685 PWM board, M5StickC Plus2 (ESP32, not S3), 2S2P 18650 pack with 20 A BMS, LM2596 buck converter, fuse, rocker switch.

## 8. Software architecture (target)

```
ROBOT                                         EDGE COMPUTER (laptop, RTX GPU)                BROWSER
Camera A -> Pi Zero 2 W (MediaMTX) --RTSP-->  Pose worker (YOLOv8s-Pose, filter, strokes) -+
M5StickC (motors, IMU, safety) <--WS 50 Hz--  Hub (FastAPI: /ws/robot /ws/ui /api) <-------+--> Dashboard (React)
                               --WS 20 Hz-->  Follow controller (M4)                            WebRTC video (WHEP via MediaMTX)
Camera B (USB, tripod) ---------USB-------->  Ball worker (MOG2 + detector + EKF) ----------+
                                              Session store (SQLite + Parquet)
```

- Three tiers: the robot does motion and video capture only; all AI runs on the edge computer; the browser only presents.
- Firmware owns motion and the safety stop. The hub and vision never drive the motors directly; they send commands that the firmware validates.
- The hub keeps only the newest message per stream (queues of size 1).
- Pose worker and ball worker are separate processes so slow inference never blocks the hub's event loop. They merge into one timestamped telemetry frame.
- The robot connects out to the edge computer (no open ports on the robot).
- MediaMTX relays video so Python never sits in the browser's video path.

### 8.1 Telemetry frame (WebSocket /ws/ui, 30 Hz)

```json
{"t": 0.0, "frame": 0,
 "player": {"kp": [[0, 0, 0]], "angles": {"elbow": 0, "shoulder": 0, "knee": 0, "trunk": 0}},
 "stroke": {"type": "FH_topspin", "phase": "finish", "score": 0},
 "ball": {"p": [0, 0, 0], "v": [0, 0, 0], "pred": [], "bounce": null},
 "robot": {"vy": 0, "vbat": 0, "yaw": 0, "estop": 0, "link_ms": 0}}
```

`kp` has 17 entries of [x, y, conf]; `pred` has up to 20 [x, y, z] points. The protocol goes into `docs/` and changes there in the same commit as the code.

### 8.2 Planned repo layout additions

`robot-pi/` for the Pi image config (`mediamtx.yml`, `provision.sh`); `vision/calib/` for Camera A intrinsics and Camera B intrinsics and table corners; `vision/follow.py` for the follow controller. (The plan names `edge/`; this repo uses `backend/` and `vision/` per `AGENTS.md`.)

## 9. Open decisions (do not guess; ask the owner)

| ID | Question | Options |
|---|---|---|
| OD-1 | Where does vision run? | Engineering plan says: laptop or PC with an RTX GPU (existing), Jetson Orin Nano as fallback; the Pi Zero 2 W only streams video. Alternatives: Pi 5 with AI HAT+, Android phone. **Owner to confirm**, and confirm the laptop's GPU and VRAM. |
| OD-2 | One camera or two? | **Decided by the owner: two cameras** (Camera A on the robot, Camera B static for the ball). Build and test Camera A first; Camera B arrives with M3. Open: Camera B placement and mount height. |
| OD-3 | Wi-Fi: the stick makes its own network, or joins a router? | Plan says a dedicated isolated 2.4 GHz router (channels 1, 6 or 11, no internet) with the robot joining it. M1 started with a stick-hosted network. **Owner to confirm** which wins. |
| OD-4 | Dashboard hosting | Plan hosts the hub and dashboard on the edge computer, not on the robot. **Owner to confirm.** |
| OD-5 | Is autonomous follow in M3 (plan) or M4 (this file)? | Keep M4 unless the owner says otherwise. |
| OD-6 | BMS rating: `docs/hardware.md` and the existing parts say 2S 20 A, the plan's BOM says 2S 10 A | Check what was bought. Either works with the 7.5 A fuse; make the docs agree. |
| OD-7 | Compute budget variant: the plan lists a cheaper robot (USB webcam on the Pi, about 720p30 for Camera A, saves about KSh 4,300) | Default is Pi Camera Module 3 Wide. |
| OD-8 | The plan's own sample firmware connects with hardcoded `SSID`, `PASS` and `SERVER_IP` | Do not copy it. Use the ignored `secrets.h` (AGENTS.md). |

## 10. System acceptance tests (from the engineering plan, on a real court)

| ID | Area | Test | Pass criterion |
|---|---|---|---|
| T1 | Mobility | Strafe 3 m on the court floor at full command, timed | Mean speed at least 0.5 m/s |
| T2 | Heading hold | 3 m strafe out and back, yaw by IMU plus floor tape | Yaw drift at most 3 deg, lateral offset at most 5 cm |
| T3 | Safety | Kill Wi-Fi mid-motion; press E-stop mid-motion | Stop within 300 ms; motor rail dead instantly |
| T4 | Endurance | Continuous follow session until the low-battery stop | At least 2 h |
| T5 | Image stability | Optical-flow jitter on a static scene while strafing at 0.5 m/s | At most 1.5 px RMS at 720p |
| T6 | Latency | LED-blink glass-to-glass, 200 samples | Overlay p95 at most 120 ms; telemetry at most 100 ms |
| T7 | Pose accuracy | Elbow angle vs manual goniometer, 30 poses, side view | RMSE at most 8 deg |
| T8 | Stroke detection | 100 coach-labelled strokes (forehand and backhand) | Recall at least 90 percent, type accuracy at least 85 percent |
| T9 | Ball tracking | Rally footage; landing points vs chalk marks | Detection at least 90 percent; landing error at most 10 cm |
| T10 | Follow behaviour | 10 minute drill with lateral footwork | Hip centre in the middle 30 percent of the frame at least 90 percent of the time |

## 11. Risks and mitigations

| Risk | Level | Mitigation |
|---|---|---|
| Counterfeit 18650 cells | High | Known vendors; capacity-test each cell; match within 50 mAh |
| Wi-Fi congestion in a club hall | High | Dedicated router on the cleanest channel; laptop on Ethernet; latest-only queues; watchdog stop |
| Acrylic cracks at motor mounts | Medium | Nylon washers, no over-tightening; spare deck, or reprint in PETG |
| L298N heat at sustained load | Medium | Duty cap 85 percent; TB6612FNG in v2 |
| Pose errors at extreme torso twist | Medium | Robot 30 to 45 degrees off-axis; temporal filtering; optional fine-tune on local footage |
| Ball lost against bright backgrounds | Medium | Orange balls; dark backdrop; fixed exposure about 1/1000 s |
| Camera B moved or bumped, so the homography is wrong | Medium | Detect table-corner drift and ask for recalibration; tape tripod feet |
| Camera A and B clocks drift | Medium | One clock on the edge computer stamps both at arrival (FR-3.11) |

Roadmap after v1: v1.5 encoder motors on a dedicated ESP32-S3 motor board with wheel-speed PID and odometry, TB6612FNG drivers; v2 stereo Camera B pair, spin from the logo or seam, 9-state EKF with Magnus; v3 on-robot inference on an NPU or Jetson-class module.

## 12. Definition of done (for any task)

1. Meets the acceptance check in this file.
2. Has tests where the logic can be tested without hardware.
3. Builds from a clean checkout with the documented commands.
4. No secrets, no large binaries committed.
5. `docs/` updated if a constant, pin, protocol or command changed.
6. Anything that cannot be tested without the robot is listed in the PR as a manual test with exact steps.
