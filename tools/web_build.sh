#!/usr/bin/env bash
# Existing browser graph using unchanged SDK entries and SDL's CMake build.
set -euo pipefail
web_cc=''
release=false
launcher=''
emsdk=''
while (($#)); do
  case "$1" in
    --web-cc|--compiler-launcher) (($# >= 2)) || { echo "Missing value: $1" >&2; exit 2; }; if [[ $1 == --web-cc ]]; then web_cc=$2; else launcher=$2; fi; shift 2 ;;
    --release) release=true; shift ;;
    --sdk) (($# >= 2)) || { echo 'Missing --sdk path' >&2; exit 2; }; emsdk=$2; shift 2 ;;
    --help|-h) echo 'Usage: bash tools/web_build.sh --sdk DIRECTORY [--web-cc BINARY] [--compiler-launcher BINARY] [--release]'; exit 0 ;;
    *) echo "Unsupported web argument: $1" >&2; exit 2 ;;
  esac
done
command -v jq >/dev/null || { echo 'Browser configuration requires jq' >&2; exit 1; }
[[ -f port/linux/port.json && -f port/web/shell.html ]] || { echo 'Browser configuration inputs missing' >&2; exit 1; }
[[ -n $emsdk ]] || { echo 'Browser configuration requires --sdk DIRECTORY outside the project and agent home' >&2; exit 2; }
emsdk=$(realpath -m "$emsdk")
case "$emsdk/" in "$(pwd -P)/"*|"$(dirname "$(pwd -P)")/"*) echo 'SDK must be outside the project and agent home' >&2; exit 2 ;; esac
if [[ -z $web_cc ]]; then web_cc="$emsdk/upstream/emscripten/emcc"; fi
np() { local v=$1; v=${v//\$/\$\$}; v=${v// /\$ }; v=${v//:/\$:}; printf '%s' "$v"; }
sq() { local v=$1; v=${v//\'/\'\\\'\'}; printf "'%s'" "$v"; }
literal() { local v=$1; [[ $v != *$'\n'* && $v != *$'\r'* ]] || exit 2; printf '%s' "${v//\$/\$\$}"; }
edge() { local out=$1 rule=$2; shift 2; printf 'build %s: %s' "$(np "$out")" "$rule"; for input in "$@"; do printf ' %s' "$(np "$input")"; done; printf '\n'; }
platform=$(jq -er '.platform_sources' port/linux/port.json)
game_dir=$(jq -er '.game_sources' port/linux/port.json)
sdk=port/include/xdk
prefix=port/linux/include/halo_linux_prefix.h
semantics=build/web/halo_msvc_semantics.h
platform_semantics=build/web/platform_msvc_semantics.h
sdl="-I$(sq build/web/sdl-install/include)"
abi="-DHALO_WEB=1 -DHALO_ANDROID=1 -fms-extensions -fshort-wchar -fcommon -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -fno-omit-frame-pointer -ffp-contract=off -O2 -pthread $sdl"
debug=-g; assertions=1
if $release; then abi+=' -DHALO_RELEASE'; debug=''; assertions=0; else abi+=' -g'; fi
game_flags='-std=gnu89 -D__STRICT_ANSI__ -w -Wno-error=incompatible-pointer-types -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=return-type'
platform_flags='-std=gnu11 -D_GNU_SOURCE -DHALO_LINUX_PLATFORM_LAYER -w'
posix="-DHALO_WEB=1 -DHALO_ANDROID=1 -std=gnu11 -D_GNU_SOURCE -O2 -g -pthread -w -I$(sq "$platform")"
printf '# Browser graph; unchanged external SDK, SDL 3.4.16 static library.\nweb_cc = %s\n' "$(literal "$(sq "$web_cc")")"
regen="sh configure.sh --web --sdk $(sq "$emsdk") --web-cc $(sq "$web_cc")"
if $release; then regen+=' --release'; fi
if [[ -n $launcher ]]; then regen+=" --compiler-launcher $(sq "$launcher")"; fi
printf 'rule configure_web\n  command = %s\n  generator = 1\n  description = CONFIGURE browser\n' "$(literal "$regen")"
regen_inputs=(configure.sh tools/web_build.sh tools/linux_msvc_semantics.mjs config/config.json port/linux/port.json port/web/src port/linux/src port/linux/game source/game port/include/xdk)
if [[ -d port/web/assets ]]; then regen_inputs+=(port/web/assets); fi
edge build.ninja configure_web "${regen_inputs[@]}"
printf 'rule web_sdl\n  command = bash tools/web_sdl.sh --sdk %s\n  description = WEB SDL 3.4.16\n' "$(literal "$(sq "$emsdk")")"
edge build/web/sdl-install/lib/libSDL3.a web_sdl tools/web_sdl.sh build/native-deps/SDL-release-3.4.16/CMakeLists.txt build/native-deps/SDL-release-3.4.16/include/SDL3/SDL_version.h
printf '%s\n' 'rule web_msvc_semantics' '  command = node tools/linux_msvc_semantics.mjs --output $out $scan' '  description = WEB MSVC SEMANTICS $out' '  restat = 1'
mapfile -d '' -t sdk_headers < <(find "$sdk" -maxdepth 1 -type f -name '*.h' -print0 | sort -z)
mapfile -d '' -t game_headers < <(find source -type f \( -name '*.c' -o -name '*.h' \) -print0 | sort -z)
printf 'build %s: web_msvc_semantics | build/web/semantics-inputs\n' "$(np "$semantics")"
printf '  scan = --all-inlines --tags source --inlines source --inlines %s\n' "$sdk"
# Explicit dependencies have no effect on the semantics command's scan arguments.
# Emit separate dependency-only phony inputs for both generated headers below.
edge build/web/semantics-inputs phony tools/linux_msvc_semantics.mjs "${sdk_headers[@]}" "${game_headers[@]}"
printf 'build %s: web_msvc_semantics | build/web/platform-semantics-inputs\n' "$(np "$platform_semantics")"
printf '  scan = --inlines %s\n' "$sdk"
edge build/web/platform-semantics-inputs phony tools/linux_msvc_semantics.mjs "${sdk_headers[@]}"
printf 'rule web_cc\n  command = mkdir -p "$$(dirname "$out")" && %s$web_cc -MMD -MF $out.d $cflags -c $in -o $out\n  description = WEB CC $out\n  depfile = $out.d\n  deps = gcc\n' "$(if [[ -n $launcher ]]; then literal "$(sq "$launcher") "; fi)"
printf '%s\n' 'rule web_link' '  command = $web_cc $ldflags -o $out @$out.rsp $libs' '  description = WEB LINK $out' '  rspfile = $out.rsp' '  rspfile_content = $in_newline'
objects=()
add_object() {
  local source=$1 cflags=$2 namespace=${3:-} object
  object="build/web/obj/${namespace}${source%.c}.o"
  objects+=("$object")
  printf 'build %s: web_cc %s | %s %s %s' "$(np "$object")" "$(np "$source")" "$(np "$prefix")" "$(np "$semantics")" "$(np "$platform_semantics")"
  for header in "${sdk_headers[@]}"; do printf ' %s' "$(np "$header")"; done
  printf ' || build/web/sdl-install/lib/libSDL3.a\n  cflags = %s\n' "$(literal "$cflags")"
}
variadic=' source/ai/action_uncover.c source/ai/ai.c source/ai/ai_debug.c source/bungie_net/common/public_key_crypt.c source/camera/editor_flying_camera.c source/game/cheats.c source/game/game_engine.c source/game/players.c source/hs/hs.c source/interface/hud_nav_points.c source/interface/ui_widget_game_data_input_functions.c source/networking/telnet_console.c source/rasterizer/xbox/rasterizer_xbox_errors.c source/render/render.c '
while IFS= read -r project; do
  flags="$abi $game_flags -include $prefix -include $semantics -Iport/linux/include -idirafter $sdk"
  while IFS= read -r define; do flags+=" $(sq "-D$define")"; done < <(jq -r '.options.defines // [] | .[]' <<<"$project")
  while IFS= read -r inc; do flags+=" -I$(sq "$inc")"; done < <(jq -r '.options.include_dirs // [] | .[] | select(. != "xbox/include")' <<<"$project")
  mapfile -t selected < <(jq -r --slurpfile port port/linux/port.json '.objects[] | select(.status != "MISSING") | .name | select(test("\\.c$";"i")) | select(. as $n | $port[0].exclude_sources | index($n) | not)' <<<"$project")
  for authored in source/game/campaign_coop.c source/game/campaign_mission.c source/main/main_fatal.c; do
    if [[ -f $authored && " ${selected[*]} " != *" $authored "* ]]; then selected+=("$authored"); fi
  done
  for source in "${selected[@]}"; do
    extra=''; if [[ $variadic == *" $source "* ]]; then extra=' -include port/android/include/halo_android_variadic_prototypes.h'; fi
    add_object "$source" "$flags$extra"
  done
  while IFS= read -r -d '' source; do add_object "$source" "$flags"; done < <(find "$game_dir" -maxdepth 1 -type f -name '*.c' -print0 | sort -z)
done < <(jq -c --slurpfile port port/linux/port.json '.projects[] | select(.name as $n | $port[0].projects | index($n))' config/config.json)
platform_cflags="$abi $platform_flags -include $prefix -include $platform_semantics -I$(sq "$platform") -Iport/linux/include -Iport/web/src -Iport/third_party/tomlc17 -Iport/third_party/kcp -Isource -Isource/cseries -idirafter $sdk"
while IFS= read -r -d '' source; do
  name=${source##*/}; [[ $name != posix_update.c && $name != updater.c ]] || continue
  if [[ $name == posix_* ]]; then add_object "$source" "$posix"; else add_object "$source" "$platform_cflags"; fi
done < <(find "$platform" -maxdepth 1 -type f -name '*.c' -print0 | sort -z)
while IFS= read -r -d '' source; do
  if [[ ${source##*/} == web_loopback_net.c ]]; then add_object "$source" "$posix"; else add_object "$source" "$platform_cflags"; fi
done < <(if [[ -d port/web/src ]]; then find port/web/src -maxdepth 1 -type f -name '*.c' -print0 | sort -z; fi)
add_object port/third_party/tomlc17/tomlc17.c "$abi -std=gnu11 -w"
add_object port/third_party/kcp/ikcp.c "$abi -std=gnu11 -w"
while IFS= read -r -d '' source; do add_object "$source" "$abi -std=gnu11 -w -Iport/third_party/musl-math/include -include port/third_party/musl-math/include/libm.h" musl-math/; done < <(find port/third_party/musl-math/src -maxdepth 1 -type f -name '*.c' -print0 | sort -z)
printf 'build build/web/halo.html | build/web/halo.js build/web/halo.wasm: web_link'
for object in "${objects[@]}"; do printf ' %s' "$(np "$object")"; done
printf ' | build/web/sdl-install/lib/libSDL3.a port/web/shell.html port/web/xiso.js port/web/fetch_path_normalization.js port/web/online_client.js port/web/library_web_transport.js\n'
printf '  ldflags = -O2 %s -pthread %s -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=16 -sOFFSCREENCANVAS_SUPPORT=1 -sOFFSCREENCANVASES_TO_PTHREAD=#canvas -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sGL_ENABLE_GET_PROC_ADDRESS=1 -sWASMFS=1 -sFORCE_FILESYSTEM=1 -sINITIAL_MEMORY=2415919104 -sMAXIMUM_MEMORY=4294967296 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=5242880 -sDEFAULT_PTHREAD_STACK_SIZE=2097152 -sEXIT_RUNTIME=0 -sASSERTIONS=%s -sENVIRONMENT=web,worker -sERROR_ON_UNDEFINED_SYMBOLS=1 --shell-file port/web/shell.html --pre-js port/web/xiso.js --pre-js port/web/fetch_path_normalization.js --pre-js port/web/online_client.js\n' "$debug" "$sdl" "$assertions"
printf '%s\n' '  libs = build/web/sdl-install/lib/libSDL3.a -lfetchfs.js -lopfs.js --js-library port/web/library_web_transport.js' 'rule web_copy_asset' '  command = mkdir -p "$$(dirname "$out")" && cp -p "$in" "$out"' '  description = WEB ASSET $out'
assets=()
while IFS= read -r -d '' source; do output="build/web/${source#port/web/}"; assets+=("$output"); edge "$output" web_copy_asset "$source"; done < <(if [[ -d port/web/assets ]]; then find port/web/assets -type f -print0 | sort -z; fi)
edge web phony build/web/halo.html build/web/halo.js build/web/halo.wasm "${assets[@]}"
