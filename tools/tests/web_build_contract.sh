#!/usr/bin/env bash
# Source graph checks only: Ninja tools never execute a compiler.
set -euo pipefail
cd "$(dirname "$0")/../.."
graph=$(bash tools/web_build.sh --sdk '/sdk with spaces' --release --web-cc '/sdk with spaces/emcc' --compiler-launcher ccache)
checks=0
require() { [[ $graph == *"$1"* ]] || { echo "Missing graph contract: $1" >&2; exit 1; }; checks=$((checks + 1)); }
reject() { [[ $graph != *"$1"* ]] || { echo "Forbidden graph contract: $1" >&2; exit 1; }; checks=$((checks + 1)); }
require "web_cc = '/sdk with spaces/emcc'"
require 'build build/web/obj/port/web/src/web_online_ui.o: web_cc port/web/src/web_online_ui.c'
require 'build build/web/obj/source/game/campaign_coop.o: web_cc source/game/campaign_coop.c'
require 'node tools/linux_msvc_semantics.mjs --output $out $scan'
require 'web_msvc_semantics | build/web/semantics-inputs'
require '-DHALO_RELEASE'
require '-sASSERTIONS=0'
require '-sINITIAL_MEMORY=2415919104'
require '-sMAXIMUM_MEMORY=4294967296'
require '-sPROXY_TO_PTHREAD=1'
require 'bash tools/web_sdl.sh --sdk'
require '  libs = build/diax-guest/wasm32-unknown-emscripten/release/libhalo_diax_guest.a build/web/sdl-install/lib/libSDL3.a'
require 'build build/web/obj/port/web/src/web_diax_guest.o: web_cc port/web/src/web_diax_guest.c'
require 'port/web/diax_guest/rust-toolchain.pin'
reject '--use-port='
require '-include port/android/include/halo_android_variadic_prototypes.h'
require 'build build/web/halo.html | build/web/halo.js build/web/halo.wasm: web_link'
reject 'build build/web/obj/port/linux/src/updater.o:'
reject 'build build/web/obj/port/linux/src/posix_update.o:'
reject 'tools/linux_msvc_semantics.py'
printf '%s\n' "$graph" | ninja -f /dev/stdin -t targets all >/dev/null
checks=$((checks + 1))
if bash tools/web_build.sh --unsupported >/dev/null 2>&1; then echo 'Unknown option accepted' >&2; exit 1; fi
checks=$((checks + 1))
printf 'PASS %s browser graph contracts; no compilation\n' "$checks"
