#!/bin/sh
# Allowed-language entry point; never invokes the historical Python tooling.
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec node tools/linux_build.mjs "$@"
