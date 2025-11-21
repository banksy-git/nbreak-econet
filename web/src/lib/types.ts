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

export type EconetStats = {
  rx_frame_count: number;
  rx_crc_fail_count: number;
  rx_short_frame_count: number;
  rx_abort_count: number;
  rx_oversize_count: number;
  rx_ack_count: number;
  rx_nack_count: number;
  tx_frame_count: number;
  tx_ack_count: number;
};

export type AunbridgeStats = {
  tx_count: number;
  tx_retry_count: number;
  tx_abort_count: number;
  tx_error_count: number;
  tx_ack_count: number;
  tx_nack_count: number;
  rx_data_count: number;
  rx_ack_count: number;
  rx_nack_count: number;
  rx_unknown_count: number;
};

export type WifiSettings = {
  ssid: string;
  password: string;
};

export type WifiApSettings = {
  ssid: string;
  password: string;
  enabled: boolean;
};

export interface ECSRow {
  station_id: number;
  udp_port: number;
};

export interface AUNRow {
  remote_ip: string;
  udp_port: number;
  station_id: number;
};

export type EconetSettings = {
  econetStations: ECSRow[];
  aunStations: AUNRow[];
};


export type StatsStreamPayload = {
  aunbridge_stats?: Partial<AunbridgeStats>;
  econet_stats?: Partial<EconetStats>;
};

export type ServerMessage =
  | ({ type: "stats_stream" } & StatsStreamPayload)
  | { type: "log"; line: string }
  |({ type: "response"; id: number } & Record<string, any>);

export type ClientMessage =
  | { type: "save_wifi"; id: number, settings: WifiSettings }
  | { type: "save_wifi_ap"; id: number, settings: WifiApSettings }
  | { type: "save_econet"; id: number, settings: EconetSettings }
  | { type: "get_wifi"; id: number }
  | { type: "get_econet"; id: number }
  | { type: "get_wifi_ap"; id: number }
  | { type: "reboot"; id: number }
  | { type: "factory_reset"; id: number };
