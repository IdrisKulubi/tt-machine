"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { Bar, Stick } from "../components/Pad";
import { buildCommand, ZERO_COMMAND, type Axes } from "../lib/joystick";

interface Telemetry {
  vbat: number;
  yaw: number;
  estop: boolean;
  batt: string;
  alive: boolean;
}
interface HubState {
  robot_connected: boolean;
  telemetry: Telemetry | null;
}

const SEND_MS = 50; // 20 Hz, always sent (zeros when idle) so the link stays alive

function hubUrl(): string {
  const env = process.env.NEXT_PUBLIC_HUB_URL;
  if (env) return env;
  // Same machine as the page, port 8000. Phone and laptop share the network.
  return `ws://${window.location.hostname}:8000/ws/ui`;
}

export default function Home() {
  const [connected, setConnected] = useState(false);
  const [state, setState] = useState<HubState>({ robot_connected: false, telemetry: null });
  const [hold, setHold] = useState(false);

  const wsRef = useRef<WebSocket | null>(null);
  const move = useRef<Axes>({ x: 0, y: 0 });
  const rot = useRef(0);
  const holdRef = useRef(false);

  const zero = useCallback(() => {
    move.current = { x: 0, y: 0 };
    rot.current = 0;
  }, []);

  // Connection with automatic reconnect.
  useEffect(() => {
    let closed = false;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const connect = () => {
      const ws = new WebSocket(hubUrl());
      wsRef.current = ws;
      ws.onopen = () => setConnected(true);
      ws.onmessage = (ev) => {
        try {
          setState(JSON.parse(ev.data) as HubState);
        } catch {
          /* ignore */
        }
      };
      ws.onclose = () => {
        setConnected(false);
        zero();
        if (!closed) timer = setTimeout(connect, 500);
      };
      ws.onerror = () => ws.close();
    };
    connect();
    return () => {
      closed = true;
      if (timer) clearTimeout(timer);
      wsRef.current?.close();
    };
  }, [zero]);

  // Steady command stream.
  useEffect(() => {
    const id = setInterval(() => {
      const ws = wsRef.current;
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      ws.send(JSON.stringify(buildCommand(move.current, rot.current, holdRef.current)));
    }, SEND_MS);
    return () => clearInterval(id);
  }, []);

  // Leaving the page or tab means stop.
  useEffect(() => {
    const stop = () => zero();
    document.addEventListener("visibilitychange", stop);
    window.addEventListener("blur", stop);
    return () => {
      document.removeEventListener("visibilitychange", stop);
      window.removeEventListener("blur", stop);
    };
  }, [zero]);

  const t = state.telemetry;
  const batt = t ? `${t.vbat.toFixed(2)} V (${t.batt})` : "no data";
  const battClass = !t ? "pill" : t.batt === "ok" ? "pill ok" : t.batt === "limited" ? "pill warn" : "pill bad";

  return (
    <main className="page">
      <div className="status">
        <span className={connected ? "pill ok" : "pill bad"}>{connected ? "Hub linked" : "Hub offline"}</span>
        <span className={state.robot_connected ? "pill ok" : "pill bad"}>
          {state.robot_connected ? "Robot linked" : "Robot offline"}
        </span>
        <span className={battClass}>{batt}</span>
        {t?.estop && <span className="pill bad">E-STOP</span>}
        {t && !t.alive && state.robot_connected && <span className="pill warn">Robot stopped</span>}
        {t?.batt === "low" && <span className="pill bad">Battery low: driving refused</span>}
      </div>

      <div className="controls">
        <Stick onChange={(a) => (move.current = a)} />
        <Bar onChange={(v) => (rot.current = v)} />
      </div>

      <div className="row">
        <button
          className={hold ? "on" : ""}
          onClick={() => {
            holdRef.current = !holdRef.current;
            setHold(holdRef.current);
          }}
        >
          Heading hold: {hold ? "ON" : "OFF"}
        </button>
        <button
          className="stop"
          onClick={() => {
            zero();
            wsRef.current?.send(JSON.stringify(ZERO_COMMAND));
          }}
        >
          STOP
        </button>
      </div>
    </main>
  );
}
