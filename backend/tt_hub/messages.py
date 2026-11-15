"""Message models. Commands are clamped here too, but the firmware is the final authority."""

from __future__ import annotations

import math
from typing import Annotated

from pydantic import BaseModel, BeforeValidator, ConfigDict, StrictBool


def clamp_unit(value: object) -> float:
    """Clamp a number to [-1, 1]. NaN becomes 0. Non-numbers raise ValueError."""
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise ValueError("expected a number")
    v = float(value)
    if math.isnan(v):
        return 0.0
    return max(-1.0, min(1.0, v))


Unit = Annotated[float, BeforeValidator(clamp_unit)]


class Command(BaseModel):
    """UI -> hub -> robot. Unitless values in [-1, 1]."""

    model_config = ConfigDict(extra="ignore")

    vx: Unit = 0.0
    vy: Unit = 0.0
    w: Unit = 0.0
    hold: StrictBool = False

    def to_wire(self) -> dict[str, float | bool]:
        return {"vx": self.vx, "vy": self.vy, "w": self.w, "hold": self.hold}


class Telemetry(BaseModel):
    """Robot -> hub -> UI."""

    model_config = ConfigDict(extra="ignore")

    vbat: float
    yaw: float = 0.0
    estop: bool = False
    batt: str = "ok"  # ok | limited | low
    alive: bool = False  # robot is receiving commands
