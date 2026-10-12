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
  - 908e083: native cursor pick/menu arrows clamped inside their art frames
    (fixed the looting crash; see below)
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

## Resolved: crash when looting a body (908e083)

Looting crashed in `heap_build_free_list` / `heap_find_free_block` under
`inven_init <- loot_container`. That was only where the damage was detected.

Cause: `gmouse_bk_process` keeps building the native 3D-cursor hover frames
while first person is on, using the hidden mouse position. The first-person
window covers the whole screen, so the mouse can sit in the bottom 100 px band
that the native interface bar normally blocks. `gmouse_3d_build_pick_frame`
(and `_menu_frame`) then shifted the arrow far below the 69x62 `ACTPICK.FRM`
and copied it over neighbouring art-cache blocks. Both builders now clamp the
shift. Confirmed on device: a long heap-checked run and a normal run, with
kills and looting in the Vault and at a raider camp.

Lessons for this codebase:
- First person widens where native code receives the mouse. Native routines
  that assume the map area (y above the interface bar) can overrun buffers.
  Check callers that pass `scr_size ... - 99/100` heights.
- Native code writes into locked art frames it owns (`gmouse_3d_*_frame_data`).
  An overrun there corrupts the art-cache heap and surfaces much later.
- The first-person raster paths and art lock/unlock pairs were audited and
  found clean.

Diagnostics kept (off by default):
- `FALLOUT_FP_HEAP_CHECK=1` validates the art-cache heap at labelled
  checkpoints: FP frames, weapon draw, FP input, toggle, modal scopes,
  loot/inventory entry, each main-loop stage and each background process.
  The first failure prints `HEAP_CHECK_FAILED` with the label, frame, reason
  and the art owning the damaged block and the block before it, then aborts.
- `run-heapcheck.sh` (local, uncommitted) launches with the heap check and the
  input log. Pasting multi-line `export` commands into Konsole proved
  unreliable; prefer a script or one-line commands.
- Tests: `tests/first_person_heap_check_test.cc`,
  `tests/gmouse_cursor_frame_test.cc` (compile flags in
  `docs/first-person-walls.md`).
