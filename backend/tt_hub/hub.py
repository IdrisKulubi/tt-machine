"""Hub state: the newest command from the UI and the newest telemetry from the robot."""

from __future__ import annotations

import time
from collections.abc import Callable
from typing import Any

from .latest import Latest
from .messages import Command, Telemetry

# A UI command older than this is not forwarded. The robot then hears silence and its
# own watchdog (300 ms) stops the motors. The firmware remains the safety authority.
DEFAULT_STALE_S = 0.15
DEFAULT_SEND_HZ = 50


class Hub:
    def __init__(
        self,
        stale_s: float = DEFAULT_STALE_S,
        send_hz: float = DEFAULT_SEND_HZ,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self.stale_s = stale_s
        self.send_period = 1.0 / send_hz
        self._clock = clock
        self._command: Command | None = None
        self._command_at = 0.0
        self.telemetry: Telemetry | None = None
        self.robot_connected = False
        self.subscribers: set[Latest[dict[str, Any]]] = set()

    # ---- commands (UI -> robot) ----
    def set_command(self, cmd: Command) -> None:
        self._command = cmd
        self._command_at = self._clock()

    def clear_command(self) -> None:
        self._command = None

    def fresh_command(self) -> Command | None:
        """The command to forward now, or None if there is none or it is stale."""
        if self._command is None:
            return None
        if self._clock() - self._command_at > self.stale_s:
            return None
        return self._command

    # ---- state (robot -> UI) ----
    def snapshot(self) -> dict[str, Any]:
        return {
            "robot_connected": self.robot_connected,
            "telemetry": self.telemetry.model_dump() if self.telemetry else None,
        }

    def publish(self) -> None:
        snap = self.snapshot()
        for sub in self.subscribers:
            sub.put(snap)

    def set_telemetry(self, t: Telemetry) -> None:
        self.telemetry = t
        self.publish()

    def set_robot_connected(self, connected: bool) -> None:
        self.robot_connected = connected
        if not connected:
            self.telemetry = None
        self.publish()
