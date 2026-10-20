# Halo 2 decompilation mapped against Halo CE and Cyberlith (nydus-iqs, 2026-10-20)

This is the second pass Kepler 97906 ordered. The first pass
(`halo2-decomp-survey-20261020.md`) asked whether the project can be trusted
and whether it is ready to port. This pass asks what it teaches Cyberlith.
It walks Halo 2 through the nine Halo CE takeaways in finn-home
`reports/20260930-web-halo-research.txt`, then covers what is new in Halo 2.

Revisions cited (prefix, then path:line):

| prefix | tree |
|---|---|
| `h2:` | kirklandsig/halo2-decompiled `040fd431` (read as data; nothing built or run) |
| `ce@585246d8:` | web-halo `origin/main` |
| `cy@227d0db0:` | cyberlith `dev` (Cyberlith `main` is `25619120`) |
| `diax@3b8f205f:` | diax, pinned by Cyberlith's siblings.lock |
| `bots@e92c0f76:` | cyberlith_bots, pinned by Cyberlith's siblings.lock |
| `naia@c943263e:` | naia, pinned by Cyberlith's siblings.lock |

How to read this document:
- Every line labelled **INFERENCE** is my reading, not something the code states.
- The session-layer functions under "Host migration" were re-read only at their heads.
- Every other cite was re-read at the stated line before it was written down.
- One caveat applies to all `h2:` cites: most game functions are still unmatched
  drafts. Game code: 9,326 matched, 75 near, 1,920 todo. Havok: 827 matched, 9
  near, 205 todo (`h2:config/functions.csv`). So the `h2:` source is a reading
  aid, not byte-verified behaviour.

## Correction to what I told Connor earlier

I said Halo 2 "kept CE's lockstep model". **That was wrong.** Halo 2 has two
simulation modes:
- A synchronous one. The host relays every player's input each tick, the way CE did.
- A distributed one. It replicates per-view baselines
  (`h2:include/unknown_067e10.h:24-36`) and runs predicted local copies of
  projectiles in that mode (`h2:src/projectiles.cpp:1831-1835`).

That distributed model is the "later Halo" model that CE takeaway #4 found the
web-halo port moving toward (finn-home `reports/20260930-web-halo-research.txt:31-37`).

## The three things Cyberlith should take first

1. **Firing-position evaluation for bots, on top of the pathing Cyberlith
   already has.**
   - **The gap.**
     - Today's bot brain is stateless. It holds no memory and no positions
       (`bots@e92c0f76:crates/bot_guest/src/kernel.rs`, `fire.rs`,
       `patrol.rs`, `target.rs`).
     - Tile and pathing sensing is "future scope (nydus-wqm)"
       (`cy@227d0db0:crates/bot_client/src/lib.rs:74-77`).
     - `astar_path` exists but nothing calls it. Its only reference is the
       module line (`cy@227d0db0:services/game/cell/src/lib.rs:24`).
   - **What Halo 2 shows.** It scores candidate positions through mode-masked
     evaluator tables:
     - pre-filters: 11 entries in `g_44ad90[12]`
       (`h2:src/unknown_25fc30.cpp:1700`)
     - post-scorers: 6 entries in `g_44adf0[12]` (`:1715`)
     - CE's ancestor of this is `ce@585246d8:source/ai/actor_firing_position.c:398`.
   - **Why it fits.** The scorers are pure functions of world state, so they
     stay deterministic. The chosen position becomes the goal handed to
     `astar_path`.
   - **INFERENCE:** this is the smallest step that gives bots spatial
     intent. Next would be Halo 2's timestamped squad knowledge, where an actor
     copies a squad-mate's newer observation
     (`h2:src/actor_object_knowledge.cpp:98`).
   - **Caution.** The Halo 2 project itself says its pathing code has had "no
     in-game test" (`h2:docs/path.md:451`). Take the shape, not the numbers.

