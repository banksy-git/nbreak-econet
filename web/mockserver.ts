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

import { type PluginOption } from "vite";
import { WebSocketServer, type WebSocket } from "ws";
import type { IncomingMessage } from "http";
import type { Socket } from "node:net";
import type { Buffer } from "node:buffer";
import type {
  AunbridgeStats,
  ClientMessage,
  EconetStats,
  ServerMessage,
} from "./src/lib/types";

export function mockWsPlugin(): PluginOption {

    let wss: WebSocketServer | undefined;
  
    return {
      name: "mock-ws-plugin",
  
      configureServer(server) {
        // Create WS server without its own HTTP server
        //  - we'll attach it to Vite's
        wss = new WebSocketServer({ noServer: true });
  
        server.httpServer?.on(
          "upgrade",
          (req: IncomingMessage, socket: Socket, head: Buffer) => {
            if (!req.url) return;
  
            // Only handle /ws upgrade requests
            if (req.url === "/ws") {
              wss?.handleUpgrade(req, socket, head, (ws: WebSocket) => {
                wss?.emit("connection", ws, req);
              });
            }
          }
        );
  
        // WebSocket behavior
        wss.on("connection", (ws: WebSocket) => {
          console.log("Mock WS client connected");
  
          let uptime = 0;
  
          let aun: AunbridgeStats = {
            tx_count: 0,
            tx_retry_count: 0,
            tx_abort_count: 0,
            tx_error_count: 0,
            rx_data_count: 0,
            tx_ack_count: 0,
            tx_nack_count: 0,
            rx_ack_count: 0,
            rx_nack_count: 0,
            rx_unknown_count: 0,
          };
  
          let eco: EconetStats = {
            rx_frame_count: 0,
            rx_crc_fail_count: 0,
            rx_short_frame_count: 0,
            rx_abort_count: 0,
            rx_oversize_count: 0,
            rx_ack_count: 0,
            rx_nack_count: 0,
            tx_frame_count: 0,
            tx_ack_count: 0,
          };
  
          function inc(v: number, spread = 5) {
            return v + Math.floor(Math.random() * spread);
          }
  
          const stateInterval = setInterval(() => {
            uptime++;
  
            // Update stats
            aun = {
              tx_count: inc(aun.tx_count, 10),
              tx_retry_count: inc(aun.tx_retry_count, 2),
              tx_abort_count: inc(aun.tx_abort_count, 1),
              tx_error_count: inc(aun.tx_error_count, 1),
              tx_ack_count: inc(aun.tx_abort_count, 1),
              tx_nack_count: inc(aun.tx_error_count, 1),
              rx_data_count: inc(aun.rx_data_count, 15),
              rx_ack_count: inc(aun.rx_ack_count, 15),
              rx_nack_count: inc(aun.rx_nack_count, 2),
              rx_unknown_count: inc(aun.rx_unknown_count, 1),
            };
  
            eco = {
              rx_frame_count: inc(eco.rx_frame_count, 20),
              rx_crc_fail_count: inc(eco.rx_crc_fail_count, 1),
              rx_short_frame_count: inc(eco.rx_short_frame_count, 1),
              rx_abort_count: inc(eco.rx_abort_count, 1),
              rx_oversize_count: inc(eco.rx_oversize_count, 1),
              rx_ack_count: inc(eco.rx_nack_count, 2),
              rx_nack_count: inc(eco.rx_nack_count, 2),
              tx_frame_count: inc(eco.tx_frame_count, 20),
              tx_ack_count: inc(eco.tx_ack_count, 20),
            };
  
            let ssp: ServerMessage = {
              type: "stats_stream",
              payload: {
                aunbridge_stats: aun,
                econet_stats: eco,
              },
            };
            ws.send(JSON.stringify(ssp));
          }, 1000);
  
          const logInterval = setInterval(() => {
            ws.send(
              JSON.stringify({
                type: "log",
                line: `[mock] ${new Date().toLocaleTimeString()} – simulated log entry`,
              })
            );
          }, 3000);
  
          ws.on("message", (msg_raw: Buffer) => {
            let msg: ClientMessage = JSON.parse(msg_raw.toString());
  
            console.log("UI sent →", msg);
  
            if (msg.type == "save_wifi") {
              let response: ServerMessage = {
                type: "response",
                id: msg.id,
                error: "No WiFi for you",
              };
              ws.send(JSON.stringify(response));
            }
  
            if (msg.type == "get_wifi") {
              let response: ServerMessage = {
                type: "response",
                id: msg.id,
                ok: true,
                settings: { ssid: "the.internet", password: "" },
              };
              ws.send(JSON.stringify(response));
            }
  
            if (msg.type == "save_econet") {
              let response: ServerMessage = {
                type: "response",
                id: msg.id,
                error: "No saving for you",
              };
              ws.send(JSON.stringify(response));
            }
  
            if (msg.type == "get_econet") {
              let response: ServerMessage = {
                type: "response",
                id: msg.id,
                ok: true,
                settings: {
                  remote_station_id: "127",
                  this_station_id: "254",
                  server_ip: "192.168.0.10",
                  server_port: "32768",
                },
              };
              ws.send(JSON.stringify(response));
            }
  
            if (msg.type == "save_wifi_ap") {
              let response: ServerMessage = {
                type: "response",
                id: msg.id,
                error: "No WiFi AP for you",
              };
              ws.send(JSON.stringify(response));
            }
  
            if (msg.type == "get_wifi_ap") {
              let response: ServerMessage = {
                type: "response",
                id: msg.id,
                ok: true,
                settings: {
                  ssid: "n-break-econet",
                  password: "",
                  enabled: true,
                },
              };
              ws.send(JSON.stringify(response));
            }
  
            if (msg.type == "reboot") {
              let response: ServerMessage = {
                type: "response",
                id: msg.id,
                error: "Can't reboot the mock!",
              };
              ws.send(JSON.stringify(response));
            }
  
            if (msg.type == "factory_reset") {
              let response: ServerMessage = {
                type: "response",
                id: msg.id,
                error: "Can't factory reset the mock!",
              };
              ws.send(JSON.stringify(response));
            }
          });
  
          ws.on("close", () => {
            clearInterval(stateInterval);
            clearInterval(logInterval);
          });
        });
      },
    };
  }