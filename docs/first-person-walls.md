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


## Native translucent-egg compatibility

Fallout's original object renderer has a special player-visibility mask, the
translucent "egg". Wall/scenery pieces are tested against the player's side
using their prototype `extendedFlags`, plus `OBJECT_WALL_TRANS_END`, before
the egg mask is blended through the sprite.

The first-person renderer now mirrors that native sidedness test only as
material metadata. It does **not** render the top-down egg effect.

When a wall would be egg-sensitive in the native renderer:

- keep its first-person structural geometry fully solid
- determine the face axis from the actual first-person segment
- search both directions on that axis for the nearest compatible straight wall
- search may pass only through proven `block.frm` topology and stops at doors
- borrow the compatible straight wall material when found
- if no clean continuation exists, keep the wall's own already-rectified solid
  material rather than deleting or alpha-cutting the structural face

This removes another class of top-down-only rendering assumptions without
changing collision, movement, wall topology, or semantic openings.
