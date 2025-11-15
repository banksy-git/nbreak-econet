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

import { connectionState, device, econetStats, aunbridgeStats, addLog } from "./stores";
import type { ClientMessage, ServerMessage } from "./types";

let socket: WebSocket | null = null;
let nextRequestId = 1;
const pending = new Map<
  number,
  { resolve: (v: any) => void; reject: (e: any) => void }
>();


export function closeWebSocket() {
  if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
    socket.close();
    socket = null;
  }
}  

export function getWebSocket() {

  if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
    return socket;
  }

  connectionState.set("connecting");

  socket = new WebSocket(`ws://${location.host}/ws`);

  socket.addEventListener("open", () => {
    connectionState.set("connected");
  });

  socket.addEventListener("close", () => {
    connectionState.set("disconnected");

    for (const { reject } of pending.values()) {
      reject(new Error("WebSocket closed"));
    }
    pending.clear();
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


