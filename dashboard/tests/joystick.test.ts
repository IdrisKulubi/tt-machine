import { describe, expect, it } from "vitest";
import { buildCommand, clamp1, offsetToAxes, offsetToAxis } from "../lib/joystick";

describe("clamp1", () => {
  it("clamps and handles NaN", () => {
    expect(clamp1(5)).toBe(1);
    expect(clamp1(-5)).toBe(-1);
    expect(clamp1(0.3)).toBe(0.3);
    expect(clamp1(NaN)).toBe(0);
  });
});

describe("offsetToAxes", () => {
  it("is zero inside the deadzone and at zero radius", () => {
    expect(offsetToAxes(1, 1, 100)).toEqual({ x: 0, y: 0 });
    expect(offsetToAxes(50, 50, 0)).toEqual({ x: 0, y: 0 });
  });
  it("scales linearly inside the circle", () => {
    expect(offsetToAxes(50, 0, 100)).toEqual({ x: 0.5, y: 0 });
  });
  it("never exceeds the unit circle", () => {
    const a = offsetToAxes(500, 500, 100);
    expect(Math.hypot(a.x, a.y)).toBeLessThanOrEqual(1.0000001);
    expect(a.x).toBeCloseTo(Math.SQRT1_2, 5);
  });
});

describe("offsetToAxis", () => {
  it("clamps and applies the deadzone", () => {
    expect(offsetToAxis(1, 100)).toBe(0);
    expect(offsetToAxis(-300, 100)).toBe(-1);
    expect(offsetToAxis(50, 100)).toBe(0.5);
  });
});

describe("buildCommand", () => {
  it("maps stick up to forward and left to +vy", () => {
    expect(buildCommand({ x: 0, y: -1 }, 0, false)).toEqual({ vx: 1, vy: 0, w: 0, hold: false });
    expect(buildCommand({ x: -1, y: 0 }, 0, false)).toEqual({ vx: 0, vy: 1, w: 0, hold: false });
  });
  it("maps rotation bar left to +w (counter-clockwise)", () => {
    expect(buildCommand({ x: 0, y: 0 }, -1, true)).toEqual({ vx: 0, vy: 0, w: 1, hold: true });
  });
  it("never produces values outside [-1, 1] or -0", () => {
    const c = buildCommand({ x: 9, y: 9 }, 9, false);
    for (const v of [c.vx, c.vy, c.w]) expect(Math.abs(v)).toBeLessThanOrEqual(1);
    expect(Object.is(buildCommand({ x: 0, y: 0 }, 0, false).vx, -0)).toBe(false);
  });
});
