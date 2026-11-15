"""FastAPI hub. Run from backend/:  uvicorn tt_hub.app:app --host 0.0.0.0 --port 8000"""

from __future__ import annotations

import asyncio
import contextlib
import json
import logging

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from pydantic import ValidationError

from .hub import Hub
from .latest import Latest
from .messages import Command, Telemetry

log = logging.getLogger("tt_hub")


def create_app(hub: Hub | None = None) -> FastAPI:
    hub = hub or Hub()
    app = FastAPI(title="TT-Coach hub")
    app.state.hub = hub
    robot_ws: list[WebSocket] = []  # at most one robot

    @app.get("/health")
    async def health() -> dict[str, bool]:
        return {"ok": True, "robot_connected": hub.robot_connected}

    async def send_commands(ws: WebSocket) -> None:
        """Forward the freshest UI command to the robot at a fixed rate.
        Stale or absent command: send nothing, so the firmware watchdog stops the robot."""
        while True:
            cmd = hub.fresh_command()
            if cmd is not None:
                await ws.send_text(json.dumps(cmd.to_wire()))
            await asyncio.sleep(hub.send_period)

    @app.websocket("/ws/robot")
    async def robot_endpoint(ws: WebSocket) -> None:
        await ws.accept()
        if robot_ws:
            with contextlib.suppress(Exception):
                await robot_ws[0].close(code=1012)
            robot_ws.clear()
        robot_ws.append(ws)
        hub.set_robot_connected(True)
        log.info("robot connected")
        sender = asyncio.create_task(send_commands(ws))
        try:
            while True:
                text = await ws.receive_text()
                try:
                    hub.set_telemetry(Telemetry.model_validate_json(text))
                except ValidationError:
                    log.warning("bad telemetry ignored")
        except WebSocketDisconnect:
            pass
        finally:
            sender.cancel()
            if ws in robot_ws:
                robot_ws.remove(ws)
                hub.set_robot_connected(False)
            log.info("robot disconnected")

    async def push_to_ui(ws: WebSocket, sub: Latest) -> None:
        while True:
            await ws.send_json(await sub.get())

    @app.websocket("/ws/ui")
    async def ui_endpoint(ws: WebSocket) -> None:
        await ws.accept()
        sub: Latest = Latest()
        hub.subscribers.add(sub)
        sub.put(hub.snapshot())
        pusher = asyncio.create_task(push_to_ui(ws, sub))
        try:
            while True:
                text = await ws.receive_text()
                try:
                    hub.set_command(Command.model_validate_json(text))
                except ValidationError:
                    log.warning("bad command ignored")
        except WebSocketDisconnect:
            pass
        finally:
            pusher.cancel()
            hub.subscribers.discard(sub)
            hub.clear_command()  # UI gone: stop forwarding, the robot's watchdog stops it
            log.info("ui disconnected")

    return app


app = create_app()
