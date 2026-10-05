# First-person input integration

The previous build projected the highlighted hex but `check_move` still called
`tile_num` on the mouse's isometric screen coordinates. Object hit testing also
used the covered isometric scene. This change connects input to the first-person
presentation without replacing native movement or action execution.

- `first_person_target_tile` owns the ground-ray projection used by highlighting,
  `check_move` (walk/run), movement cursor placement and AP/path preview. It uses
  the existing floor basis and rejects sky, distant rays and out-of-viewport
  positions. Window origins are accounted for.
- The isometric screen-boundary guard is bypassed only in first person; the
  projected target still goes through the engine's bounded `tile_num` lookup.
- Wall and billboard pixels record object identity only when they win the shared
  depth test. `object_under_mouse` uses that visible pixel in first person,
  preserving type filters. A foreground wall blocks objects behind it. Empty
  and transparent pixels do not fall back to the hidden isometric scene.
- Pick results are checked against live objects and IDs before use. Toggle,
  viewport size, elevation and camera position/rotation guard stale snapshots.
- Native walk/run, combat AP, interaction handlers and third-person input remain
  in place. The pointer now draws after walls and objects.

Remaining limitations: ground movement is still a ground-plane intersection,
not a surface hit on a wall. An obstructed destination follows native pathfinding.
Object picking reflects the last rendered scene. Wall topology and FRM material
reconstruction are unchanged in this commit and still need visual improvement.
The native isometric hover-menu artwork is not reconstructed in this pass.

## Checks

The standalone input test supplies a minimal engine fixture and discards the
uncalled renderer at link time. No game assets or SDL are needed:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -fsanitize=address,undefined -Isrc tests/first_person_input_test.cc \
  -Wl,--gc-sections -o /tmp/fallout-input-test
/tmp/fallout-input-test
```

Input and wall regression tests passed with ASan/UBSan, with LeakSanitizer disabled
for this environment. All changed C++ translation units compiled, using SDL
2.26.1 headers for the engine files. Full linking and gameplay testing were not
performed here.

On the Deck, test movement mode by clicking a nearby highlighted floor hex,
turning and repeating. Click sky after selecting a floor tile: it must not walk
to the old destination. Switch to arrow mode and click a visible item or scenery
object, then check third-person controls and the interface bar.

## Established Deck build workflow

Run these from the ordinary Deck terminal. The SDK command exits automatically
when the build finishes, so launching afterward occurs outside the build sandbox.

```sh
cd ~/fallout1-ce-first-person && git pull --ff-only && \
flatpak run --filesystem=home --share=network --command=sh \
  org.freedesktop.Sdk//24.08 \
  -c 'cd ~/fallout1-ce-first-person && cmake --build build -j4'
```

After a successful build:

```sh
cd "/home/deck/.local/share/Steam/steamapps/common/Fallout/" && \
  ~/fallout1-ce-first-person/build/fallout-ce
```
