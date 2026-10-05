# First-person wall reconstruction

This milestone replaces camera-filtered axis voting with native hex adjacency.
It does not change floor/world mapping, input, the R5/F11/0 toggle, native
movement, mouse targeting, or the shared depth buffer.

## Structure and materials

- Collect visible wall objects in a radius of 19 hexes on the current elevation,
  including those behind the camera. Hidden objects and `block.frm` do not
  contribute. The outer ring supplies topology for the rendered radius of 18.
- Use `tile_num_in_direction` and a tile occupancy set for six native neighbor
  queries per object. Ignore the native API's self-return at map boundaries.
- Each object owns half of every connection, ending at a shared midpoint.
  Corners retain multiple arms. A one-neighbor endpoint extends half a hex in
  the opposite direction. An isolated piece uses rotation as a provisional
  axis, with a one-unit footprint.
- Build horizontal segments independently of FRM anchors and opaque width.
  The normal material pass still crops the original FRM and estimates height.
- Clip segment endpoints against the near plane only after reconstruction,
  interpolating UVs with those endpoints. Rasterization retains depth testing.

This is an adjacency model, not a definitive interpretation of every Fallout
wall prototype. Touching decorative walls, neighboring parallel chains and
triangular clusters can produce extra connections. Isolated special pieces
may need explicit prototype metadata. Original isometric textures still have
transparent margins/slopes inside their crop; their reconstruction is a
separate unfinished task. Duplicate objects on one tile share connectivity
but retain their individual art. Doors remain in the scenery billboard pass.

## Steam Deck validation

Build normally, then load the same save used for the previous prototype.

1. Toggle with R5 and turn through all six directions beside a room corner.
   Wall footprints should stay fixed when neighbors pass behind the camera.
2. Approach a wall and rotate beside it. A visible portion must survive even
   when its center is behind the near plane; texture coordinates should not
   reset to the full image at the clipped endpoint.
3. Inspect straight chains, corners, junctions, ends, doors and isolated pieces.
4. Confirm floor alignment, native movement, toggle behavior and mouse hex
   highlighting still behave as before. The existing mouse click discrepancy
   reported before this milestone is not addressed here.

For a geometry-only view, launch the built executable from the game-data
working directory with:

```sh
FALLOUT_FP_WALL_DEBUG=1 /path/to/fallout-ce
```

Or prefix the Steam launch options with `FALLOUT_FP_WALL_DEBUG=1 %command%`.
Green = connected piece, yellow = more than two neighbors, red = isolated
rotation fallback. Debug walls have a uniform height and opaque fill, bypass
FRM loading, and use normal segment clipping and depth testing. Remove the
environment variable and restart to restore textures (even `=0` enables it).

## Automated checks

No proprietary game data or SDL is required for the structural regression test:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  -fsanitize=address,undefined tests/first_person_wall_test.cc \
  -o /tmp/fallout-wall-test
/tmp/fallout-wall-test
```

The fixture mirrors tile.cc's native 200-column neighbor table and boundary
behavior. It exercises both column parities, all six directions, midpoint
joins, endpoints, corners, junctions, isolated/map-edge tiles, and near-plane
UV clipping. It is not a real-map or gameplay test.

Validation here: renderer translation unit compiled with warnings as errors;
regression test passed with ASan/UBSan (leak detection disabled because the
execution environment blocks LeakSanitizer's process inspection). Full game
linking and visual validation remain unverified: CMake/SDL are unavailable in
this environment and the package manager cannot install them.

## Export actual map topology

The solid-color Deck recording confirmed that gaps persist without FRM texture
transparency. Yellow sections also show multiple adjacency links. Neither fact
alone establishes which missing faces should be filled. Inspect the source map
objects before changing connection rules or inventing wall planes.

Set `FALLOUT_FP_MAP_DUMP` to an output text-file path to export once per process,
on the first first-person frame after loading a map/save. This is independent of
`FALLOUT_FP_WALL_DEBUG`; neither option changes saved game data.

```sh
cd "/home/deck/.local/share/Steam/steamapps/common/Fallout/" && \
FALLOUT_FP_MAP_DUMP="$HOME/Desktop/fallout-wall-map.txt" \
  ~/fallout1-ce-first-person/build/fallout-ce
```

Load the test save, then toggle R5. The terminal reports success or failure.
Attach `fallout-wall-map.txt` from the Desktop. No further video is needed for
this step. Restart the process to take a different snapshot.

The tab-separated diagnostic contains the map name, elevation, player tile and
rotation, every wall/scenery object on that elevation (including hidden objects
and invisible blocker art), prototype flags, scenery subtype, art name, current
frame dimensions/offsets and six native neighboring tiles. It contains metadata,
not game art pixels or the save itself. The explicitly named output file is
replaced on the next run with the option enabled. Parent directories must exist.

The purpose is to distinguish omitted/invisible geometry, door scenery, special
wall prototypes and false connections between nearby wall chains. Merely closing
all visible gaps would risk putting walls across real doors and passages.
