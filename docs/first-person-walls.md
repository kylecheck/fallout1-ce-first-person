# First-person wall reconstruction

This milestone replaces native-neighbor connection guessing with Fallout's own
wall prototype classification. It does not change floor/world mapping, input,
the R5/F11/0 toggle, native movement, mouse targeting, or the shared depth
buffer.

## What the map snapshot established

The VAULTBUR diagnostic showed that visible walls already carry a structural
class in the high bits of prototype `extendedFlags` (the engine's Wall Light
Type field):

- `0x00000000`: north/south
- `0x08000000`: east/west
- `0x10000000`: north corner
- `0x20000000`: south corner
- `0x40000000`: east corner
- `0x80000000`: west corner

Low action bits such as `0x2000` are ignored for structural classification.

Long east/west runs in the snapshot alternate between neighboring hex columns.
Their hex centers therefore zigzag by half a world-Y unit even though the wall
art represents one straight wall. The renderer now corrects that parity offset
and emits one straight east/west line. North/south pieces already align on the
world-Y basis.

The snapshot also contained many `block.frm` wall and scenery objects around
visible wall runs. Their 1x1 art remains invisible in first person. Wall-type
blockers are now used only as conservative structural evidence when a chain is
proven by compatible visible wall geometry on both ends; raw adjacency still
does not create visible branches.

## Geometry rules

Wall structure is now chosen in this order:

1. Read the wall PRO's `extendedFlags`.
2. Classify the piece as north/south, east/west, or one of four corner types.
3. Emit fixed world-space geometry from that class and the established tile
   world mapping.
4. Camera transform, near-plane clipping and depth rasterization happen only
   after structural geometry exists.
5. FRM cropping/height/texturing remain a separate material approximation.

Object rotation and FRM width/anchor no longer select structural orientation.
Native hex adjacency also no longer creates wall branches. This specifically
removes the false zigzags and three/four-way junctions seen in the previous
adjacency build.

Corner arm directions are based on the VAULTBUR wall-map relationships and are
still considered a runtime-validation target. Texture reconstruction for
corners remains approximate because one isometric FRM is being projected onto
two first-person planes.

## Geometry-only debug view

Launch with:

```sh
FALLOUT_FP_WALL_DEBUG=1 /path/to/fallout-ce
```

The debug view bypasses FRM pixels and uses the same depth/clipping path with
solid colors:

- red: north/south
- green: east/west
- blue: north corner
- yellow: south corner
- magenta: east corner
- cyan: west corner

This is the preferred validation mode for the next Deck recording. Straight
runs should no longer zigzag as the camera turns. Corners should stay fixed in
world space and should not sprout extra branches merely because another wall
occupies an adjacent hex.

## Automated checks

No proprietary game data or SDL is required for the structural regression test:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  -fsanitize=address,undefined tests/first_person_wall_test.cc \
  -o /tmp/fallout-wall-test
/tmp/fallout-wall-test
```

The test verifies Wall Light Type masking, both straight wall axes, east/west
hex-parity correction, the four corner direction patterns and continuation
deltas, seam overlap, and near-plane UV clipping.

## Export actual map topology

Set `FALLOUT_FP_MAP_DUMP` to an output text-file path to export once per
process, on the first first-person frame after loading a map/save. This remains
independent of `FALLOUT_FP_WALL_DEBUG`.

```sh
cd "/home/deck/.local/share/Steam/steamapps/common/Fallout/" && \
FALLOUT_FP_MAP_DUMP="$HOME/Desktop/fallout-wall-map.txt" \
  ~/fallout1-ce-first-person/build/fallout-ce
