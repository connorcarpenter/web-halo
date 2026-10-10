# Browser command generation

Generate the existing browser Ninja target without executing Python:

```sh
sh configure.sh --web --release --web-cc /path/to/emcc
ninja -t commands build/web/obj/port/web/src/web_online_ui.o
```

The configuration entry point atomically installs the generated graph only after
successful generation. Native configuration remains unchanged. Bash and jq read
the existing project/port configuration; both semantics headers use the existing
qualified Node semantics generator. The graph retains platform/game/libc source
categories, depfiles, response-file linking, WebGL2/pthreads/WasmFS settings,
browser adapters and UI asset copies. Authored campaign units join the game list.

`bash tools/tests/web_build_contract.sh` checks graph contracts and parses it with
Ninja without executing compiler commands. This is command-generation evidence,
not browser C compilation, browser gameplay, or complete launcher migration.

## Unresolved execution boundary

The custom SDL port remains `port/web/halo_sdl3.py`, pinned to SDL 3.4.16 and its
existing SHA512. Its Python hook is retained explicitly rather than silently
removed. The inspected Emscripten 4.0.16 `emcc` wrapper itself executes Python;
that inspected version is **not** a compatible or qualified SDK selection.
No SDK version is currently pinned by this project. Compiler execution awaits a
supported policy/source contract; do not replace emcc with raw clang and claim
the browser target has been qualified.

The historical launcher `tools/web_run.py`, ISO extraction and development
server still require migration. Do not execute those Python paths under the
current no-Python contract. No game data or runtime operation is needed for graph
generation.
