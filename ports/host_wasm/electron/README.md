# MMBasic Anywhere Desktop

Electron wrapper for the `ports/host_wasm/web` WebAssembly app.

Build the WASM bundle and package a self-contained macOS app:

```sh
cd ports/host_wasm/electron
npm install
npm run dist
```

This requires Node.js 22.12.0 or newer.

For local development without creating a distributable:

```sh
npm run build:wasm
npm start
```

The app embeds the generated `../web` directory as Electron resources. At
runtime the main process serves those files from `127.0.0.1:43185` inside the
app and adds the COOP/COEP headers required by the pthreads WebAssembly build.
The fixed origin keeps browser storage stable across launches. Set
`MMBA_DESKTOP_PORT` to override the port.
