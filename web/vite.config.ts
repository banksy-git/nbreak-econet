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

import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import { viteSingleFile } from "vite-plugin-singlefile";
import tailwindcss from "@tailwindcss/vite";
import { mockWsPlugin } from "./mockserver";
import { execSync } from "node:child_process";

export function getGitVersion(): string {
  return execSync("git describe --tags --dirty --always --long")
    .toString()
    .trim();
}

export default defineConfig({
  plugins: [tailwindcss(), svelte(), viteSingleFile(), mockWsPlugin()],
  build: {
    outDir: "../fsroot/web",
  },
  define: {
    __GIT_COMMIT__: JSON.stringify(getGitVersion()),
  },
});
