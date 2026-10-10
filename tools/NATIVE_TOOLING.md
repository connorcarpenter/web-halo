# Native Linux tooling port

The native route uses a POSIX shell entry and Node 20+ helpers, without invoking
Python. It reads the existing `config/config.json` and `port/linux/port.json`,
retains the i686 game/platform/POSIX ABI split, generates MSVC linkage headers,
and rejects unresolved weak symbols and glibc wide-character imports after link.

```sh
sh configure.sh --linux-cc clang --linux-nm llvm-nm-18 --pgo off --lto thin
ninja -j 4 linux
node --test tools/tests/native_helpers.test.mjs tools/tests/linux_build.test.mjs
```

The compiler, linker and LLVM symbol reader must be compatible. Native compilation
requires 32-bit libc development headers/libraries and 32-bit SDL3 development
headers/libraries. LTO needs lld and an LLVM-aware symbol reader. SDL2 is not SDL3.
ISO/maps are runtime inputs, not configure or non-PGO compile prerequisites.

Web, Android, authentic Xbox and PGO use/train are explicitly refused by this entry
until their tooling ports are implemented; the historical Python entry remains
unchanged, but is not used by this route. The imported source manifest is retained;
`source/game/campaign_coop.c` is included when present so the campaign implementation
cannot be silently omitted. Node tests and Ninja dry runs prove tooling properties,
not an engine compile, link, asset load or multiplayer acceptance.

## Qualified project-local SDL route

The Worktop native route used Clang/LLVM 18 and SDL 3.4.16, the version already
pinned by `port/web/halo_sdl3.py`. Its release archive SHA512 is
`af4109305de11e94619404feffd2874f26a25cbde71b7c8d8f3cd2210232858f18061df85df6a4467d5109f168381d6eb4dc1e27bd375d12f7448d1cbd71da41`.
Verify that archive before extracting it into `build/native-deps`. No Python
entry is needed to consume this dependency.

On Ubuntu the demonstrated missing prerequisites were `libc6-dev-i386`,
`lib32gcc-13-dev`, `libc6-dev:i386`, `linux-libc-dev:i386`, and `lld-18`.
Use the supported package manager for your host; inspect its proposed transaction
before installation. These names are measured Ubuntu prerequisites, not a
portable installation script.

```sh
cmake -S build/native-deps/SDL-release-3.4.16 \
  -B build/native-deps/sdl-build -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_C_FLAGS=-m32 \
  -DCMAKE_SHARED_LINKER_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/native-deps/install" -DSDL_TESTS=OFF
cmake --build build/native-deps/sdl-build --parallel 4
cmake --install build/native-deps/sdl-build
sh configure.sh --linux-cc clang --linux-nm llvm-nm-18 \
  --linux-ld /usr/bin/ld.lld-18 --native-prefix build/native-deps/install \
  --pgo off --lto thin
ninja -j 4 linux campaign-native-controls
```

The campaign control links the unmodified production campaign implementation
with Finn's native engine-header test and only its backing-store stubs. It does
not link duplicate player/data/main implementations. Its 19 checks exercise
formation, remote roster counting, committed save-reader values, and refusal;
they do not establish map loading, save writing, or checkpoint restoration.
The full game target separately proves the production hook/accessor link.
SDL backend discovery and successful ELF32 linking are not live video/audio or
multiplayer acceptance.

Every future native qualification of a head carrying
`source/game/campaign_coop.c` must also run `campaign-native-controls` and
report its actual PASS count and unpiped exit. Retain the same two-object target;
the expected count is 19 for Finn's test at `54fcc6b`, and its writer names any
changed count when updating the test. A successful game link alone is not this
behavioral guard.
