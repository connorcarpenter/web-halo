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

The Diax host archive uses the installed toolchain recorded in
`port/web/diax_guest/rust-toolchain.pin`, with `rust-src`. Its ordinary Cargo
entry rebuilds `std` through `-Zbuild-std=std,panic_abort` and enables
`atomics`, `bulk-memory` and the pthread link argument to match the existing
engine's shared-memory ABI. This follows Rust's
[Emscripten ABI compatibility guidance](https://doc.rust-lang.org/rustc/platform-support/wasm32-unknown-emscripten.html#emscripten-abi-compatibility).
These compiler settings apply to the outer Rust/C host; the interpreter still
owns separate guest linear memory. No guest entry becomes a JS export. A
successful archive build alone does not qualify final linking or runtime.

`web_diax_worker` owns guest creation, staged replacement, initial byte delivery
and destruction on one checked pthread. Replacement requires a newer generation;
failed admission keeps the prior guest alive. Even an empty init batch must be
delivered before events or guest image capture. The caller still validates and
applies the canonical game action batch and binds the matching engine checkpoint.
These ownership functions do not supply a production mission activation caller.

`bash tools/tests/diax_worker_controls.sh` exercises the actual C worker against
the native Rust archive and interpreted Wasm fixtures, including a foreign-thread
refusal, failed replacement, retained init output, traps and same-blob restore.
It uses ordinary Cargo and clang-18 without engine assets or SDL. The fixtures
have no campaign vocabulary; this is ownership/byte-boundary coverage, not a
compiled Rust cooperative mission, browser gameplay or coherent engine restore.

The historical launcher `tools/web_run.py`, ISO extraction and development
server still require migration. Do not execute those Python paths under the
current no-Python contract. No game data or runtime operation is needed for graph
generation.
