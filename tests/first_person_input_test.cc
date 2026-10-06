// Minimal engine fixture. Include the renderer so the pick snapshot can be
// exercised without SDL or proprietary map/art data. Link with section GC to
// omit the uncalled rendering pass and its game-engine dependencies.
#include "game/first_person.cc"
#include <cassert>
#include <iostream>

namespace fallout {
Object player {};
Object target {};
Object* obj_dude = &player;
int display_win = 1;
int map_elevation = 0;
static Rect testRect { 0, 0, 639, 379 };
static int sampledX, sampledY;
static bool liveTarget = true;
int win_width(int) { return 640; }
int win_height(int) { return 380; }
int win_get_rect(int, Rect* rect) { *rect = testRect; return 0; }
int tile_coord(int, int* x, int* y, int) { *x = 100; *y = 200; return 0; }
int tile_num(int x, int y, int elevation, bool ignoreBounds)
{
    assert(elevation == 0 && !ignoreBounds);
    sampledX = x; sampledY = y;
    return 20100;
}
Object* obj_find_first_at(int) { return liveTarget ? &target : nullptr; }
Object* obj_find_next_at() { return nullptr; }
// Toggle creates and shows the full-screen view window and hides the interface.
unsigned char colorTable[32768];
int screenGetWidth() { return 640; }
int screenGetHeight() { return 380; }
int win_add(int, int, int, int, int, int) { return display_win; }
void win_show(int) {}
void win_hide(int) {}
int intface_is_hidden() { return 1; }
void intface_hide() {}
void intface_show() {}
void first_person_heap_check(const char*) {}
}

int main()
{
    using namespace fallout;
    player.tile = 20100;
    assert(first_person_target_tile(320, 329) == -1);
    first_person_toggle();
    // 640x380 at level pitch: horizon 163, focal 448, eye 0.74. Row 329 puts
    // the center ray at z~2. Heading 0 is yaw -30 degrees, where the
    // established isometric basis gives (+16z,-12z), so (+32,-24).
    assert(first_person_horizon(640, 380) == 163);
    assert(first_person_target_tile(320, 329) == 20100);
    assert(sampledX == 132 && sampledY == 176);
    // The camera heading is independent of native character facing.
    player.rotation = 3;
    assert(first_person_target_tile(320, 329) == 20100);
    assert(sampledX == 132 && sampledY == 176);
    player.rotation = 0;
    // Turning half a circle reverses the displacement; no raw screen use.
    first_person_turn(12);
    assert(first_person_target_tile(320, 329) == 20100);
    assert(sampledX == 68 && sampledY == 224);
    first_person_turn(-12);
    testRect.ulx = 10; testRect.uly = 20;
    assert(first_person_target_tile(330, 349) == 20100);
    assert(sampledX == 132 && sampledY == 176);
    assert(first_person_target_tile(330, 183) == -1); // Horizon.
    assert(first_person_target_tile(330, 184) == -1); // Beyond far plane.
    assert(first_person_target_tile(9, 349) == -1);
    assert(first_person_target_tile(650, 349) == -1);
    assert(first_person_target_tile(330, 400) == -1); // Outside viewport.

    gPickWidth = 640; gPickHeight = 380;
    gPickTile = player.tile; gPickRotation = gFirstPersonCameraRevision; gPickElevation = 0;
    gFirstPersonPicks.assign(640 * 380, {nullptr, -1});
    gFirstPersonInteractionPicks.assign(640 * 380, {nullptr, -1});
    target.id = 123; target.fid = OBJ_TYPE_ITEM << 24;
    gFirstPersonPicks[275 * 640 + 320] = { &target, 123 };
    assert(first_person_object_at(330, 295, -1, true, 0) == &target);
    // Small sparse sprites snap within the assist radius, never beyond it.
    assert(first_person_object_at(334, 299, -1, true, 0) == &target);
    assert(first_person_object_at(337, 295, -1, true, 0) == nullptr);
    assert(first_person_object_at(330, 295, OBJ_TYPE_CRITTER, true, 0) == nullptr);
    assert(first_person_object_at(330, 295, -1, true, 1) == nullptr);
    target.flags = OBJECT_HIDDEN;
    assert(first_person_object_at(330, 295, -1, true, 0) == nullptr);
    target.flags = 0;
    target.id = 124; // Reused pointer must not select a different object.
    assert(first_person_object_at(330, 295, -1, true, 0) == nullptr);
    target.id = 123;
    liveTarget = false; // Removed objects are never dereferenced through cache.
    assert(first_person_object_at(330, 295, -1, true, 0) == nullptr);
    liveTarget = true;
    // The opaque-bounds footprint selects through transparent sprite holes.
    gFirstPersonPicks[275 * 640 + 320] = { nullptr, -1 };
    gFirstPersonInteractionPicks[275 * 640 + 320] = { &target, 123 };
    assert(first_person_object_at(330, 295, -1, true, 0) == &target);
    // Any camera change invalidates the snapshot until the next render.
    first_person_turn(1);
    assert(first_person_object_at(330, 295, -1, true, 0) == nullptr);
    first_person_toggle();
    assert(gFirstPersonPicks.empty());
    assert(first_person_target_tile(330, 349) == -1);
    std::cout << "First-person input projection and object-picking tests passed\n";
}
