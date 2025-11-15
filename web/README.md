# EconetWifi - Embedded Web Interface

Author: Paul G. Banks <https://paulbanks.org/>
Licensed under GPL-3, see the LICENSE file in the project root for full license information.

## Developing

To start a development web server with mock environment to test locally without hardware :

	pnpm dev

The mock server code is a Vite plugin and located in mockserver.ts alongside this file.

## Building

To build the web interface for embedding:

   	pnpm build --emptyOutDir

