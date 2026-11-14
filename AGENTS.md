# AGENTS.md: guide for AI coding agents working on TT-Coach

Read this file fully before you change anything. Then read `requirements.md` (what to build) and `docs/hardware.md` (pins, constants, kinematics).

## 1. What we are building

TT-Coach is a table-tennis training robot: a Mecanum-wheel base that carries a camera, watches one player, scores strokes, tracks the ball, and shows results on a dashboard. This repo is the software for it: firmware, backend, vision and dashboard. It is a proof of concept built by one person on a small budget, so simple and working beats clever and complete.

Milestones: **M1 drives** (phone joystick, safe stop) then **M2 sees** (camera, pose, stroke detection) then **M3 demo** (scoring, ball tracking, dashboard). We are at the start of M1. Do not build M2 or M3 features until the owner says so.

## 2. Repo layout (create folders as needed; keep to this)

```
firmware/    PlatformIO project for the M5StickC Plus2 (C++, Arduino-ESP32 core 3.x)
backend/     Python hub (FastAPI), WebSocket relay, latest-only queues
vision/      Python vision worker: pose, stroke state machine, ball tracking, replay tools
dashboard/   Web UI (React + Vite + TypeScript)
docs/        hardware.md and other reference docs
tests/       Python tests (firmware has its own host-side tests under firmware/test)
```

`backend/backend.py` exists but is empty; the backend will be a proper package. Move it when you start the backend work.

## 3. Do

- **Safety first in motion code.** Watchdog stop (300 ms), input clamping, speed limit, and low-battery refusal come before features. They need tests.
- **Keep one source of truth.** Pins, constants, signs and the protocol live in `docs/hardware.md` and one config file per component. Change the doc in the same commit as the code.
- **Latest wins.** Camera frames and commands use queues of size 1 and drop old data. Never build a backlog.
- **Make logic testable without hardware.** Put kinematics, command parsing, filters and the stroke state machine in plain functions with unit tests. Hardware calls stay in thin wrappers.
- **Work in small steps.** One feature, one commit, tests passing. Prefer a boring solution that runs on the real device.
- **Say what you could not test.** If something needs the robot, write the exact manual test steps in the PR or commit message.
- **Ask when it matters.** See section 7.
- **Be honest in reports.** Say what ran, what did not, and what is a guess.



## 4. Do not

- **Do not drive the motors from anything except the firmware's validated path.** Backend and vision send commands; the firmware decides.
- **Do not remove or loosen the watchdog**, the speed limit or the fuse and E-stop assumptions, even temporarily or "for testing".
- **Do not assume the chip is an ESP32-S3.** It is an ESP32-PICO-V3-02. No USB host, no camera interface.
- **Do not power the PCA9685 from 5 V.** Use the stick's 3V3.
- **Do not hardcode** `LXY`**, motor signs or Wi-Fi credentials in code that is spread across files.** One config only. Credentials go in an ignored file (`secrets.h`, `.env`), never in git.
- **Do not block the asyncio event loop** with model inference or heavy OpenCV work. Use a separate process or thread.
- **Do not let OpenCV buffer frames.** Set buffer size to 1 and read the newest.
- **Do not add cloud services, accounts, telemetry or uploads.** Everything runs locally.
- **Do not commit** model weights, videos, build output or large files. Use `.gitignore` and document where to download them.
- **Do not add heavy dependencies** without saying why and what it costs on a Raspberry Pi.
- **Do not rewrite working code for style.** Change only what the task needs.
- **Do not invent hardware facts.** If a pin, value or part is not in `docs/hardware.md`, ask.
- **Do not make the project more complex than the milestone needs.** No frameworks "for later".



## 5. Conventions

**Firmware (C++ / PlatformIO)**

- Arduino-ESP32 core 3.x. Libraries: `arduinoWebSockets`, `ArduinoJson`, Adafruit PWM Servo Driver. Pin versions in `platformio.ini`.
- Motion runs in its own task at 200 Hz on core 1. Networking runs on core 0. Share data through one small struct guarded by a mutex or atomics.
- No `delay()` in the control path. No dynamic allocation in the control loop.
- Host-side unit tests for kinematics and the command parser (`pio test -e native`).

**Backend and vision (Python 3.11+)**

- FastAPI, `uvicorn`, `asyncio`, type hints everywhere, `pydantic` models for messages.
- `ruff` for lint and format, `pytest` for tests. `uv` or `pip` with a pinned `requirements` file per component (do not confuse with `requirements.md`, which is the product spec).
- Vision: Ultralytics YOLOv8-Pose for the body, MOG2 plus a small detector for the ball, One-Euro filter on keypoints, OpenCV for table corners and homography. Every stage must run offline on a saved clip (replay tool).
- Log with the standard `logging` module, one line per event, no prints in library code.

**Dashboard**

- React, Nextjs, TypeScript. Talks to the hub over WebSocket. Video over WebRTC (WHEP) from MediaMTX when we reach M2. Keep the UI plain and large-touch friendly; it is used on a phone.

**Git**

- Branch per task: `feat/…`, `fix/…`, `docs/…`. Conventional commit messages (`feat(firmware): add watchdog`).
- Small commits. Do not rewrite shared history.
- Pushes may be blocked in some sessions; commit locally and tell the owner.



## 6. How to work on a task

1. Read the matching requirement IDs in `requirements.md`.
2. State your plan in two or three lines.
3. Write the test first when the logic is testable.
4. Implement the smallest change that passes.
5. Run lint and tests; run the build.
6. Update docs if any pin, constant, protocol or command changed.
7. Report: what changed, what you ran, what needs a manual test on the robot.



## 7. When to stop and ask the owner

- A hardware fact is missing or contradicts the docs.
- A choice in `requirements.md` section 9 (open decisions) blocks you.
- The task needs a new dependency, paid service or new hardware.
- A change would touch safety behaviour.
- The simplest approach would break a requirement.

Do not guess and carry on in these cases.

## 8. Known traps

- The L298N drops about 1.5 to 2 V, so low PWM values do not move the motors (deadband `D0`). Handle it in firmware.
- If strafing goes the wrong way while forward is right, the roller direction or a motor sign is wrong. Fix the sign constant, then re-test all four directions.
- The camera never goes on the ESP32. Video goes to the Pi, phone or laptop.
- Wi-Fi power saving adds latency. Disable it on the stick for control traffic.
- Cheap 18650 cells and unbalanced packs are a fire risk. Never write code or docs that suggest bypassing the BMS or fuse.
- Mixed units: kinematics use metres and rad/s; commands from clients are unitless in [-1, 1].



## 9. Safety statement

This robot moves around people. Any change that could make it move when it should not is treated as a bug of the highest priority, and needs a test or a written manual test before it is merged.