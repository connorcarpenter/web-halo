# Browser command generation

Generate the existing browser Ninja target without executing Python:

```sh
sh configure.sh --web --release --sdk /external/path/to/emsdk
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

## SDK and SDL execution boundary

The graph replaces the historical Python SDL hook with SDL 3.4.16's supported
Emscripten CMake build (`tools/web_sdl.sh`), using retained source, generated SDL
configuration headers and a pthread-enabled static library. It does not select
the SDK's older SDL port. Historical files remain preserved, not executable hooks.
The SDK must be installed outside the project and agent home. Signed policy
permits unchanged upstream compiler internals through documented shell entries;
direct Python invocations, vendor patches and project Python hooks remain barred.
Emscripten 6.0.12 is installed externally via unchanged emsdk install/activate.
Compiler readback is 6.0.12 (5488e0871d46d19a3209988190fa6a653126256f);
SDK manager source is 35ff8a6d150541276abbc6bae512ca90bcfbe220. Installation
and version readback are not browser C qualification.

The historical launcher `tools/web_run.py`, ISO extraction and development
server still require migration. Do not execute those Python paths under the
current no-Python contract. No game data or runtime operation is needed for graph
generation.