2. **An authored, composite effect spec.**
   - **What Cyberlith has.**
     - The sim emits facts, and catalog rows pick presentation categories for
       them (`cy@227d0db0:services/game/simulation/src/effect_dispatch.rs:1-6`).
     - Render and audio taps never write back into the sim
       (`cy@227d0db0:apps/game/inworld/core/src/resources/deterministic_cosmetics.rs:18,52,64`).
     - Particles exist only as leaf specs
       (`cy@227d0db0:crates/asset/spec/src/particle_system.rs:16-18`).
     - No spec ties particles, lights and sounds together over time.
   - **What Halo 2 shows.** `s_effect_event` has a skip chance, delay and
     duration ranges, and lists of parts, beams, accelerations and particle
     systems (`h2:include/effects.h:143-159`). CE's events had only parts and
     particles (`ce@585246d8:source/effects/effects.c:371-382`).
   - **Why it fits.** The new spec would be selected by the category Cyberlith
     already dispatches. It is purely cosmetic, so it carries no determinism
     risk. It gives designers one authored object per impact or muzzle flash.

3. **Runtime prefetch by priority, driven by the dependency lists the specs
   already declare.**
   - **What Cyberlith has.**
     - Specs list their dependencies, e.g. level
       (`cy@227d0db0:crates/asset/spec/src/json/level/mod.rs:1145`).
     - Those lists are consumed only at bake time and in tests
       (`cy@227d0db0:crates/asset/spec/src/processor.rs:213`). A
       `git grep '\bdependency_ids\b|\.dependencies\(\)'` finds no runtime
       caller.
     - The only prefetch is the world editor's terrain ring
       (`cy@227d0db0:apps/world_editor/render/src/camera.rs:1102`).
   - **What Halo 2 shows.** It grew CE's two predicted resource kinds
     (`ce@585246d8:source/cache/predicted_resources.h:15-16`) into three
     priority classes: needed now, urgent, predicted
     (`h2:include/animation_cache.h:52-61`). Predicted-resource blocks hang off
     BSP clusters (`h2:src/unknown_03f2b0.cpp:313`) and item definitions
     (`h2:src/items.cpp:441`).
   - **INFERENCE:** a browser client pays for a cold fetch in visible stalls,
     so this matters more to Cyberlith than it did to an Xbox reading from a DVD.

Considered and not ranked:
- **Debug poison on freed slots** (`h2:src/unknown_16b570.cpp:72-74`).
  INFERENCE: in Rust, diax's generation check already turns a stale handle
  into `None`, so poisoning adds little.
- **Input quantization round-trip.** Halo 2 encodes and then decodes its own
  local action before it predicts, so its prediction sees exactly what the
  host will see (`h2:src/unknown_0699a0.cpp:402-425`). Cyberlith's move and aim
  intents are already 8-way enums (`cy@227d0db0:services/depot/types/src/direction.rs:11-20`),
  so for those this holds by construction. `FireCommand` was not checked.
- **Join-in-progress state transfer.** This is the closest contender. See
  "New in Halo 2" below.

## The nine CE takeaways, carried into Halo 2

### 1. Datum arrays with generational handles
- **H2:** Still one pool type. A handle is a 16-bit salt plus a 16-bit index,
  and salt 0 marks a free slot (`h2:include/data_array.h:1-10`).
  - New since CE: lookups return NULL on a salt mismatch
    (`h2:include/data_array.h:172-185`).
  - New since CE: an `initialize_to_bad` option fills free slots with 0xBA
    (`h2:src/unknown_16b570.cpp:72-74`).
  - Unchanged: the first salt is seeded from the array's name
    (`h2:src/unknown_16b570.cpp:64-65`, identical to
    `ce@585246d8:source/memory/data.c:357-358`).
- **Cyberlith:** Already has this.
  - `DeterministicId{pool, slot, generation}`
    (`diax@3b8f205f:crates/sim/determinism/src/lib.rs:25-29`), with freed
    slots held in quarantine before reuse (`:152`).
  - Physics has its own generational `Arena`
    (`diax@3b8f205f:crates/physics/dynamics/src/data/arena.rs:20-31`).
  - Nothing to take.

### 2. Tags as a reflective schema
- **H2:** The retail cache drops the field schema at runtime. A tag block
  shrinks to `{count, address}` (`h2:include/impacts.h:32-36`). Byte-swap
  definitions survive only in recording playback
  (`h2:src/unknown_2c4e10.cpp:9-37`).
  - INFERENCE: the schema lived in the tools, not the game.