```

The tab-separated diagnostic contains map name, elevation, player tile and
rotation, every wall/scenery object on that elevation (including hidden objects
and invisible blocker art), prototype flags, scenery subtype, art name, frame
metadata and six native neighboring tiles.


## Corner-gap correction

The first prototype-driven build still left a large opening at some inside
corners. The cause was structural rather than a missing black wall asset.

East/West wall centers require a +/-0.25 world-Y parity correction because the
hex centers zigzag. North/South wall centers do not. The previous corner helper
applied the shifted center to both axes and then gave the vertical arm a fixed
0.5-unit length. That left a quarter-hex hole on one parity/direction and
overextended the opposite case.

Corners now use two different references:

- horizontal vertex: parity-corrected East/West lattice
- vertical endpoint: the real North/South boundary at worldY +/- 0.5

Real VAULTBUR examples are covered by regression tests, including 13078->13079,
13878->13678/13877, and 13890->13690/13891. Odd-column synthetic cases are also
tested.

The renderer also uses a slightly larger world-space overlap at joins and a
small endpoint-only opaque-pixel repair for sloped isometric FRM edges. These
measures are deliberately local: they should hide projection/alpha cracks
without filling authored windows or turning genuine door openings into walls.


## Rectified wall materials

Fallout wall FRMs are isometric cutouts, not rectangular textures. Treating
their transparent mask literally in first person created large black holes even
when the reconstructed wall plane itself was correct.

The renderer now builds a temporary rectangular material for every visible wall
FRM before projection:

- find the opaque span independently on every source scanline
- stretch that scanline's opaque span across the rectangular material width
- if a sampled texel inside the span is transparent, replace it with the nearest
  opaque texel from the same scanline
- if an entire scanline is empty, borrow the nearest non-empty scanline
- project the resulting solid material onto the existing structural wall plane

This deliberately separates wall structure from 2D sprite alpha. Genuine
openings should ultimately come from map semantics such as doors and their
state, rather than from transparent pixels authored for the original isometric
compositor.

The rectified material is precomputed once per visible wall object per frame,
instead of searching for replacement texels for every projected screen pixel.


## Blocker-backed structural bridges

Some Fallout maps use `block.frm` wall objects as invisible collision/topology
cells between visible wall sprites. In VAULTBUR, for example, the visible west
corner at tile 13090 is followed by a `block.frm` wall at 13290 and then a
visible north/south wall at 13490. Ignoring the blocker leaves a real
first-person hole even though the original isometric scene reads as continuous.

The first-person renderer now treats blocker cells as topology hints rather
than renderable art:

- `block.frm` itself is never textured or exposed as an interactable object
- from each blocker cell, trace both directions along the vertical (+/-200)
  and horizontal (+/-1) structural axes
- a blocker is promoted to synthetic wall geometry only when both directions
  reach compatible visible wall structure while passing exclusively through
  other blocker cells
- the synthetic span inherits its material from a real wall at one end
- if a helper qualifies on both axes, choose the shorter proven bridge rather
  than inventing a four-way intersection
- tracing is capped at six cells so unrelated collision fields cannot connect
  distant rooms

This is intentionally stricter than the old adjacency approach: raw neighboring
objects still do not define wall topology. Blockers are used only as evidence
for a missing span when visible wall structure proves the same axis on both
sides.


## Wall-finish material pass

The wall renderer now treats corner appearance separately from corner
structure.

Straight wall FRMs are rectified with a center-preserving scanline transform:
each source row is aligned by its opaque center and only the narrowed isometric
silhouette is edge-extended. This avoids independently stretching every row,
which was producing striped/checkered distortion in first person.

Corner geometry still comes from the corner prototype class, but each corner
arm now looks for the straight wall that continues that arm. When found, the
arm borrows that straight wall's rectified material and maps the full texture
across the face. The corner FRM is retained only as a fallback. This keeps
corners visually continuous with the walls they actually join.

All structural wall planes now use one nominal world height (1.65) instead of
deriving physical height from FRM pixel dimensions. The source FRM height was
an isometric-art dimension, not reliable 3D geometry, and caused uneven wall
tops.

Blocker-backed bridges are also door-aware. If a blocker trace encounters a
scenery prototype of type `SCENERY_TYPE_DOOR`, the bridge is rejected so a
semantic doorway cannot be permanently sealed by reconstructed wall geometry.
Door open/closed presentation remains a scenery/rendering task.



## Wall milestone status

The wall renderer is now considered structurally complete enough for the next
first-person milestones.

Validated in VAULTBUR:

- straight wall runs hold their world-space shape while walking and turning
- prototype-driven corner geometry produces enclosed rooms
- blocker-backed reconstruction closes large missing spans that the isometric
  renderer supplied indirectly
- semantic door tiles are excluded from blocker bridges
- wall tops use a consistent structural height
- corner arms can borrow compatible straight-wall materials
- depth testing and native movement remain stable

Known limitations intentionally deferred:

- one isolated corner pattern can still fail to produce a convincing visible
  face even though the surrounding geometry is correct
- original Fallout wall FRMs are isometric sprites, so some first-person
  textures remain visibly stretched or distorted
- cosmetic seam/material work should be revisited after doors, scenery and
  other world objects have a proper first-person representation

A test of Fallout's native translucent "egg" sidedness did not fix the remaining
corner case, so that experiment was removed rather than carrying extra
player-relative material logic into later milestones. The original egg remains
a top-down renderer behavior, not part of the first-person wall model.


## Door/scenery milestone

Doors now begin moving out of the generic billboard renderer and into structural
first-person geometry.

For scenery prototypes of type `SCENERY_TYPE_DOOR`:

- collect their live tile, rotation, frame and prototype `extendedFlags`
- place closed doors on the same structural lattice used by walls
- render them as depth-tested vertical planes instead of camera-facing sprites
- write the real door object into the first-person pick buffer, so existing
  Fallout mouse/use logic can target the door directly
- keep door tiles excluded from blocker-backed wall reconstruction
- skip doors in the later generic scenery billboard pass to avoid double
  rendering
- render frame-0 closed doors structurally, but keep non-zero animated/open
  door frames visible through the live scenery pass instead of making the
  door vanish entirely; native Fallout collision, scripts and use behavior
  remain authoritative

This is intentionally a minimal bridge to semantic scenery. The next validation
target is that a closed door visually occupies its doorway, can still be clicked
through the normal Fallout action system, and disappears from the structural
opening when native door animation advances away from frame 0.

## VAULTBUR doorway snapshot and scenery cleanup

The October 5 snapshot (`VAULTBUR.SAV`, elevation 0, player tile 14478)
contains 611 wall objects and 121 scenery objects: 120 generic scenery and
one ladder-up. There are **zero** `SCENERY_TYPE_DOOR` objects on this elevation.
Consequently this room cannot validate the animated-door fallback. Its visible
openings must be investigated as wall/generic-scenery construction, without
inventing interactive door objects or sealing gaps from proximity alone.

The generic scenery pass now skips `block.frm`, matching the existing wall
collector's handling of invisible collision helpers. These helpers no longer
produce billboard pixels, depth, or pick IDs; native collision remains intact.
Critter sprites now use a constant scale of 80 source pixels per world unit,
including live and death frames, instead of normalizing every rat and corpse to
human height. This is an initial visual scale, pending in-game validation.

An explicit `FALLOUT_FP_MAP_DUMP` also writes `<dump-path>.art.txt`. The companion
contains unique wall/scenery frames within 18 hexes of the player, their FID,
direction, frame number, dimensions, indexed pixels, and the 6-bit RGB palette.
Index zero is transparent. Match these entries to the topology table by FID
and direction. This allows identification of doorway art before defining frame
geometry. The export runs once per launch with the existing map dump, not during
normal gameplay. It contains local game artwork and is not a repository fixture.

No wall lattice, blocker-span reconstruction, door state/use behavior, or native
movement changes are included in this cleanup.

## Verified doorway overhead spans

The nearby source-art export identifies two paired wall-art constructions:

- `dv1036.frm` at tile 14677 and `dv1035.frm` at 14679 flank empty tile 14678.
- `dv1043.frm` at tile 15092 and `dv1044.frm` at 15492 flank empty tile 15292.

The new visual profiles require these exact art pairs, matching structural
orientations, and the corresponding tile spacing. They are asset-based, not
hard-coded to these example map coordinates. Row wrapping, ordinary wall art,
and mismatched pairs are rejected. An existing wall, wall blocker, or native
door on the middle tile also prevents this additional geometry.

For a verified pair, the renderer adds only an overhead span across the empty
middle segment, from height 1.35 to the existing wall height 1.65. It samples
the upper 28% of the broad header piece's rectified material. These dimensions
and texture sampling are initial visual approximations requiring Deck review.
The existing flank wall geometry and passage width remain untouched. Header
pixels use normal depth testing and retain a real wall object as pick owner;
no new engine object, use action, or collision is created. The added spans are
kept outside the topology/corner-material collection so they cannot change
established wall reconstruction.

This is an overhead framing pass, not completed 3D doorway modeling. Narrower
posts, jamb depth, windows, and the separate `bvs40.frm` round vault-door scenery
remain deferred. Unknown gaps remain open rather than receiving guessed frames.

Regression coverage: `tests/first_person_doorway_test.cc` checks both verified
pairs and rejects wrong art, orientation, spacing, map bounds, and row wrapping.
The existing wall-lattice regression suite remains unchanged.

## Fixed generic vault doorway scenery

The room snapshot at player tile 15295 identifies the complete doorway frame as
`v13secr6.frm`: generic scenery object 532 at tile 16696, with extended flags
`0x08002000`. It is not a native animated door. Its surrounding east/west wall
run ends at 16693 and resumes at 16699, leaving five lattice columns for the
complete frame. The local blocker cells are retained as native collision data.

This specific artwork now renders on a fixed east/west structural plane with
the existing parity correction, a five-column width, and wall height 1.65.
Its live frame is copied and vertically sheared by column to straighten the
verified 151x142 sprite's rising top edge (36 source pixels of rise). The
112-pixel face height is scaled with live frame dimensions. Unlike solid wall
material rectification, this transform retains index-zero transparency in the
passage. Source pixels outside the live FRM stay transparent.

The generic billboard pass skips this asset to prevent duplicate rendering.
The plane uses the shared clipping/depth path and writes the actual scenery
object into the pick buffer only at visible pixels. Native scripts, use logic,
and collision remain authoritative. Its baseline is projected on the ground
plane, avoiding the billboard pass's screen-edge baseline clamp. Other generic
scenery continues to use the existing renderer.

This is an asset-specific first structural scenery profile. The world placement
and material orientation require Deck validation; it is still a flat frame
without jamb thickness or a modeled back face. Elevator artwork and unrelated
openings are not classified from this asset.

## Broad straight-face material and opening pass

Single-face north/south and east/west wall textures now undo isometric shear
by source column: retain each column's horizontal location, start it at its
first opaque pixel, and use the greatest opaque column span as face height.
Solid materials repair empty samples only within their own column and repeat
its bottom edge where necessary. This replaces horizontal scanline shifting,
which folded posts into stripes and dragged unrelated pixels into openings.
Corner and unknown-class fallback materials retain their existing transform;
corner arms still prefer their compatible straight continuation art.

Verified window/doorway profiles (`dv1010` through `dv1013`, `dv1035`, `dv1036`,
`dv1043`, `dv1044`, `velvdr03`, and `velvdr04`) preserve transparent interior
pixels and short/header-only columns. Corner arms do not borrow these cutout
materials, so a nearby window cannot create a new hole in a solid corner.
The transform was visually checked against the exported original frames.

The elevator wall-frame pair `velvdr04.frm`/`velvdr03.frm` at 14302/14306
establishes a three-cell overhead span. The profile requires exact artwork,
matching east/west classes, and same-row four-column separation. Existing
visible walls, wall blockers, or native doors within the span reject it;
scenery collision helpers retain their native behavior. The upper trim texture
is divided continuously across the three overhead cells, not repeated on each.

Wall segment positions, movement, collision, and native use behavior remain
unchanged. Frames remain flat and do not yet have modeled jamb thickness. The
newly transparent visual openings do not grant movement through native walls.
Material orientation/appearance and the elevator trim require Deck review.
For a comparison launch, `FALLOUT_FP_MATERIAL_LEGACY=1` restores the prior
material transform; it does not change topology or the new overhead profiles.

Explicit map/art exports now include unique wall/scenery frames for the whole
current elevation. This supersedes the earlier 18-hex art-export limit and
avoids repeated exports merely because another structural scenery object is
farther down the corridor. Exports remain opt-in and run once per process.

`tests/first_person_material_test.cc` verifies shear removal, interior alpha,
header-only columns, solid face filling, and empty input. The doorway suite now
includes the elevator pairing and wrong-spacing rejection; existing wall
geometry regression tests remain unchanged.

## Large arch depth and nearby scenery grounding

The verified `v13secr6.frm` frame now has front/back faces at +/-0.06 world
units from its existing central wall-lattice plane. Narrow outer returns connect
the two faces. Inner jamb returns are located from the central transparent run
at 80% of the rectified frame height and sample an adjacent solid source column.
Source alpha is preserved up each return. The arch opening remains clear;
no collision, interaction, central placement, or camera-height changes are made.
This is a first depth treatment: curved inner arch surfaces and the horizontal
header underside are still absent, and the jamb sampling needs Deck review.

Generic billboard baselines now project their actual ground anchor even below
the viewport; only draw bounds clip them. Previously clamping that baseline to
the viewport bottom raised nearby scenery and corpses as the camera approached.
This correction applies to the round vault-door scenery and damaged panel while
they retain their existing billboard orientation. The damaged `v13secr4.frm`
pixels were absent from earlier radius-limited exports; the full-elevation art
export added in the preceding build will supply them for structural conversion.

The modified source passes the warning-clean compiler syntax check. Existing
wall, doorway, and material regression suites pass. New depth and grounding
behavior requires in-game visual testing against the user's assets.


### Damaged wall scenery: first structural pass

The full-elevation `fallout-depth-test.txt.art.txt` export contains 121 unique
frames, including `v13secr4.frm` (342x223). Unlike the clean arch, this art
combines a damaged wall, a side face, top caps and floor rubble. It must not
be rectified by treating its complete opaque silhouette as one wall face.

The first pass crops its front face to source columns 40..341, sampling from
source row 91 at the left to 28 at the right, for 105 vertical pixels. Index
zero remains transparent, including the damage. The crop removes the major
baked side/floor silhouette; small attached debris remains in the face.
It uses the existing structural scenery depth/pick pass, two thin faces and
outer returns, and is excluded from camera-facing billboards.

Its provisional horizontal span is eight lattice column intervals, centered
on the native object. In the exported room this places the ends on columns
92 and 100 around the object at column 96. This is an asset-specific visual
profile, not a change to world coordinates or blocker reconstruction. Verify
both end joins and front/back alignment on Deck before calling it complete.
The full broken side, top surfaces and separate floor rubble are deferred;
this pass does not claim to reconstruct the complete original solid.
Native blockers, scripts, collision and object state remain untouched. Picking
opaque pixels returns the original scenery object; the hole writes no depth
or pick entry. No passage or new gameplay is inferred from the visual hole.

Validation: strict C++17 syntax compilation and existing wall lattice,
doorway and material regressions passed. The cropped source face was inspected
visually; room placement still requires an in-game test.


### Visibility radius and topology support

The first-person collector previously rejected walls, doors and scenery beyond
18 hexes. That cutoff caused visible pop-in in long rooms. It also removed
wall/blocker evidence before six-cell bridge reconstruction and corner material
borrowing, which could make boundary pieces change appearance as the player
moved. The native character egg is an isometric screen-space masking effect;
first-person wall materials read raw FRM pixels and do not apply that mask.

The visible radius is now 48 hexes for walls, structural scenery, doors and
billboards. Wall/blocker collection reaches 54 hexes, retaining six extra rings
for the existing structural searches. Supporting walls outside 48 are evidence
only, not drawn. All existing lattice geometry, opening rules, native collision,
use and object state are unchanged. This is still a finite range; it does not
claim to solve every isolated corner or reconstruct absent map geometry.

Validation: strict C++17 compilation and standalone lattice/near-plane,
doorway and material regressions. Deck testing should check distant room walls
and whether remaining corner faults persist once the supporting walls are in
range. Longer range may increase frame cost; no on-Deck performance result is
claimed before that test.


### Walls MVP checkpoint and independent camera heading

Deck testing confirmed the longer render range. Walls are an MVP checkpoint
for the current tested rooms, with isolated corners and asset-specific polish
still deferred. No claim is made that every map has been validated.

The camera previously read `obj_dude->rotation` every frame. Native hex
pathfinding rotates the character to face each path segment, making click-to-
walk steer the camera and making backpedal reverse the next forward input.
First-person heading is now presentation-owned. Enabling first person seeds
it from native facing; left/right turns change it in the existing 60-degree
increments. Character facing can change freely without changing the view.
Up/down request native one-hex movement along/opposite the camera heading.
Click-to-walk still uses native destination, pathing, collision and animation.
Floor picking, scene projection and pick-buffer validity use the same heading;
a deliberate camera turn invalidates old picks.

This pass removes automatic yaw changes. Position still follows native tile
centers, so lateral hex stepping/position snapping may remain. Smooth position
interpolation and continuous mouse look are separate future presentation work.

Validation: strict first-person C++17 syntax compilation and an actual camera
function harness covering native facing changes, turn wrap, disabled turns and
mode reentry. Run the harness without SDL/game data using:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Isrc tests/first_person_camera_test.cc src/game/first_person.cc -Wl,--gc-sections -o /tmp/fp-camera-test
/tmp/fp-camera-test
```

