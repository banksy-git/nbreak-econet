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

import type { Component } from "svelte";
import { writable } from "svelte/store";
import type { EconetStats, AunbridgeStats } from "./types";

export const activePage = writable<Component>();

export type ConnectionState = "connecting" | "connected" | "disconnected";

export const connectionState = writable<ConnectionState>("connecting");

export const device = writable({
  model: "",
  online: false,
  uptime: 0,
  temperature: 0,
  voltage: 0,
  firmware: "",
});

export const econetStats = writable<EconetStats>({
  rx_frame_count: 0,
  rx_crc_fail_count: 0,
  rx_short_frame_count: 0,
  rx_abort_count: 0,
  rx_oversize_count: 0,
  rx_ack_count: 0,
  rx_nack_count: 0,
  tx_frame_count: 0,
  tx_ack_count: 0,
});

export const aunbridgeStats = writable<AunbridgeStats>({
  tx_count: 0,
  tx_retry_count: 0,
  tx_abort_count: 0,
  tx_error_count: 0,
  tx_ack_count: 0,
  tx_nack_count: 0,
  rx_data_count: 0,
  rx_ack_count: 0,
  rx_nack_count: 0,
  rx_unknown_count: 0,
});

export type LogLevel = "info" | "warn" | "error" | "other";
export interface LogEntry {
  level: LogLevel;
  line: string;
}
const MAX_LOGS = 200;
export const logs = writable<LogEntry[]>([]);
function detectLevel(line: string): LogLevel {
  if (line.startsWith("E ")) return "error";
  if (line.startsWith("W ")) return "warn";
  if (line.startsWith("I ")) return "info";
  return "other";
}
export function addLog(line: string) {
  const entry: LogEntry = { level: detectLevel(line), line };
  logs.update((ls) => {
    const next = [...ls, entry];
    if (next.length > MAX_LOGS) next.shift(); // drop oldest
    return next;
  });
}
