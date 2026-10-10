#!/usr/bin/env bash
# Native impact controls: actual C worker -> Rust C ABI -> existing Diax/wasmi.
# This is not mission vocabulary, browser gameplay or engine restore evidence.
set -euo pipefail
cd "$(dirname "$0")/../.."
free_bytes=$(df -B1 --output=avail / | tail -n 1)
if (( free_bytes < 26843545600 )); then
  echo 'Diax worker controls refuse: below 25 GiB free disk' >&2
  exit 1
fi
manifest=port/web/diax_guest/Cargo.toml
target_dir=build/diax-guest
controls_dir=build/diax-worker-controls
mkdir -p "$controls_dir"
cargo build --locked --manifest-path "$manifest" --lib --target-dir "$target_dir" -j 4
cargo run --locked --manifest-path "$manifest" --example worker_fixture \
  --target-dir "$target_dir" -j 4 -- "$controls_dir/diax_worker_fixture.h"
clang-18 -std=c11 -Wall -Wextra -Werror -pthread \
  -Isource -Iport/web/src -I"$controls_dir" \
  port/web/tests/diax_worker_controls_test.c port/web/src/web_diax_guest.c \
  "$target_dir/debug/libhalo_diax_guest.a" -ldl -lm \
  -o "$controls_dir/diax_worker_controls"
"$controls_dir/diax_worker_controls"
