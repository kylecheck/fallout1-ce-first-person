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
