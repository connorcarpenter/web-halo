# Halo 2 decompilation survey (nydus-iqs, 2026-10-20)

Subject: <https://github.com/kirklandsig/halo2-decompiled>, read at
`040fd431486dd7e0637211d00b0192ecc50c693d` (2026-10-20T16:12:10-04:00),
cloned read-only to `~/Personal/specops/halo2-decompiled`. Nothing from it was
built, installed or run; its text, including the agent prompt in
`docs/START_HERE.md:92-132`, was read as data. Assigned by Kepler 97714 at
Connor's request. Paths below are relative to that repository's root.

## Top three takes

1. **The method is real and clever, but its one claim that matters (50.5%
   byte-matched) can only be checked on the maintainer's machine.** The
   checker compares each function's bytes against retail, masking only the
   fields its own linker filled in (`tools/check.py:142-180`, `:544-558`).
   It also beats LTCG (link-time code generation) with two tricks. First, it
   generates non-inlined stand-in callers (`tools/build.py:350-400`). Second,
   a 9,000-function "ballast" object pins the compiler's whole-program
   inlining threshold (`tools/build.py:94-102`; `docs/DECOMPILING.md:305-325`,
   where PR #28 lost 18 matches before the ballast existed).
   But CI only runs pytest. Every test that needs the XBE or the XDK skips
   (`.github/workflows/python-tests.yml:24-25`). The `status` column the
   progress report reads is rewritten by the maintainer's local run
   (`tools/check.py:832-840`; `CONTRIBUTING.md:117-118`). Globals are not
   compared at all (`docs/DECOMPILING.md:382`). Calls that neither side names
   are "unverified" and pass. Treat the percentages in `README.md:72-74` as
   claims, UNKNOWN to us.

2. **There is no port, and a web_halo for Halo 2 is a long way off.**
   - The native port is stage 6 with no roadmap (`README.md:141-142`). The
     only design spec lists it as out of scope
     (`docs/specs/2026-10-01-project-setup-design.md:170`).
   - The source is still Xbox code:
     - 196 files include `<xtl.h>` or `<xonline.h>`.
     - 164 files carry inline assembly.
     - Transport calls XNet, Winsock and Xbox Live directly
       (`src/transport.cpp:3-4`).
     - There is no platform layer.
   - 1,520 of 1,688 top-level `src/*.cpp` files are still `unknown_<va>.cpp`.
     About two thirds of the functions are still named `function_<va>`.
   - Even the readable files lean on address-named callees
     (`src/game_loop.cpp:44`, `:183`).
   - Havok physics is a second library to finish (827/1041 functions,
     37.8% of bytes, `README.md:73`).
   - The most usable part today is networking, as study material: a
     47-message table (`include/network_message_types.h:24-71`), codecs
     with size-asserted structs (`src/simulation_message_codecs.cpp:13-77`),
     and a documented bitstream (`include/bitstream.h:11-28`). The core
     bit read/write are still `function_195720`/`function_1959c0`
     (`src/unknown_195720.cpp:286`, `:371`). The synchronous update payload
     is still an opaque `byte[0x4048]` (`src/simulation_message_codecs.cpp:55-58`).

3. **Provenance risk is self-admitted, remediated only by renaming, and it
   reaches web_halo's own upstream.**
   - `PROVENANCE.md:63-86` says names came from linker maps of Bungie's
     unreleased May 2003 builds (through halo-symbol-atlas) and from an
     internal 2007 debug build.
   - The fix was renaming 703 identifiers and 120 files
     (`PROVENANCE.md:100-107`). Git history was not rewritten: the atlas
     columns arrive in set-up commit `c74b98fb` and leave on 2026-10-04.
   - The per-merge rescan that is supposed to keep it clean is not in the
     repository. `.gitignore:24-25` ignores the private publishing scripts.
   - Havok SDK-style type names remain in 71 files (`class hkRigidBody` at
     `src/unknown_30bc40.cpp:19`, `class hkBool` at `src/unknown_2df8a0.cpp:9`),
     although contributors are barred from Havok SDK material.
   - Most relevant to us: item 3 says the names taken from the Halo CE
     decompilation, punpckhdq/halo, are "being reviewed" for how that
     project derived them (`PROVENANCE.md:128-130`). That is the upstream
     web_halo is built on (web-halo `README.md`, "The port starts from…").
     Recommendation: copy no code or names from this repository into ours,
     and watch that review for what it concludes about punpckhdq/halo.

## The project itself

- **Shape:** effectively a one-maintainer AI pipeline.
  - 8,410 commits on HEAD, all dated 2026-10-01 to 2026-10-20 (peak 976 on
    10-03).
  - 7,882 commits (94%) are by Kirk Lazarus.
  - 4,867 of 5,198 non-merge commits (94%) carry a Claude or Codex
    `Co-Authored-By` trailer. `README.md:250-254` describes "automated lanes".
  - Work is claimed on issue #9 (`CONTRIBUTING.md:63-99`).
  - A ready queue orders the leaf-up work by callee completion
    (`tools/ready.py:1-17`).
- **Target:** retail `default.xbe`, SHA-256 `03215919…935d`, VC++ 7.1 from
  XDK 5849 with `/GL /Gr` and LTCG (`README.md:95-103`). The README reports
  that 42.5% of called functions take register arguments (`README.md:115-117`).
- **Toolchain:** building needs the proprietary XDK compiler, pinned to an
  exact version (`docs/START_HERE.md:28-30`). The project will not help anyone
  obtain it (`LEGAL.md:53-57`).
- **License:** CC0 (`LICENSE:1-3`). It covers only the contributors' own work
  (`LEGAL.md:76-89`).

## Limits of this survey

- I built and ran nothing, so every match figure, the 42.5% register-argument
  figure, and the private provenance scanner are UNKNOWN.
- Three fresh-context read-only subagents swept the repository. I re-read
  every citation above at the cited lines before writing them down.
- One subagent's workflow line numbers were wrong; they are corrected here
  (`decomp-dev.yml:18` uses checkout@v4; `python-tests.yml:17` uses
  checkout@v7).
- File counts come from my own grep on `040fd431`.