- **CE:** The schema is in the game: `tag_field`, plus block definitions with
  byte-swap and postprocess callbacks
  (`ce@585246d8:source/tag_files/tag_groups.h:55,71,79-80`).
- **Cyberlith:** `#[derive(SpecCodec)]` generates the schema
  (`diax@3b8f205f:crates/asset/spec_derive/src/lib.rs:1-3`) and a
  `postprocess` hook (`:88-91`). Same idea, already taken.

### 3. Fixed tick, sim owns the frame
- **H2:** The tick rate is set at runtime from the display refresh, divided by 2.
  It falls back to 60/2 = 30 (`h2:src/unknown_137dd0.cpp:84-88`).
  - INFERENCE: a 50 Hz PAL display gives 25 ticks per second.
  - The tick runs one flat, ordered list of subsystem updates
    (`h2:src/game_loop.cpp:135-169`).
- **CE:** A constant 30 (`ce@585246d8:source/cseries/cseries.h:34`).
- **Cyberlith:** A fixed 40 ms tick (25 Hz)
  (`cy@227d0db0:services/game/naia_proto/src/constants.rs:11`) with an
  explicit physics order: ground-follow, physics, collisions, elevator,
  quantize (`cy@227d0db0:services/game/simulation/src/schedule.rs:373-379`).
- **Take:** none. Cyberlith's fixed tick is the better design for a
  deterministic sim. Don't copy Halo 2's refresh-derived rate.

### 4. Netcode
- See the correction above.
- **Distributed mode:**
  - per-view baselines: `h2:include/unknown_067e10.h:24-36`
  - predicted projectiles: `h2:src/projectiles.cpp:1831-1835`
  - input quantization round-trip: `h2:src/unknown_0699a0.cpp:402-425`
- **Time sync:** NTP-style. A type 0/1 echo
  (`h2:src/unknown_059ad0.cpp:1908-1910`) yields
  `offset = ((t2-t0)+(t3-t1))/2` (`:1980-1984`).
- **Cyberlith:**
  - Server-authoritative, with three timelines. Confirmed and predicted are
    separate Bevy+rapier worlds (`cy@227d0db0:_AGENTS/NETWORKING_MODEL.md:106-118`).
  - Time is managed by naia's EMA and standard-deviation estimator
    (`naia@c943263e:client/src/connection/time_manager.rs:163-192`).
- **INFERENCE:** Cyberlith is already past Halo 2's distributed mode. It
  rolls back a whole predicted world rather than predicting objects one by one.
  The netcode lessons that carry over are join and session, below.

### 5. Predicted resources and precache
- Grew in Halo 2. Top-three take #3.

### 6. Platform code in a port layer
- **H2:** None.
  - 196 source files include `<xtl.h>` or `<xonline.h>`.
  - D3D calls appear inline (`h2:src/flexible_surface.cpp:516-537`).
  - The push buffer is sized by hand (`h2:src/unknown_01cf50.cpp:7942`).
- **CE:** Has a separate `port/` tree.
- **Cyberlith:** Already has the layer: a backend-agnostic `render_api` with
  wgpu behind it (`diax@3b8f205f:crates/render/api/src/lib.rs:1-7`). Nothing to take.

### 7. Game logic as data (HS)
- **H2:** Compiled script nodes are 20 bytes each
  (`h2:include/hs_scheduler.h:104-118`). Map load points a data array straight
  at the scenario's node block (`h2:src/hs_scheduler.cpp:201-202`).
  - Threads and globals live in game state (`:224-225`).
  - Built-ins dispatch through the `g_4744e0` table
    (`h2:src/unknown_209520.cpp:844`).
- **Cyberlith:** A deterministic wasmi runtime with fuel metering, no host
  imports, and checkpoints taken as linear-memory snapshots
  (`diax@3b8f205f:crates/modding/runtime/src/lib.rs:4-25`).
- **INFERENCE:** Halo 2 confirms CE's lesson: script state is game state, so
  it is saved and restored with everything else. Cyberlith's linear-memory
  checkpoint already does this.