Full `game.cc` compilation locally requires unavailable SDL headers; the Deck
SDK build and movement/targeting playtest are the integration checks.


## Scene-agnostic interaction/combat foundation

First-person object picking now has a small screen-space assist radius after an
exact visible-pixel miss. Fallout's original FRMs are sparse isometric
silhouettes, which made small switches, items and distant critters too difficult
to select when treated as literal one-pixel targets in perspective.

The assist is intentionally generic:

- exact rendered pixels remain authoritative
- only a small nearby pick-buffer neighborhood is searched after an exact miss
- generic snapping ignores walls so a nearby wall cannot steal a click from a
  small scenery/item/critter target
- explicit wall queries still work
- the returned object is still the real live Fallout `Object`
- scenery use continues through native use/script logic
- combat targeting continues through native critter selection,
  `combat_attack_this`, weapon/AP/range checks and combat state

This is the foundation for testing doors, containers, ladders, elevators,
switches and enemies across maps without introducing location-specific
interaction code. Visual presentation can still be specialized later by
semantic scenery class, but gameplay authority stays in Fallout's existing
systems.


## Interaction/combat foundation validation

The October 5 Steam Deck test validated the generic first-person interaction path
across multiple native object types:

- scripted elevator scenery could be examined and opened its native elevator UI
- critters could be acquired from first person and entered Fallout's normal
  combat/weapon checks (including the native no-ammo result)
