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
}

int main()
{
    using namespace fallout;
    player.tile = 20100;
    assert(first_person_target_tile(320, 275) == -1);
    first_person_toggle();
    // Center ray at z=2, rotation 0. Established isometric basis gives (+32,-24).
    assert(first_person_target_tile(320, 275) == 20100);
    assert(sampledX == 132 && sampledY == 176);
    // Opposite facing reverses that displacement; no raw screen-coordinate use.
    player.rotation = 3;
    assert(first_person_target_tile(320, 275) == 20100);
    assert(sampledX == 68 && sampledY == 224);
    player.rotation = 0;
    testRect.ulx = 10; testRect.uly = 20;
    assert(first_person_target_tile(330, 295) == 20100);
    assert(sampledX == 132 && sampledY == 176);
    assert(first_person_target_tile(330, 183) == -1); // Horizon.
    assert(first_person_target_tile(330, 184) == -1); // Beyond far plane.
    assert(first_person_target_tile(9, 295) == -1);
    assert(first_person_target_tile(650, 295) == -1);
    assert(first_person_target_tile(330, 400) == -1); // Interface/outside viewport.

    gPickWidth = 640; gPickHeight = 380;
    gPickTile = player.tile; gPickRotation = player.rotation; gPickElevation = 0;
    gFirstPersonPicks.assign(640 * 380, {nullptr, -1});
    target.id = 123; target.fid = OBJ_TYPE_ITEM << 24;
    gFirstPersonPicks[275 * 640 + 320] = { &target, 123 };
    assert(first_person_object_at(330, 295, -1, true, 0) == &target);
    assert(first_person_object_at(331, 295, -1, true, 0) == nullptr);
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
    player.rotation = 1;
    assert(first_person_object_at(330, 295, -1, true, 0) == nullptr);
    player.rotation = 0;
    first_person_toggle();
    assert(gFirstPersonPicks.empty());
    assert(first_person_target_tile(330, 295) == -1);
    std::cout << "First-person input projection and object-picking tests passed\n";
}
