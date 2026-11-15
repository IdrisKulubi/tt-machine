import asyncio
import time

import pytest
from fastapi.testclient import TestClient
from pydantic import ValidationError
from tt_hub.app import create_app
from tt_hub.hub import Hub
from tt_hub.latest import Latest
from tt_hub.messages import Command, Telemetry


# ---------- messages ----------
def test_command_clamps_and_defaults():
    c = Command.model_validate_json('{"vx": 5, "vy": -9.5, "w": 0.25}')
    assert (c.vx, c.vy, c.w, c.hold) == (1.0, -1.0, 0.25, False)


def test_command_missing_fields_are_zero():
    c = Command.model_validate_json("{}")
    assert (c.vx, c.vy, c.w, c.hold) == (0.0, 0.0, 0.0, False)


@pytest.mark.parametrize(
    "bad",
    [
        '{"vx": "fast"}',
        '{"vx": true}',
        '{"hold": "yes"}',
        '{"hold": 1}',
        "[1]",
        "nope",
        '{"w": null}',
    ],
)
def test_command_rejects_malformed(bad):
    with pytest.raises(ValidationError):
        Command.model_validate_json(bad)


def test_command_nan_becomes_zero():
    assert Command.model_validate_json('{"vx": NaN}').vx == 0.0


def test_command_extra_fields_ignored():
    assert Command.model_validate_json('{"vx": 0.5, "evil": 1}').vx == 0.5


def test_telemetry_parses_firmware_frame():
    t = Telemetry.model_validate_json(
        '{"vbat":7.62,"yaw":12.3,"estop":false,"batt":"ok","alive":true}'
    )
    assert t.vbat == 7.62 and t.alive


# ---------- latest-only queue ----------
def test_latest_keeps_only_newest():
    async def run():
        q: Latest[int] = Latest()
        for i in range(10):
            q.put(i)
        return await q.get()

    assert asyncio.run(run()) == 9


# ---------- hub staleness ----------
def test_stale_command_is_not_forwarded():
    now = [100.0]
    hub = Hub(stale_s=0.15, clock=lambda: now[0])
    assert hub.fresh_command() is None
    hub.set_command(Command(vx=0.5))
    assert hub.fresh_command() is not None
    now[0] += 0.14
    assert hub.fresh_command() is not None
    now[0] += 0.02
    assert hub.fresh_command() is None


def test_clear_command():
    hub = Hub()
    hub.set_command(Command(vx=1))
    hub.clear_command()
    assert hub.fresh_command() is None


# ---------- end to end over WebSockets ----------
@pytest.fixture
def hub():
    return Hub(send_hz=200)


@pytest.fixture
def client(hub):
    # `with` keeps ONE event loop for all connections (needed for asyncio queues).
    with TestClient(create_app(hub)) as c:
        yield c


def test_command_reaches_robot_clamped_and_telemetry_reaches_ui(hub, client):
    with client.websocket_connect("/ws/robot") as robot, client.websocket_connect("/ws/ui") as ui:
        first = ui.receive_json()  # initial snapshot
        assert "robot_connected" in first

        robot.send_text('{"vbat":7.5,"yaw":1.0,"estop":false,"batt":"ok","alive":true}')
        # drain until the UI sees telemetry
        seen = None
        for _ in range(5):
            msg = ui.receive_json()
            if msg["telemetry"]:
                seen = msg
                break
        assert seen and seen["robot_connected"] and seen["telemetry"]["vbat"] == 7.5

        ui.send_text('{"vx": 3, "vy": -0.5, "w": 0, "hold": true}')
        got = robot.receive_json()
        assert got == {"vx": 1.0, "vy": -0.5, "w": 0.0, "hold": True}


def test_bad_ui_frame_is_ignored_and_connection_survives(hub, client):
    with client.websocket_connect("/ws/ui") as ui:
        ui.receive_json()
        ui.send_text("garbage")
        ui.send_text('{"vx": "x"}')
        ui.send_text('{"vx": 0.2}')
        # still alive: the valid command was stored
        for _ in range(100):
            if hub.fresh_command() is not None:
                break
            time.sleep(0.01)
        assert hub.fresh_command().vx == 0.2


def test_ui_disconnect_clears_command(hub, client):
    with client.websocket_connect("/ws/ui") as ui:
        ui.receive_json()
        ui.send_text('{"vx": 0.5}')
        for _ in range(100):
            if hub.fresh_command() is not None:
                break
            time.sleep(0.01)
        assert hub.fresh_command() is not None
    for _ in range(100):
        if hub.fresh_command() is None:
            break
        time.sleep(0.01)
    assert hub.fresh_command() is None


def test_robot_disconnect_updates_state(hub, client):
    with client.websocket_connect("/ws/robot"):
        assert hub.robot_connected
    for _ in range(100):
        if not hub.robot_connected:
            break
        time.sleep(0.01)
    assert not hub.robot_connected
    assert hub.telemetry is None