- terminal scenery returned its native examine text
- the close-range billboard interaction footprint remained usable
- object targeting remained stable at longer viewing distances

The 24-step presentation heading (15-degree increments) was also validated in
the same vault rooms. It substantially reduces the six-direction "bladed" view
without changing Fallout's six-direction hex movement, collision, pathing, or
animation authority.

This marks the generic picking/interaction/combat-target selection layer as a
usable foundation. Remaining combat work is primarily feedback/presentation
(AP, attack state, selected target, weapon state) rather than replacing native
combat semantics.


## First-person combat presentation

The viewport now mirrors native Fallout combat state instead of replacing it.
A compact top-left status panel shows the current mouse mode at all times. In
combat/attack mode it additionally shows current AP, attack AP cost, ammo for
weapons that use it, and the hovered critter.

For a hovered combat target the panel reports either native hit chance or the
same pre-attack failure condition used by Fallout's combat code (no ammo, out
of range, not enough AP, blocked aim, dead target, or crippled-arm weapon
restrictions). The underlying attack still goes through Fallout's normal combat
functions; this is presentation only.


## Native controller free-look foundation

The first-person camera can now read the native SDL game-controller right-stick
X axis independently of Fallout's mouse cursor. Camera heading is stored
continuously rather than only as 15-degree slots, while keyboard/D-pad turn
inputs still move in the existing 15-degree increments.

At full right-stick deflection the camera turns at roughly 150 degrees/second,
with an 18% dead zone. The right stick changes presentation only; native
Fallout movement still resolves the camera heading to the nearest of the six
hex directions.

This is intentionally the first half of the Steam Deck control redesign. Steam
Input should expose the right stick as a normal gamepad/joystick axis, while
the right trackpad remains mouse input for Fallout's cursor and UI. Left-stick
camera-relative hex movement is the next controller milestone after this
free-look path is validated.


## Vertical controller free look

