"use client";

import { useRef, useState } from "react";
import { offsetToAxes, offsetToAxis, type Axes } from "../lib/joystick";

/** Round 2-D stick. Springs back to centre on release or cancel. */
export function Stick({ onChange }: { onChange: (a: Axes) => void }) {
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState<Axes>({ x: 0, y: 0 });

  const update = (e: React.PointerEvent) => {
    const r = ref.current!.getBoundingClientRect();
    const radius = r.width / 2;
    const a = offsetToAxes(e.clientX - (r.left + radius), e.clientY - (r.top + radius), radius);
    setPos(a);
    onChange(a);
  };
  const release = () => {
    setPos({ x: 0, y: 0 });
    onChange({ x: 0, y: 0 });
  };

  return (
    <div
      ref={ref}
      className="stick"
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId);
        update(e);
      }}
      onPointerMove={(e) => e.buttons !== 0 && update(e)}
      onPointerUp={release}
      onPointerCancel={release}
      onLostPointerCapture={release}
    >
      <div className="knob" style={{ transform: `translate(${pos.x * 85}%, ${pos.y * 85}%)` }} />
    </div>
  );
}

/** Horizontal bar for rotation. Left is counter-clockwise. */
export function Bar({ onChange }: { onChange: (v: number) => void }) {
  const ref = useRef<HTMLDivElement>(null);
  const [v, setV] = useState(0);

  const update = (e: React.PointerEvent) => {
    const r = ref.current!.getBoundingClientRect();
    const half = r.width / 2 - 28;
    const a = offsetToAxis(e.clientX - (r.left + r.width / 2), half);
    setV(a);
    onChange(a);
  };
  const release = () => {
    setV(0);
    onChange(0);
  };

  return (
    <div
      ref={ref}
      className="bar"
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId);
        update(e);
      }}
      onPointerMove={(e) => e.buttons !== 0 && update(e)}
      onPointerUp={release}
      onPointerCancel={release}
      onLostPointerCapture={release}
    >
      <div className="knob" style={{ transform: `translateX(${v * (ref.current ? ref.current.clientWidth / 2 - 28 : 0)}px)` }} />
    </div>
  );
}
