/*
 * EconetWiFi
 * Copyright (c) 2025 Paul G. Banks <https://paulbanks.org/projects/econet>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the LICENSE file in the project root for full license information.
 */

import { connectionState, econetStats, aunbridgeStats, addLog } from "./stores";
import type { ClientMessage, ServerMessage } from "./types";

let socket: WebSocket | null = null;
let nextRequestId = 1;
let shouldReconnect = true;
let pendingRestart = false;
let reconnectTimer: number | null = null;
let pingTimer: number | null = null;
let lastPongAt = 0;
const PING_INTERVAL_MS = 5000;
const PONG_TIMEOUT_MS = 12000;
const RECONNECT_DELAY_MS = 1000;
const RESTART_DELAY_MS = 5000;
const pending = new Map<
  number,
  { resolve: (v: any) => void; reject: (e: any) => void }
>();

function scheduleReconnect(delayMs: number) {
  if (reconnectTimer) return;
  reconnectTimer = window.setTimeout(() => {
    reconnectTimer = null;
    getWebSocket();
  }, delayMs);
}

function clearReconnectTimer() {
  if (reconnectTimer) {
    window.clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
}

function startPingLoop() {
  if (pingTimer) {
    window.clearInterval(pingTimer);
  }
  pingTimer = window.setInterval(() => {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      return;
    }

    const now = Date.now();
    if (lastPongAt && now - lastPongAt > PONG_TIMEOUT_MS) {
      pendingRestart = false;
      socket.close();
      return;
    }

    socket.send(JSON.stringify({ type: "ping", id: 0 }));
  }, PING_INTERVAL_MS);
}

function stopPingLoop() {
  if (pingTimer) {
    window.clearInterval(pingTimer);
    pingTimer = null;
  }
}

function handleSocketClosed() {
  stopPingLoop();
  socket = null;
  connectionState.set("connecting");

  for (const { reject } of pending.values()) {
    reject(new Error("WebSocket closed"));
  }
  pending.clear();

  if (!shouldReconnect) {
    connectionState.set("disconnected");
    return;
  }

  const delay = pendingRestart ? RESTART_DELAY_MS : RECONNECT_DELAY_MS;
  pendingRestart = false;
  scheduleReconnect(delay);
}

function handleRestartingMessage() {
  pendingRestart = true;
  connectionState.set("connecting");
  if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
    socket.close();
  } else {
    scheduleReconnect(RESTART_DELAY_MS);
  }
}

export function closeWebSocket() {
  shouldReconnect = false;
  pendingRestart = false;
  clearReconnectTimer();
  stopPingLoop();
  connectionState.set("disconnected");
  if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
    socket.close();
  }
  socket = null;
}  

export function getWebSocket() {

  if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
    return socket;
  }

  connectionState.set("connecting");
  shouldReconnect = true;

  socket = new WebSocket(`ws://${location.host}/ws`);

  socket.addEventListener("open", () => {
    connectionState.set("connected");
    lastPongAt = Date.now();
    startPingLoop();
  });

  socket.addEventListener("close", () => {
    handleSocketClosed();
  });

  socket.addEventListener("message", (event) => {
    let msg: ServerMessage;

    try {
      msg = JSON.parse(event.data);
    } catch (e) {
      console.error("Invalid WS message", e, event.data);
      return;
    }

    handleMessage(msg);
  });

  return socket;
}

function handleMessage(msg: ServerMessage) {

  if (msg.type === "pong") {
    lastPongAt = Date.now();
    return;
  }

  if (msg.type === "restarting") {
    handleRestartingMessage();
    return;
  }

  if (msg.type === "stats_stream") {

    if (msg.aunbridge_stats) {
      aunbridgeStats.update((s) => ({ ...s, ...msg.aunbridge_stats }));
    }

    if (msg.econet_stats) {
      econetStats.update((s) => ({ ...s, ...msg.econet_stats }));
    }
  }

  if (msg.type === "log") {
    addLog(msg.line);
  }

  if (msg.type === "response") {
    if (pending.has(msg.id)) {
      const { resolve } = pending.get(msg.id)!;
      pending.delete(msg.id);
      resolve(msg);
    }
  }
}

export function sendWsRequest(message: Record<string, any>): Promise<any> {
  const ws = getWebSocket();
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    return Promise.reject(new Error("WebSocket not connected"));
  }

  const id = nextRequestId++;
  const msgWithId = { ...message, id };

  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify(msgWithId));
  });
}