Native right-stick look now consumes both SDL controller axes. Horizontal motion
continues to rotate the camera freely; vertical motion adds a clamped pitch
range of roughly +/-18 degrees at about 90 degrees/second.

The software renderer implements pitch by shifting the shared projection
horizon. Floor sampling, wall/object projection, target-tile conversion, and
the pick buffer all use that same pitch-aware horizon so visual aiming and
native hex targeting stay aligned.

The right stick must be exposed by Steam Input as a normal joystick/gamepad
stick. It should not also emit mouse movement; the right trackpad remains the
dedicated Fallout mouse cursor.


Vertical look now uses asymmetric pitch limits: about +18 degrees upward and
-40 degrees downward. Fallout maps do not provide meaningful ceiling geometry,
so the tighter upward cap avoids exposing mostly empty/black space, while the
deeper downward range lets the player inspect essentially the full ground plane
and use the trackpad for precise nearby hex/object targeting.


## Full-screen first-person presentation

First-person mode now renders into its own full-screen GNW window rather than
being confined to Fallout's original map viewport above the 100-pixel interface
bar. Entering first person hides the native interface bar but leaves the
underlying Fallout interface systems enabled; inventory, Pip-Boy, dialogue,
elevator panels, and other native modal windows can still open above the
first-person view.

The first-person mouse/pick path now uses that full-screen presentation window,
including the lower portion of the screen that previously belonged to the
interface bar. Leaving first person restores the interface bar when it was
visible before entering first-person mode.


## Free-look input ownership fix

Right-stick free look is now polled from the gameplay/background update path
instead of from the renderer itself. This prevents camera input from freezing
after inspect/combat cursor-mode transitions when no unrelated map redraw is
pending. A successful controller look update explicitly requests a map refresh.

First-person mode also suppresses Fallout's native edge-scrolling cursor logic.
The full-screen overlay owns the playfield while active, so pushing the cursor
toward an edge should no longer turn it into the legacy map-scroll arrows or
let that hidden isometric interaction layer interfere with free look.


## Right-stick camera speed tuning

Native right-stick yaw now turns at roughly 270 degrees/second at full
deflection (18 of the internal 15-degree heading units per second), up from
about 150 degrees/second. The dead zone and continuous heading model are
unchanged; this is a responsiveness pass so free look reads as a camera rather
than as a slightly faster version of the 15-degree D-pad turn step.


## First-person eye-height tuning

The first-person eye height was raised from 0.50 to 0.62 world units. The same
shared value is used by the floor projection, wall/object placement, visible
hex projection, and screen-to-world target-tile conversion so raising the
camera does not desynchronize the cursor from the rendered ground.

This is an initial calibration pass rather than a final physical scale. Vault
window sightlines are the preferred visual reference for the next adjustment.


Eye-height calibration was nudged again from 0.62 to 0.68 world units after
runtime testing against Vault window sightlines. This keeps the camera clearly
above the earlier crouched-looking perspective without materially changing the
established world scale.


## First-person redraw path optimization

Controller camera motion now redraws the full-screen first-person presentation
window directly instead of forcing each right-stick sample through Fallout's
isometric `tile_refresh_display` path. The map draw callback also stops
redrawing the hidden isometric display window underneath the first-person
overlay.

This removes redundant dirty-rect/compositing work from continuous camera
motion and is intended to address the slideshow-like feel observed while
turning. Native simulation and map refreshes still remain authoritative for
world changes and movement.


## Camera response and eye-height follow-up

Vertical right-stick look now runs at roughly 180 degrees/second at full deflection, up from 90, so pitch responds more closely to the faster horizontal free-look feel. The pitch limits themselves are unchanged.

First-person eye height was raised again from 0.68 to 0.74 world units, matching the previous +0.06 calibration step. The shared projection/targeting eye-height value remains unified so floor targeting stays aligned with the higher viewpoint.


## Native left-stick movement foundation

The SDL left stick now supplies camera-relative movement intent directly to the first-person controller layer. The stick vector is converted into a direction relative to the current continuous camera heading, then quantized only at the final step to Fallout's six native hex directions.

Movement still uses Fallout's native animation/pathing/collision request path (`register_object_move_to_tile`), so the stick does not move the player through arbitrary analog world coordinates. Forward, backward, and lateral/diagonal stick directions therefore become native neighboring-hex requests while preserving engine authority.

A 32% movement dead zone and a short 120 ms repeat gate prevent idle drift and command spam. Steam Input should expose the left stick as a normal joystick/gamepad stick for this path; keyboard/D-pad emulation on the left stick should be disabled to avoid duplicate movement/turn commands.


## First-person HUD and duplicate-stick guard

The first-person overlay now carries a minimal persistent native-state HUD: current/max HP at the lower left, and ammo at the lower right with AP added while combat is active. These values are read from Fallout's existing critter, stat, interface, and weapon state rather than introducing replacement gameplay state.

A controller activity guard also prevents legacy left/right arrow events from rotating the camera while the native left stick is actively supplying movement. This handles Steam Input layouts that still leak the older arrow binding alongside the SDL joystick axis, eliminating the small yaw bump seen immediately before lateral hex movement while preserving D-pad camera turns when the left stick is centered.


## First-person combat target treatment

Attack/crosshair mode now gives the hovered critter a Fallout-green first-person targeting treatment using the existing depth-tested pick buffer. A checker-pattern overlay recolors only the critter's actually visible pixels, so foreground walls and scenery still occlude the target correctly while enough original sprite art remains visible underneath.

The target brackets also switch to the same green treatment in attack mode, and a compact native-font label appears above the target with its name plus current native hit chance. If Fallout rejects the shot, the label mirrors the native reason instead (for example NO AMMO, OUT OF RANGE, NO AP, DEAD, or BLOCKED). Native combat remains fully authoritative; this is presentation only.


## First-person equipped weapon presentation

The first-person overlay now presents the currently active native weapon using that item's original Fallout inventory FRM from the user's local game data. Transparent bounds are cropped, the art is nearest-neighbor scaled without changing its aspect ratio, and it is placed as a lower-right 2D/2.5D viewmodel layer. Switching the active hand or weapon therefore changes the viewmodel automatically without introducing duplicate equipment state.

