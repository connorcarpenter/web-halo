#!/usr/bin/env bash
# SDL's supported Emscripten CMake route, replacing our Python user-port hook.
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=''
print_only=false
while (($#)); do
    case "$1" in
        --sdk) (($# >= 2)) || { echo 'Missing --sdk path' >&2; exit 2; }; sdk=$2; shift 2 ;;
        --print-commands) print_only=true; shift ;;
        --help|-h) echo 'Usage: bash tools/web_sdl.sh --sdk DIRECTORY [--print-commands]'; exit 0 ;;
        *) echo "Unsupported SDL argument: $1" >&2; exit 2 ;;
    esac
done
[[ -n $sdk ]] || { echo 'SDL browser build requires --sdk DIRECTORY' >&2; exit 2; }
sdk=$(realpath -m "$sdk")
project=$(pwd -P)
case "$sdk/" in
    "$project/"*|"$(dirname "$project")/"*) echo 'SDK must be outside the project and agent home' >&2; exit 2 ;;
esac
source_dir=build/native-deps/SDL-release-3.4.16
version_header="$source_dir/include/SDL3/SDL_version.h"
[[ -f $version_header ]] || { echo 'Retained pinned SDL 3.4.16 source is missing' >&2; exit 1; }
version=$(awk '/^#define SDL_MAJOR_VERSION/{a=$3} /^#define SDL_MINOR_VERSION/{b=$3} /^#define SDL_MICRO_VERSION/{c=$3} END{print a "." b "." c}' "$version_header")
[[ $version == 3.4.16 ]] || { echo "SDL source version mismatch: $version" >&2; exit 1; }
if ! $print_only; then
    [[ -x "$sdk/upstream/emscripten/emcmake" && -f "$sdk/emsdk_env.sh" ]] || { echo 'Supported activated SDK shell entry is missing' >&2; exit 1; }
    # Unchanged vendor setup; never call a Python script or patch the SDK.
    source "$sdk/emsdk_env.sh"
fi
run() {
    printf '+ '; printf '%q ' "$@"; printf '\n'
    if ! $print_only; then
        local available
        available=$(df -B1 --output=avail / | awk 'NR==2 {print $1}')
        [[ $available =~ ^[0-9]+$ ]] || { echo 'Disk availability UNKNOWN' >&2; exit 3; }
        ((available >= 32212254720)) || { echo "SDL build capacity refusal: $available < 32212254720 bytes" >&2; exit 3; }
        "$@"
    fi
}
run "$sdk/upstream/emscripten/emcmake" cmake -S "$source_dir" -B build/web/sdl-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DSDL_PTHREADS=ON -DSDL_STATIC=ON -DSDL_SHARED=OFF \
    -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF "-DCMAKE_INSTALL_PREFIX=$project/build/web/sdl-install"
run nice -n 19 cmake --build build/web/sdl-build --target SDL3-static --parallel 4
run cmake --install build/web/sdl-build
