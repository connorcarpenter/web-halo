#!/bin/sh
# Allowed-language entry point; never invokes the historical Python tooling.
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if [ "${1-}" = "--web" ]; then
    shift
    case "${1-}" in
        --help|-h) exec bash tools/web_build.sh "$@" ;;
    esac
    # Keep the previous generated graph if command generation refuses.
    web_graph=$(mktemp ./build.ninja.web.XXXXXX)
    trap 'rm -f "$web_graph"' EXIT HUP INT TERM
    bash tools/web_build.sh "$@" > "$web_graph"
    mv "$web_graph" build.ninja
    trap - EXIT HUP INT TERM
    printf '%s\n' 'Generated browser build.ninja (compiler qualification remains separate).'
    exit 0
fi
exec node tools/linux_build.mjs "$@"