Attack/crosshair mode gives the same weapon art a slightly larger, raised ready pose; normal movement keeps it lower in frame. This is intentionally a presentation prototype using original local assets, not a final reconstructed weapon model. Native weapon state, ammo, AP, attacks, and animations remain authoritative.


## First-person weapon presentation layer

Weapon rendering is now split into two concerns: Fallout remains authoritative for the active item, hit mode, native critter animation, ammo, AP, and combat; the first-person layer translates that state into a presentation pose. The viewmodel currently supports Lowered, Ready, Attack, and Reload poses.

Presentation profiles are keyed from Fallout's native weapon animation category (pistol, SMG, shotgun/rifle, minigun/launcher, and melee families). Each profile can independently tune width, horizontal framing, and vertical placement per pose. The current inventory FRM remains only a temporary visual source, but future first-person art or 2.5D reconstructions can plug into this layer without changing equipment/combat logic.

Attack pose detection follows the player's native critter animation (melee, throw, point/fire/burst/continuous fire). Crosshair mode selects Ready; reload hit modes select Reload; normal exploration selects Lowered.


## Cross-map combat targeting and large-room performance

First-person attack mode now treats the center reticle as the authoritative combat pick point. Right-stick camera aim therefore drives the same critter selection used by the green target treatment, hit-chance HUD, hover feedback, and native `combat_attack_this` path. Normal interaction mode still uses the real mouse/trackpad pointer.

Wall materials are now cached across first-person frames instead of being re-extracted and rectified from their source FRMs on every redraw. This removes a large amount of repeated CPU work in bigger maps such as the starting Vault, while leaving established wall geometry, topology reconstruction, and projection math unchanged.


## Unified center-reticle world input and render-buffer reuse

All first-person world modes now use the center reticle as the authoritative pointer. Move targeting, inspect/use object selection, combat target selection, hover feedback, and the visible crosshair therefore reference one camera-centered point instead of allowing the hidden/free mouse cursor to drift independently. The separate first-person mouse pointer overlay has been removed; native modal interfaces still retain normal mouse ownership when they appear above the first-person view.

The renderer also reuses its large depth and pick buffers across frames instead of reallocating them for every camera update. Hover-object bounds are recorded during billboard projection and reused for highlighting, replacing the previous full-frame pixel scan used to rediscover a selected object's screen bounds. These changes are aimed specifically at reducing large-map camera hitching without changing world geometry or native movement/combat authority.


## Dedicated first-person mode cycle and GPU composition bridge

First person now owns its world-mode cycle instead of delegating to Fallout's stock mouse-mode toggle. The first-person sequence is MOVE -> INTERACT -> ATTACK -> MOVE, so ATTACK remains available even before native combat has begun; native combat itself still starts only when the player actually attacks a target.

A dedicated GPU composition hook now runs after Fallout CE uploads its software framebuffer to SDL and before `SDL_RenderPresent`. The center reticle has been moved into this GPU pass as the first visible migration step, with mode-aware coloring and suppression while a native modal window owns the center of the screen. This does not yet move the world renderer off the CPU, but it establishes the render stage where floor, structural walls, billboards, weapon presentation, and HUD can be migrated incrementally without replacing Fallout's simulation or the existing first-person coordinate model.


## Pre-combat attack-mode persistence and duplicate move suppression

Fallout's background mouse process normally forces any cursor mode at or above CROSSHAIR back to MOVE when the physical mouse is not over the native map window and combat has not yet started. First person now bypasses that legacy reset, allowing its explicit MOVE -> INTERACT -> ATTACK cycle to remain stable before combat begins.

The legacy KEY_ARROW_UP and KEY_ARROW_DOWN movement paths are also suppressed while native left-stick movement is active, matching the existing left/right guard. This prevents Steam Input from sending both joystick intent and legacy arrow events that can queue extra forward/backward hex movement after the stick is released.


## Native combat initiation, options overlay, and first textured GPU element

First-person attack mode can now initiate native Fallout combat directly from the center-reticle target. Outside combat, a crosshair click builds the same native combat-start structure used by Fallout's combat system and enters combat with the selected live critter as the initial defender; once combat is active, normal `combat_attack_this` handling remains unchanged.

The full-screen first-person overlay can now be temporarily hidden while the native options/start menu is open, then restored afterward. This establishes the pattern for letting native modal UI appear above first person instead of being trapped behind the overlay.

The equipped first-person weapon viewmodel is now the first textured presentation element moved out of the CPU framebuffer. Its original indexed Fallout inventory art is converted through the active game palette into an SDL GPU texture, cached, and scaled/composited by the GPU immediately before present. The world floor/walls/billboards are still software-rendered at this stage, so this is an architectural migration step rather than the large performance win; the next GPU work should target the expensive world passes, beginning with the floor and structural geometry.


## Dedicated first-person mode state and floor migration bridge

First-person MOVE / INTERACT / ATTACK state is now stored independently from Fallout's legacy mouse-mode state. This prevents the native pre-combat cursor rules from silently removing ATTACK mode. First-person left-click handling now routes directly through that dedicated state: ATTACK can start native combat on the center-reticle critter target, while INTERACT uses the same live-object use/examine paths as before.

While the world renderer is being migrated to the GPU, the software perspective floor now samples one authoritative projection lookup per 2x2 output block instead of per pixel. This reduces the most expensive current floor loop by roughly four times while preserving the established world-to-isometric mapping and leaving picking/combat coordinates unchanged. This is a temporary performance bridge, not the final GPU floor implementation.


## Unify right-click mode cycling and HUD with dedicated state

The dedicated first-person mode was already used by M, the reticle and click
routing. Right-click still called the legacy cursor cycle, which deliberately
skips CROSSHAIR outside combat. The mode/combat text also still read legacy
state, so its label could disagree with the reticle and actual action.

Right-click now invokes the same first-person MOVE -> INTERACT -> ATTACK cycle
as M. The mode/combat text reads that same state. Native isometric right-click
behavior is unchanged. This addresses an input/state mismatch, not GPU warm-up
or a proven first-combat initialization defect. Validate from a fresh load:
cycle into ATTACK before combat, check HUD/reticle agree, target a live critter,
and click; then repeat after combat ends. World rendering remains software;
GPU composition currently covers the reticle and weapon texture.


## GPU world rasterization: first complete world backend

