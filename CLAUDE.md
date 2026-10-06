# Fallout 1 CE — First-Person Conversion

Converting Fallout 1 Community Edition into a playable first-person game on
Steam Deck/Linux, using original Fallout assets and native gameplay systems.
Continue the existing implementation. Do not start over.

- Repo: ~/fallout1-ce-first-person (fork of alexbatalov/fallout1-ce)
- Working branch: `first-person-prototype`
- Last known-good checkpoint: c59bcd33530b3d12c550c28ffb0685bbca5ea0b0
- Key docs: `docs/first-person-walls.md`, `docs/first-person-input.md`
- First-person code: `src/game/first_person*.{cc,h}`, `src/game/gamepad*`
- Tests: `tests/first_person_*_test.cc`, `tests/gamepad_*_test.cc`

## Hard constraints

- Preserve native movement, collision, pathfinding, AP, turns, inventory,
  scripts, dialogue, locks and object-use logic. First person is a
  presentation and input layer over the native simulation.
- Solutions must be general across maps. No per-location manual fixes.
- Do NOT casually rewrite the wall/world coordinate math. It is stable.
  Walls are MVP-complete; difficult corners and stretched textures are deferred.
- Preserve GPU renderer performance, the software fallback, and native Deck
  controls (including rear buttons).
- Preserve these fixes:
  - 153ca65: downward-look depth guides stay inside the framebuffer
  - 1bfae28: filter correlated Deck keyboard echoes (e.g. Y also sending Space)
  - 829a168: `win_get_top_visible_win` for first-person ownership. Legacy
    `win_get_top_win` includes hidden windows. Presentation visibility is
    separate from combat/input permission.
  - c59bcd3: weapon sprite mirrored horizontally, same placement/animation
- Explain findings before any substantial architectural change, and wait for
  approval.

## Build (the SDK is a Flatpak; the host has no compiler)

Always build through the SDK, non-interactively:

```sh
flatpak run --filesystem=home --share=network --command=sh org.freedesktop.Sdk//24.08 \
  -c 'cd ~/fallout1-ce-first-person/build && cmake --build . -j4'
```

The build config must be a normal Release build:
ASAN=OFF, UBSAN=OFF, CMAKE_BUILD_TYPE=Release, CMAKE_EXE_LINKER_FLAGS="".
A previous AddressSanitizer attempt failed in this environment and was abandoned.
Do not retry ASan. Do not confuse leftovers from that attempt with a source
regression. Verify with:

```sh
grep -E '^(ASAN|UBSAN|CMAKE_BUILD_TYPE|CMAKE_EXE_LINKER_FLAGS|CMAKE_C_FLAGS|CMAKE_CXX_FLAGS):' \
  ~/fallout1-ce-first-person/build/CMakeCache.txt
flatpak run --filesystem=home --command=sh org.freedesktop.Sdk//24.08 \
  -c 'ldd ~/fallout1-ce-first-person/build/fallout-ce | grep -iE "asan|ubsan" || echo clean'
```

Unit tests also run inside the SDK, e.g.:

```sh
flatpak run --filesystem=home --command=sh org.freedesktop.Sdk//24.08 -c \
 'cd ~/fallout1-ce-first-person && g++ -std=c++17 -Wall -Wextra -Werror \
  -ffunction-sections -fdata-sections -Isrc $(sdl2-config --cflags) \
  tests/first_person_input_test.cc \
  -Wl,--gc-sections -o /tmp/fp-input-test && /tmp/fp-input-test'
```

## Device testing (the human does this)

You cannot play the game. When a change needs on-device testing, stop and give
the user one command block plus a short checklist of what to try. The launch
command must run from the host terminal, outside the SDK:

```sh
cd "/home/deck/.local/share/Steam/steamapps/common/Fallout/" && \
FALLOUT_FP_INPUT_LOG="/home/deck/fallout1-ce-first-person/input-debug.log" \
~/fallout1-ce-first-person/build/fallout-ce
```

Each launch overwrites `input-debug.log`. Tell the user to copy it after a
crash (e.g. `cp input-debug.log input-debug-caseA.log`) before relaunching.
Afterwards, read the logs and any crash report the user saves in the repo root.

## Git rules

- Work only on `first-person-prototype`. Never force-push. Never rewrite history.
- Before each commit: build succeeds and relevant tests pass.
- Small, focused commits with descriptive messages (match the existing style).
- Diagnostic-only code must be gated behind an env var and off by default.
- Push after each verified commit unless told otherwise.

## Current blocking bug: crash when looting a body

Main-thread stack:

```
heap_build_free_list <- heap_find_free_block <- heap_allocate <- cache_add
<- cache_lock <- art_ptr_lock <- inven_init <- loot_container
<- scripts_check_state <- main_game_loop
```

The input log ends right after RT dispatched RETICLE_ACTION. Looting runs as a
deferred script request on the next tick. Opening native inventory from the
first-person action menu works, so `inven_init` itself is not inherently broken.

Working theory (unproven):
- `heap_build_free_list` walks every art-cache block header, and it only runs
  under allocation pressure.
- A header corrupted earlier stays latent until the loot UI loads many FRMs at
  once. The crash site is a detection point, not the cause.
- Do not assume the corpse interaction itself is the culprit.

Suspects, in priority order:
1. Out-of-bounds writes in first-person software raster paths (depth buffer,
   pick buffer, floor spans, wall columns, guides, reticle, outlines).
   c36fb41 and 153ca65 already fixed this bug class twice.
2. In-place writes into cached art pixel data (weapon mirror, GPU palette
   remap, wall rectification). These must only write to private copies.
3. Unbalanced `art_ptr_lock`/`art_ptr_unlock` calls (double unlock can free a
   block that is still in use).

Planned diagnostics (env-gated, off by default):
- `FALLOUT_FP_HEAP_CHECK=1`: validate the art-cache heap at labelled checkpoints
  (after each FP frame, split GPU vs software path; after each FP input action;
  around the weapon draw; on FP toggle). Log the first failing label and frame
  number to the input log, then abort.
- Guard pages: a PROT_NONE page after FP-owned software buffers, so the first
  overrun faults at the culprit line.
- Audit every art lock/unlock pair and every write through an art frame pointer.
