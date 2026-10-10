#!/usr/bin/env bash
set -euo pipefail
if [[ ${1:-} == --help ]]; then
  echo 'Usage: bash tools/diax_guest_build.sh --sdk DIRECTORY'
  exit 0
fi
[[ $# == 2 && $1 == --sdk ]] || { echo 'Expected --sdk DIRECTORY' >&2; exit 2; }
sdk=$(realpath "$2")
compiler="$sdk/upstream/emscripten/emcc"
[[ -x $compiler ]] || { echo 'External emcc is missing' >&2; exit 2; }
case "$sdk/" in "$(dirname "$(pwd -P)")/"*) echo 'SDK must be outside agent home' >&2; exit 2 ;; esac
available=$(df -B1 --output=avail /)
available=${available//[[:space:]]/}
[[ $available =~ ^Avail([0-9]+)$ ]] || { echo 'Cannot measure disk floor' >&2; exit 1; }
(( ${BASH_REMATCH[1]} >= 26843545600 )) || { echo 'Below 25 GiB disk floor' >&2; exit 1; }
expected=$(cat port/web/diax_guest/diax-main.pin)
actual=$(git -C ../diax rev-parse HEAD)
[[ $actual == "$expected" ]] || { echo "Diax source mismatch: expected $expected, read $actual" >&2; exit 1; }
printf 'Diax source %s; target wasm32-unknown-emscripten\n' "$actual"
linker=$(jq -cn --arg compiler "$compiler" '$compiler')
cargo build --locked --manifest-path port/web/diax_guest/Cargo.toml --lib --release \
  --target wasm32-unknown-emscripten --target-dir build/diax-guest -j 4 \
  --config "target.wasm32-unknown-emscripten.linker=$linker"