The default first-person world path now uses an offscreen OpenGL 3.3 context
created through SDL. The existing SDL presentation renderer is retained; its
current context is restored after each world frame. OpenGL functions are loaded
through SDL, so this adds no separate GL linker or SDK package requirement.
The offscreen window is hidden. Unsupported initialization or a GL frame error
selects the established software world path automatically. Setting
`FALLOUT_FP_SOFTWARE=1` explicitly selects that comparison/fallback path.
Wall debug mode also uses the software path.

The GPU draws native floor art, reconstructed walls/corners/lintels, structural
doors/large scenery and live animated scenery/critter/item billboards. The CPU
still prepares native objects, materials and the same structural segments.
Indexed FRMs are uploaded as R8 textures with nearest sampling. Palette-index
zero is discarded; GPU depth and R32UI owner attachments agree at opaque pixels.
A separate depth-tested assisted-pick attachment retains billboard opaque-bound
interaction footprints while rejecting occluded targets. Native object IDs and
live-pointer validation remain authoritative after readback; there is no new
collision, combat or interaction simulation.

Floor geometry inverts the same existing world-to-isometric basis. Original
`square_coord` places each FRM, and a reusable per-art mask built with native
`square_num` ownership removes overlapping rectangular margins. The shader
handles perspective interpolation and near-plane clipping. The floor retains
its 36-unit far cutoff; the visible wall collector remains 48 hexes with 54
hexes of topology evidence. Neither wall lattice nor camera movement changes.

To preserve the existing native window compositor, this first backend reads
indexed color, depth, exact IDs and assisted IDs back once per frame. It avoids
the former CPU floor/wall/door/billboard pixel rasterizers, but synchronous
readback and remaining CPU material/geometry preparation can still limit
performance. No Deck FPS improvement is claimed before measurement. HUD text,
target highlights and native modal menus remain lightweight software UI; the
existing SDL GPU weapon and reticle composition follows the world output.

Validation: compiled all modified C++ translation units with SDL headers; new
backend and test compile with strict warnings. Executed the backend using SDL's
offscreen GL driver: indexed color, transparency, scene depth, exact and assisted
picking, wall occlusion, near clipping, framebuffer resize, context restoration
and explicit software selection passed. Existing lattice/clipping, doorway and
material regressions also passed. The full game/Deck integration still requires
the user's SDK build and visual/input test with Fallout data.

