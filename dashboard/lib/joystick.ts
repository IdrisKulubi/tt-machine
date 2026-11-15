// Pure joystick math. No DOM, so it is unit tested.

export interface Axes {
  x: number; // right is +
  y: number; // down is +
}

export interface Command {
  vx: number;
  vy: number;
  w: number;
  hold: boolean;
}

export const DEADZONE = 0.08;

export function clamp1(v: number): number {
  if (Number.isNaN(v)) return 0;
  return Math.max(-1, Math.min(1, v));
}

/** Pointer offset from the stick centre (pixels) to axes in the unit circle, with a deadzone. */
export function offsetToAxes(dx: number, dy: number, radius: number, deadzone = DEADZONE): Axes {
  if (radius <= 0) return { x: 0, y: 0 };
  let x = dx / radius;
  let y = dy / radius;
  const len = Math.hypot(x, y);
  if (len > 1) {
    x /= len;
    y /= len;
  }
  if (Math.min(len, 1) < deadzone) return { x: 0, y: 0 };
  return { x: clamp1(x), y: clamp1(y) };
}

/** Horizontal offset to a single axis in [-1, 1] with a deadzone. */
export function offsetToAxis(dx: number, halfWidth: number, deadzone = DEADZONE): number {
  if (halfWidth <= 0) return 0;
  const v = clamp1(dx / halfWidth);
  return Math.abs(v) < deadzone ? 0 : v;
}

const round3 = (v: number) => Math.round(v * 1000) / 1000 + 0; // + 0 turns -0 into 0

/**
 * Movement stick up = forward (+vx), left = +vy.
 * Rotation bar left = counter-clockwise (+w).
 */
export function buildCommand(move: Axes, rot: number, hold: boolean): Command {
  return {
    vx: round3(clamp1(-move.y)),
    vy: round3(clamp1(-move.x)),
    w: round3(clamp1(-rot)),
    hold,
  };
}

export const ZERO_COMMAND: Command = { vx: 0, vy: 0, w: 0, hold: false };
