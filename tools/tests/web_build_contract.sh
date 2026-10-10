#!/usr/bin/env bash
# Source graph checks only: Ninja tools never execute a compiler.
set -euo pipefail
cd "$(dirname "$0")/../.."
graph=$(bash tools/web_build.sh --sdk '/sdk with spaces' --release --web-cc '/sdk with spaces/emcc' --compiler-launcher ccache)
require() { [[ $graph == *"$1"* ]] || { echo "Missing graph contract: $1" >&2; exit 1; }; }
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
require '  libs = build/web/sdl-install/lib/libSDL3.a'
[[ $graph != *'--use-port='* ]]
require '-include port/android/include/halo_android_variadic_prototypes.h'
require 'build build/web/halo.html | build/web/halo.js build/web/halo.wasm: web_link'
[[ $graph != *'build build/web/obj/port/linux/src/updater.o:'* ]]
[[ $graph != *'build build/web/obj/port/linux/src/posix_update.o:'* ]]
[[ $graph != *'tools/linux_msvc_semantics.py'* ]]
printf '%s\n' "$graph" | ninja -f /dev/stdin -t targets all >/dev/null
if bash tools/web_build.sh --unsupported >/dev/null 2>&1; then echo 'Unknown option accepted' >&2; exit 1; fi
printf 'PASS 18 browser graph contracts; no compilation\n'