The standalone GPU check can be compiled on Linux with:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc $(sdl2-config --cflags) tests/first_person_world_gpu_test.cc src/game/first_person_world_gpu.cc $(sdl2-config --libs) -o /tmp/fp-world-test
SDL_VIDEODRIVER=offscreen /tmp/fp-world-test
```

Deck verification should cover the vault and cave floors, both sides of doors,
transparent windows, moving rats, target selection behind walls, opening native
menus, and leaving/reentering first person. The terminal logs successful world
GPU initialization or software fallback, making the selected backend observable.


## Fresh-combat preview, close-ground aim and scene scheduling

Fresh characters can have zero current combat AP before their first turn. The
first-person preview formerly passed that value into native bad-shot checking,
showing NO AP outside combat even though an attack can initiate a native turn.
`combat_check_bad_shot` now accepts an optional AP-check flag, enabled by default
for every existing actual attack. Only first-person previews omit it outside
combat. Native ammo, range, death, crippled-arm and obstruction rules still
apply; no AP or native object state is temporarily changed for a preview. The
HUD uses maximum native AP for the pre-combat preview and current AP in combat.

The native player-turn flag is established before its requested initial attack,
which avoids silently rejecting that attack when a loaded combat state has the
flag cleared. The original native attack, turn initialization and AP consumption
remain authoritative. Whether these fixes explain every symptom in the cave
still requires testing from the fresh save.

Looking down previously clamped the horizon to the viewport, making nearby
floor-level targets impossible to center. The horizon can now leave the screen,
with downward pitch extended to 55 degrees. Background fill bounds and software
floor loop starts are clamped separately to avoid negative buffer offsets. GPU
ground-clear scissoring also clamps safely. Both picking and scene projection
use this same horizon. World coordinates and wall lattice are unchanged.

Repeated native map dirty rectangles now request a world refresh rather than
immediately doing full GPU rasterization/readback each time. Presentation flushes
one pending scene; a world click also flushes pending work to avoid stale picks.
A suspended options overlay keeps pending work without rendering beneath it.
This specifically reduces redundant work in scenes with multiple independently
moving critters; no measured Deck frame-rate gain is claimed yet.

Validation: modified translation units compile with SDL headers. Regression
checks cover near-ground center-ray reach at 1280x800, 800x600 and 640x480,
bounded background fills, coalescing 100 requests into one scene, modal deferral
and click-flush behavior. The GPU depth/transparency/picking/context checks pass.
An isolated compiled copy of the native bad-shot function verifies fresh AP-zero
previews still enforce ammo/range/death and actual attacks still enforce AP.
Integration test: load the untouched cave save, select ATTACK before any combat,
center a nearby rat and click; compare movement responsiveness outside combat
with the small vault and verify close-ground aim and options-menu return.

## Native gameplay-loop foundation

Deck testing of cf66c0a reports a substantial responsiveness gain in the cave
and vault, with attack initiation working before the first combat. This pass
builds on that renderer; wall geometry, projection, depth/pick ownership and
rat simulation speed are unchanged.

- **F8 action menu:** clickable native modal command list over the first-person
  scene. Arrows select, Enter executes, Escape/F8 closes. The menu pauses native
  map background processes and restores their previous enabled state. Commands
  are queued after closing, so Space/end turn and Enter/end combat reach the
  native combat input loop. End combat can still be refused by native rules.
- **Combat:** R reloads the active hand through `intface_use_item`, preserving
  its native ammunition, AP and sound handling. B switches hands; N cycles the
  native primary/secondary/aimed/reload choices. ATTACK with reload selected
  executes reload. The HUD names the selected action and displays turn state.
  Native aimed attacks retain the body-part selector and its own hit chances.
- **Interaction:** M/right-click still cycles MOVE/INTERACT/ATTACK. Reticle
  prompts identify pickup, fixed-container open/loot/close, talk, corpse loot,
  scenery use, door open/close and native locked state. E examines the target.
  Existing native actions own approach, AP, locks, scripts and transitions.
  Portable containers retain native pickup behavior. A reversed art-null check
  in native fixed-container animation scheduling is corrected (successful art
  locks now provide the action frame and are released; failures skip it).
- **Skills/items:** native skill and use-item targeting take precedence over
  the three first-person modes. Pick targets still come from the GPU/CPU
  object buffers and pass to the existing skill/item action branches. F8 also
  exposes use-held-item, inventory, skills, Pip-Boy, character and automap.
- **Feedback:** native display messages appear for six seconds in the viewport,
  including lock, range, AP, examination and script messages. This first pass
  shows the latest message on one line; the original native log is retained.
- **Modal ownership:** scoped, nested overlay suspension covers inventory,
  loot, use-inventory-on, dialogue, aimed-shot selection, skills, character,
  Pip-Boy, automap, options/pause/confirmation, save/load and elevator UI.
  Loading and map transitions suspend the scene while objects are replaced;
  final resume clears cached picks, resets controller timing and redraws.
  Original native screens retain their presentation, then return to first person.
- **Movement:** analog and arrow movement share the same native one-hex request,
  passing current AP plus bonus movement in combat. They reject input while
  native UI owns the view, the interface is disabled, or it is an enemy turn.
  Native animation/pathing still charges the actual movement cost.

Validation: changed translation units pass syntax compilation with SDL2 headers
(existing legacy integer/pointer-cast warnings remain in actions.cc). A compiled
engine fixture exercises the actual renderer input/modal functions: free-roam
movement, combat AP/bonus allowance, zero-AP rejection, enemy-turn/disabled-UI
blocking, nested modal return, stale pick clearing, and native skill/item mode
precedence. Wall, doorway, material, projection/coalescing and offscreen OpenGL
color/depth/transparency/picking regressions pass. No proprietary game-data
integration run or complete CMake build is available here; Deck verification is
still required, especially script-driven dialogue, ladders and map exits.

Test cycle: attack a rat -> N to an aimed mode -> select a body part -> R reload
-> Space/end turn -> finish combat -> M to INTERACT -> loot the corpse -> use a
fixed container/door -> try a locked target with Skills/Lockpick -> save/load ->
use an exit or ladder. Also hold/release the stick around inventory and nested
Options/Load; the world must not move behind the UI or retain a stale target.
For Deck, bind F8 to a spare Steam Input button; its menu can then be operated
with the trackpad/click or arrow/Enter bindings. Existing bindings are unchanged.

## Native Deck/gamepad layout

The game now polls one SDL2 game controller from the central input pump, so
buttons continue to work in synchronous native inventory/dialogue/menu loops.
The thumbsticks and buttons share that controller. A controller exposing paddles
is preferred if both raw and virtual devices are available. Gameplay commands
are queued into Fallout's normal input loop rather than executing game actions
from an SDL callback. `FALLOUT_FP_GAMEPAD=0` disables the new button/pointer
mapping for legacy Steam keyboard/mouse layouts (analog first-person sticks
remain available).

| Input | First-person world | Action menu / native UI |
| --- | --- | --- |
| A | Interact at reticle without changing selected mode | Confirm action menu; mouse click/hold in original screens |
| B | Options | Back/cancel |
| X / Y | Inventory / Pip-Boy | Original screens own their input |
| L1 / R1 | Switch hands / cycle native weapon attack type | No gameplay shortcut behind UI |
| LT / RT | Cycle mode / execute reticle action once per press | RT click/hold in native pointer UI |
| Left / right stick | Established native movement / camera | Right stick moves the original UI pointer |
| D-pad | Unassigned in the world | Arrows with 350 ms initial, 120 ms held repeat |
| View/Select | Action menu (enable FP first from top-down) | Close/cancel |
| Menu/Start | Options | Enter confirmation |
| Right-stick click (R3) | Toggle first person | No toggle behind modal UI |
| L4 / R4 / L5 / R5, if exposed | Action menu / reload / end turn / toggle view | L4 closes action menu; other shortcuts suppressed |

SDL2's Steam Deck mapping uses Paddle1=R4, Paddle2=L4, Paddle3=R5, Paddle4=L5
(verified against SDL2's Deck HID driver). A Steam virtual controller may lack
paddles entirely; the game tests capabilities and shows the appropriate hints.
View/Select and R3 provide access to the same actions without custom rear-button
mapping, with reload/end turn available through the action menu. The game does
not modify the user's Steam configuration, claim a Steam AppID, or ship the
Steamworks SDK. It cannot force Steam to expose physical grips hidden by its
virtual-controller layout. Trackpads remain Steam/OS mouse inputs; the native
right-stick pointer plus A/RT also supports original UI without a mouse trackpad.

One-time setup for this native mode: apply Steam's standard **Gamepad** template
to the existing Fallout First Person shortcut, replacing the earlier keyboard
bindings. Keep ordinary gamepad buttons/triggers/sticks/D-pad. Old keyboard
bindings must be removed to avoid contradictory actions; application code cannot
identify whether a keyboard event came from Steam Input or a real keyboard.
Launch the same shortcut after each rebuild. To use trackpad mouse, retain that
input source only, or use a gamepad template that supplies it.

Input transitions suppress every already-held button/trigger until released.
Opening inventory while A/RT is held cannot click/drag an item accidentally;
confirming a menu cannot immediately interact with the world underneath. Focus
loss releases the emulated mouse, and regain/reconnection requires release
before any held button becomes active. World triggers have 18000/12000 activation
and release thresholds; holding RT does not repeatedly attack or move. Native
UI A/RT stays held for inventory dragging, combined with physical mouse buttons
through the normal mouse device path. Native gameplay/AP/script rules still own
the resulting actions.

Validation: binding tests cover the agreed standard/rear-button layout, trigger
hysteresis, held press suppression, D-pad repeat, modal transitions and reset.
An actual SDL virtual controller fixture tests central command delivery, menu
confirmation, merged native click/hold/release, right-stick pointer motion,
focus loss/regain and disconnect. The existing production gameplay fixture
passes AP/turn/modal/pick/skill checks. Modified translation units compile with
SDL2 headers. Deck/Steam Input integration and proprietary-data UI flows still
require the next device test.
