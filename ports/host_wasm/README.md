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
`host/web/picomite.{mjs,wasm,data}`.
