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
