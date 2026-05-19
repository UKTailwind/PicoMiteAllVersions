# host_wasm

Browser-hosted MMBasic build. The port uses the common runtime spine for boot,
source loading, console, and abort/service helpers, while `host_wasm_*.c` owns
browser entry policy, JS input, canvas/audio bridges, and MEMFS/IDBFS details.

Build from the repository root:

```sh
./ports/host_wasm/build.sh
```

The script first uses `emcc` already on `PATH`. If it is missing, it sources
`$HOME/emsdk/emsdk_env.sh` and retries. On this workspace that resolves to
`/Users/joshv/emsdk/emsdk_env.sh`. The output is
`ports/host_wasm/web/picomite.{mjs,wasm,data}`.

## Desktop app

Latest CI-built desktop apps are linked from:
https://github.com/jvanderberg/PicoMiteAllVersions/releases/tag/desktop-latest

The web bundle can be packaged as a self-contained Electron app:

```sh
cd ports/host_wasm/electron
npm install
npm run dist
```

This requires Node.js 22.12.0 or newer. The packaged app includes the generated
`web/` assets and runs them in an Electron window with the COOP/COEP headers
required by the pthreads WASM build.
