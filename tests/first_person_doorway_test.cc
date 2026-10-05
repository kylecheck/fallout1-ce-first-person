#include "game/first_person_doorway.h"
#include <cassert>
#include <iostream>
using namespace fallout;
int main()
{
    constexpr unsigned int ew = 0x08000000u;
    assert(first_person_doorway_gap("dv1036.frm", 14677, ew,
        "dv1035.frm", 14679, ew) == 14678);
    assert(first_person_doorway_gap("dv1043.frm", 15092, 0,
        "dv1044.frm", 15492, 0) == 15292);
    assert(first_person_doorway_gap("velvdr04.frm", 14302, ew,
        "velvdr03.frm", 14306, ew) == 14303);
    assert(first_person_doorway_gap("velvdr04.frm", 14302, ew,
        "velvdr03.frm", 14305, ew) == -1);
    // Nearby ordinary art and reversed, misaligned, or row-wrapped pairs
    // must never create overhead geometry across an unproven opening.
    assert(first_person_doorway_gap("dv1000.frm", 14677, ew,
        "dv1035.frm", 14679, ew) == -1);
    assert(first_person_doorway_gap("dv1035.frm", 14677, ew,
        "dv1036.frm", 14679, ew) == -1);
    assert(first_person_doorway_gap("dv1036.frm", 14677, 0,
        "dv1035.frm", 14679, ew) == -1);
    assert(first_person_doorway_gap("dv1036.frm", 14799, ew,
        "dv1035.frm", 14801, ew) == -1);
    assert(first_person_doorway_gap("dv1043.frm", 15092, 0,
        "dv1044.frm", 15493, 0) == -1);
    assert(first_person_doorway_gap("dv1043.frm", 39892, 0,
        "dv1044.frm", 40292, 0) == -1);
    std::cout << "Doorway profile regression tests passed\n";
}
