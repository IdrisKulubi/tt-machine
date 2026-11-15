# M1 bring-up and manual tests

Everything here needs the robot. Software tests (`pytest`, `npm test`, `pio test -e native`) do not replace these.

## First power-up (do this order)

1. **Wheels off the ground, battery pack NOT connected, stick on USB only.** Flash the firmware. The screen must show `NO WIFI` or `NO HUB`, a `low` battery state, and `PCA9685 NOT FOUND` if the PCA is not wired. The robot refuses to drive on USB power (battery reads 0 V).
2. Fit the fuse and measure the pack (7.4 to 8.4 V). Trim the LM2596 to 5.10 V before connecting the stick (see `docs/hardware.md`).
3. Check the PCA9685 is found at 0x40 (no "NOT FOUND" on the screen). If not, check the Grove bus choice (`Wire` vs `Wire1`) first.
4. Calibrate `VBAT_CAL`: compare the screen voltage with a multimeter on the pack, set `VBAT_CAL = meter / screen`, reflash.
5. Wheels still off the ground: run the hub, open the page, push the stick. Check each wheel direction against the table below. Fix wrong wheels with the sign in `tt_config.h`, never by rewiring.

| Command | FL | FR | RL | RR |
|---|---|---|---|---|
| Forward | + | + | + | + |
| Strafe left | - | + | + | - |
| Rotate CCW | - | + | - | + |

(+ means the wheel pushes the robot forward.)

## Manual tests (put results in the PR)

| Test | Steps | Pass |
|---|---|---|
| Watchdog, link | Drive at low speed with wheels off the ground. Turn the laptop Wi-Fi off (or kill the hub). | Wheels stop within 300 ms of the last command; screen shows LINK. |
| Watchdog, phone | Drive, then close the page or turn off the phone Wi-Fi. | Wheels stop (hub stops forwarding after 150 ms, the robot stops 300 ms after that). |
| Recovery | After a stop, reconnect and push the stick. | Robot ramps up from rest, does not jump. |
| Stop time | Film the wheels and the phone with a 240 fps camera, or log timestamps. | Measured stop at most 300 ms after the last command reaches the robot. |
| Speed limit | Full stick forward on the floor. | About 0.3 m/s (40 percent of 0.75). Robot actually moves (above the deadband). |
| Strafe | Mark 1 m, strafe left with the stick. | Straight within 10 degrees over 1 m. If it goes the wrong way while forward is right, fix a motor sign. |
| Battery limit | Bench supply on the pack input: 8 V, then 6.5 V, then 6.1 V, then back up. | At 6.5 V the screen shows `limited`; at 6.1 V it shows `low` and the robot refuses to drive; recovery needs 6.8 V for `ok`. |
| Yaw sign and units | With wheels off the ground, rotate the robot by hand 90 degrees CCW and read `yaw` on the hub log. | About +90. If the size is wrong, the gyro unit is wrong; if negative, flip the sign. Check this BEFORE using heading hold. |
| Heading hold | Hold on, drive straight 3 m; also nudge it sideways gently. | Drift under 10 degrees over 3 m; robot resists the nudge. If it turns away from the nudge, flip the yaw sign. |
| Accel limit | Step the stick from 0 to full. | No wheel spin, no mast whip. |
| E-stop pin | Short G26 to GND. | Wheels stop, `E-STOP` shows on the page. (Hardware E-stop not fitted yet.) |
| USB-only refusal | Power the stick from USB only. | Robot refuses to drive. |

## Running it

```bash
# one-time
python -m venv .venv && .venv/Scripts/pip install -r backend/requirements-dev.txt
cp firmware/include/secrets.example.h firmware/include/secrets.h   # edit SSID, password, laptop IP
cd dashboard && npm install
```

```bash
# hub (from the repo root)
cd backend && ../.venv/Scripts/python -m uvicorn tt_hub.app:app --host 0.0.0.0 --port 8000
```

```bash
# phone page: open http://<laptop-ip>:3000 on the phone
cd dashboard && npm run dev
```

```bash
# firmware
cd firmware && pio run -e m5stick -t upload
```

```bash
# tests
.venv/Scripts/python -m pytest
cd dashboard && npm test && npm run typecheck
cd firmware && pio test -e native
```

The phone, laptop and stick must be on the same 2.4 GHz network. Allow ports 3000 and 8000 through the laptop firewall.