### 8. Named pools, signatures, fail-fast
- **H2:** The allocators are named and signed:
  - loop allocator `'loop'`, with a debug `'head'` block holding file, line
    and time (`h2:src/loop_allocator.cpp:12-13`;
    `h2:include/loop_allocator.h:28-34`)
  - physical page allocator (`h2:include/physical_memory.h:39-55`)
  - The retail build returns NULL where CE asserts
    (`ce@585246d8:source/memory/data.c:153-157`).
- **Cyberlith:** No named pools. The only `#[global_allocator]` instances are
  a test peak counter (`cy@227d0db0:mods/vocab/src/dispatch.rs:97,113`) and
  bench jemalloc (`cy@227d0db0:test/game/bench/src/main.rs:7-9`).
- **INFERENCE:** in Rust, the fail-fast half is the `Result` on full regions
  (`diax@3b8f205f:crates/sim/determinism/src/lib.rs:114`). Named pools would
  be a profiling aid, not a correctness one. Low priority.

### 9. Verification-heavy method
- **H2:** Byte-matching against retail. The first-pass survey covers it, along
  with why its percentages are UNKNOWN to us.
- **Cyberlith:** The analogue is byte-exact physics determinism
  (`diax@3b8f205f:crates/physics/ARCHITECTURE.md:3-11`).
- **INFERENCE:** this is the same discipline: one reference output, and every
  change must reproduce it exactly.

## New in Halo 2

### Join-in-progress
- **H2:** A joiner receives the whole game state, compressed at level 9
  (`h2:src/unknown_085540.cpp:801-807`; the region is 0x3fe000 bytes,
  `h2:src/unknown_067e10.cpp:431`). It then buffers updates from update
  number N onward and applies them (`h2:src/unknown_085540.cpp:593-597`).
- **Cyberlith:**
  - Has a server-side admission gate for remote join-in-progress
    (`cy@227d0db0:apps/game/network/src/remote_seat_gate.rs:1-10,22`).
  - Has a live-checkpoint channel and timeline
    (`cy@227d0db0:services/game/naia_proto/src/channels.rs:148`;
    `cy@227d0db0:apps/game/inworld/core/src/systems/game/tick/live_checkpoint.rs:11`).
  - How a joiner's world is seeded today is UNKNOWN to this pass.
- **INFERENCE:** if seeding is not already "checkpoint at tick N, then replay
  buffered commands from N", Halo 2 is the template, and this belongs in the
  top three.

### Host migration and the session layer
- **H2:** (function heads re-read; bodies not):
  - graceful handoff (`h2:src/unknown_059ad0.cpp:3784-3876`)
  - election (`:3896-3966`)
  - reliable streams with acks, RTT and RTO (`h2:src/network_streams.cpp:560-772`)
  - stale-base rejection (`h2:src/unknown_059ad0.cpp:4614-4619`)
  - Re-read: membership is sent as deltas
    (`h2:src/network_messages_membership.cpp:148-152`).
- **Cyberlith:**
  - Dedicated servers. There is no host migration.
  - The closest piece is an epoch-fenced match-resume lease
    (`cy@227d0db0:apps/game/network/src/match_resume.rs`).
  - The seat gate says "succession election timing are staged out"
    (`remote_seat_gate.rs:9-10`).
- **Take:** only membership deltas against an acked base, with stale bases
  rejected. Peer host election does not fit a dedicated-server model.

### Havok
- **H2:** Havok is linked as prebuilt code (`h2:docs/DECOMPILING.md:273-279`).
  - Each tick, the game masks FP exceptions and sets MXCSR, steps the world,
    restores FP state, then walks islands and syncs each body back to its game
    object by a stored key (`h2:src/unknown_1c3d30.cpp:90-118`).
  - The game writes rigid-body velocities directly
    (`h2:src/unknown_1d24a0.cpp:163-300`).
- **CE:** Its own mass-point physics
  (`ce@585246d8:source/physics/physics.c:2097`).
- **Cyberlith:** A permanent, f32, libm, byte-exact fork of rapier and parry
  (`diax@3b8f205f:crates/physics/ARCHITECTURE.md:3-11`), stepped with a fixed
  duration and snapped to a grid after the solve.
