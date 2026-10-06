// Exercise production input/modal code with a minimal native engine fixture.
// g++ -std=c++17 -ffunction-sections -fdata-sections -I/path/to/SDL/include
//     -Isrc tests/first_person_gameplay_test.cc -Wl,--gc-sections -o /tmp/fp-gameplay-test
#include "game/first_person.cc"
#include <cassert>
#include <iostream>

namespace fallout {
Object player {};
Object* obj_dude = &player;
Rect scr_size {0, 0, 1279, 799};
unsigned int combat_state = COMBAT_STATE_0x02;
int combat_free_move = 0;
static int topWindow = 5;
static bool interfaceEnabled = true;
static int nativeMode = GAME_MOUSE_MODE_MOVE;
static int hides = 0, shows = 0, moves = 0, submittedAp = 0;
static int registeredRotation = 0;
int win_get_top_visible_win(int, int) { return topWindow; }
bool intface_is_enabled() { return interfaceEnabled; }
void intface_hide() {}
void win_hide(int) { hides++; }
void win_show(int) { shows++; }
int gmouse_3d_get_mode() { return nativeMode; }
int tile_num_in_direction(int tile, int rotation, int distance)
{
    registeredRotation = rotation;
    assert(distance == 1);
    return tile + 1;
}
int register_begin(int) { return 0; }
int register_end() { return 0; }
int register_object_move_to_tile(Object* object, int tile, int elevation, int ap, int)
{
    assert(object == obj_dude && tile == obj_dude->tile + 1 && elevation == obj_dude->elevation);
    submittedAp = ap;
    moves++;
    return 0;
}
}

int main()
{
    using namespace fallout;
    gFirstPersonEnabled = true;
    gFirstPersonWindow = 5;
    player.tile = 20100;
    assert(first_person_world_input_allowed());
    assert(first_person_overlay_visible());
    gFirstPersonActionMenuActive = true;
    assert(!first_person_overlay_visible() && !first_person_world_input_allowed());
    gFirstPersonActionMenuActive = false;
    first_person_move(3);
    assert(moves == 1 && submittedAp == -1 && registeredRotation == 3);
    combat_state = COMBAT_STATE_0x01 | COMBAT_STATE_0x02;
    player.data.critter.combat.ap = 3;
    combat_free_move = 2;
    first_person_move(1);
    assert(moves == 2 && submittedAp == 5);
    player.data.critter.combat.ap = combat_free_move = 0;
    first_person_move(1);
    assert(moves == 2);
    player.data.critter.combat.ap = 5;
    combat_state = COMBAT_STATE_0x01; // Enemy turn, even with leftover AP.
    first_person_move(1);
    assert(moves == 2 && !first_person_world_input_allowed());
    assert(first_person_overlay_visible()); // Enemy turn still shows the scene.
    combat_state = COMBAT_STATE_0x02;
    interfaceEnabled = false;
    first_person_move(1);
    assert(moves == 2);
    interfaceEnabled = true;
    topWindow = 9; // Native dialog owns the view.
    assert(!first_person_overlay_visible());
    first_person_move(1);
    assert(moves == 2);
    topWindow = 5;
    gFirstPersonPicks.push_back({&player, 123});
    {
        FirstPersonModalScope options;
        assert(hides == 1 && gFirstPersonOverlaySuspended);
        assert(!first_person_overlay_visible());
        {
            FirstPersonModalScope load;
            assert(hides == 1 && gFirstPersonModalDepth == 2);
            first_person_move(1);
            assert(moves == 2);
        }
        assert(shows == 0 && gFirstPersonOverlaySuspended);
    }
    assert(shows == 1 && !gFirstPersonOverlaySuspended && gFirstPersonModalDepth == 0);
    assert(gFirstPersonPicks.empty());
    first_person_resume_overlay(); // Extra resume never underflows.
    assert(shows == 1 && gFirstPersonModalDepth == 0);
    gFirstPersonMode = GAME_MOUSE_MODE_CROSSHAIR;
    nativeMode = GAME_MOUSE_MODE_MOVE;
    assert(first_person_mode() == GAME_MOUSE_MODE_CROSSHAIR);
    nativeMode = GAME_MOUSE_MODE_USE_LOCKPICK;
    assert(first_person_mode() == GAME_MOUSE_MODE_USE_LOCKPICK);
    nativeMode = GAME_MOUSE_MODE_USE_CROSSHAIR;
    assert(first_person_mode() == GAME_MOUSE_MODE_USE_CROSSHAIR);
    gFirstPersonEnabled = false;
    { FirstPersonModalScope nativeOnly; }
    assert(hides == 1 && shows == 1);
    std::cout << "PASS native AP/turn movement, modal nesting, pick invalidation and skill targeting\n";
}
