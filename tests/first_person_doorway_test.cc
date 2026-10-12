#include "game/first_person_doorway.h"
#include <cassert>
#include <cmath>
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
    // Door animation progress and the sliding panel.
    assert(first_person_door_openness(0, 6) == 0.0);
    assert(first_person_door_openness(5, 6) == 1.0);
    assert(std::fabs(first_person_door_openness(2, 5) - 0.5) < 1e-9);
    assert(first_person_door_openness(9, 6) == 1.0 && first_person_door_openness(-1, 6) == 0.0);
    assert(first_person_door_openness(1, 1) == 1.0 && first_person_door_openness(0, 1) == 0.0);
    const FirstPersonWallSegment panel { 0.0, 0.0, 2.0, 1.0, 0.0, 1.0 };
    const FirstPersonWallSegment closed = first_person_slide_door_segment(panel, 0.0);
    assert(closed.ax == 0.0 && closed.ay == 0.0 && closed.bx == 2.0 && closed.u1 == 1.0);
    const FirstPersonWallSegment half = first_person_slide_door_segment(panel, 0.5);
    assert(half.ax == 1.0 && half.ay == 0.5 && half.bx == 2.0 && half.by == 1.0);
    assert(half.u0 == 0.0 && half.u1 == 0.5); // The leading half stays in view.
    const FirstPersonWallSegment open = first_person_slide_door_segment(panel, 1.0);
    assert(open.ax == open.bx && open.ay == open.by && open.u1 == open.u0);
    const FirstPersonWallSegment reversed = first_person_slide_door_segment({ 0.0, 0.0, 1.0, 0.0, 1.0, 0.0 }, 0.25);
    assert(reversed.ax == 0.25 && std::fabs(reversed.u1 - 0.25) < 1e-9);
    std::cout << "Doorway profile and sliding door regression tests passed\n";
}