- **Take:** at most, a debug assertion that the FP environment is as expected
  at each step boundary.
  - INFERENCE: Halo 2's dt is variable and its x87 Havok is not deterministic,
    so nothing else carries over.

### Vehicles and boarding
- **H2 vehicles:** Updates switch on a locomotion kind
  (`h2:src/vehicles.cpp:1081-1104`): differential, wheeled, hover, walker,
  and two others. CE has a fixed list of vehicle types instead.
- **H2 boarding:**
  - Boarding is a unit request with perform, update, finished and interrupted
    handlers (`h2:src/unknown_0e6900.cpp:9-23`; boarding registered at `:119`).
  - Who may board: no friend in the seat, and no player already boarding it
    (`h2:src/unknown_0a76b0.cpp:2717-2720`).
  - Boarding seats are flagged on the seat (`:2783-2786`).
- **Cyberlith:** No vehicles, boarding or ragdolls (`git grep` for `ragdoll`
  gives 0 hits; `boarding|hijack` gives 1, which is "onboarding").
  - The only carrier is the elevator, whose rider links are sim state
    (`cy@227d0db0:services/game/simulation/src/elevator.rs:1-6`).
- **INFERENCE:** the request lifecycle (perform, update, finished,
  interrupted) is a good pattern for any multi-tick action on a carrier. With
  nothing in Cyberlith to use it yet, it is not a take now.

### Dual-wield
- **H2:**
  - Weapon flag bits 22 and 23 mark "dual-wieldable" and "second hand only"
    (`h2:src/unit_object_type.cpp:5912-5914`).
  - The second hand opens only when the first hand's weapon allows it.
  - Units gain `current_secondary_weapon_index`; CE had only
    `current_weapon_index` (`ce@585246d8:source/units/units.h:477-480`).
- **Cyberlith:** Already ahead.
  - Two active sides and two backpack slots
    (`cy@227d0db0:services/game/naia_proto/src/components/networked/loadout.rs:38-50`).
  - An independent `FireSide {Primary, Secondary}` pipeline
    (`cy@227d0db0:services/game/simulation/src/systems/projectiles.rs:547-556`).
  - A `dual_wield` field on weapons
    (`cy@227d0db0:crates/asset/catalog/src/weapon_catalog.rs:201`) that is
    false on all 14 rostered weapons
    (`cy@227d0db0:crates/asset/catalog/src/fixtures.rs:998-1000`).
- **INFERENCE:** Halo 2's eligibility rule would give that field a meaning.
  But it would restrict play that works today, so it is a design call for
  Connor, not an engineering take.

### Effects and rendering
- **Effects:** top-three take #2.
- **H2 rendering:** raw Xbox D3D with no abstraction layer, so there is
  nothing to take.
- **Small deterministic bound worth noting:** Halo 2 caps a projectile at 10
  collisions per tick (`h2:src/projectiles.cpp:2030-2033`).

## AI: what is new beyond CE
- **Behavior is a DAG of slot handlers.** `k_slot_type_count = 0x83`
  (`h2:include/slot_handler.h:102`; slot kinds at `:8-11`).
- **Decisions use an urgent interrupt.** A behavior scoring more than 1 is
  "urgent" (`h2:src/unknown_1a58b0.cpp:919,954`). The decision walk is at
  `:773-910`.
- **Behaviors post requests rather than act directly**
  (`h2:src/actor_behavior.cpp:116`).
- **Knowledge is tiered:** props 0x100, prop refs 0x400, tracking 0x64
  (`h2:include/props.h:4-9`).
- **Squads:** 335 squads, 100 squad groups
  (`h2:src/unknown_2003a0.cpp:163-164`).
- **For Cyberlith:** take #1 above, then the timestamped shared knowledge,
  then a small prioritized behavior list with the urgent interrupt.
  - INFERENCE: the full 0x83-slot DAG is far more than a stateless bot brain
    needs.

## Limits
- Nothing was built or run, from either the Halo 2 tree or Cyberlith.
- Four fresh-context read-only subagents swept AI, netcode, physics, and tags
  and engine core.
- I re-read every cite at the stated line (the session-layer functions at their heads only).
- Absences are search results over the named tree with the named pattern.
  They are not proofs of absence.
