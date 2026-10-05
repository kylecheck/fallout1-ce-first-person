#include "game/first_person.h"
#include "game/object_types.h"
#include <cassert>
#include <iostream>
namespace fallout { Object* obj_dude = nullptr; }
int main() {
    using namespace fallout;
    Object dude {};
    obj_dude = &dude;
    dude.rotation = 2;
    first_person_toggle();
    assert(first_person_rotation() == 2);
    for (int facing = 0; facing < 6; ++facing) {
        dude.rotation = facing;
        assert(first_person_rotation() == 2);
    }
    first_person_turn(-3);
    assert(first_person_rotation() == 5);
    assert(dude.rotation == 5);
    first_person_turn(1);
    assert(first_person_rotation() == 0);
    first_person_turn(13);
    assert(first_person_rotation() == 1);
    first_person_toggle();
    first_person_turn(1);
    assert(first_person_rotation() == 1);
    dude.rotation = 4;
    first_person_toggle();
    assert(first_person_rotation() == 4);
    std::cout << "Camera heading independence, turn wrap and mode reentry passed\n";
}
