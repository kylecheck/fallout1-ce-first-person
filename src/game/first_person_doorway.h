#ifndef FALLOUT_GAME_FIRST_PERSON_DOORWAY_H_
#define FALLOUT_GAME_FIRST_PERSON_DOORWAY_H_

#include <cstring>
#include "game/first_person_wall.h"

namespace fallout {

// Asset pairs verified from the VAULTBUR topology and original FRM snapshot.
// This is an explicit visual profile, not a new door object or collision rule.
inline int first_person_doorway_gap(const char* a, int tileA, unsigned int flagsA,
    const char* b, int tileB, unsigned int flagsB)
{
    if (tileA < 0 || tileB < 0 || tileA >= 40000 || tileB >= 40000) {
        return -1;
    }
    const auto kindA = first_person_wall_kind(flagsA);
    const auto kindB = first_person_wall_kind(flagsB);
    if (std::strcmp(a, "dv1036.frm") == 0 && std::strcmp(b, "dv1035.frm") == 0
        && kindA == FIRST_PERSON_WALL_EAST_WEST && kindB == kindA
        && tileB == tileA + 2 && tileA / 200 == tileB / 200) {
        return tileA + 1;
    }
    if (std::strcmp(a, "dv1043.frm") == 0 && std::strcmp(b, "dv1044.frm") == 0
        && kindA == FIRST_PERSON_WALL_NORTH_SOUTH && kindB == kindA
        && tileB == tileA + 400) {
        return tileA + 200;
    }
    if (std::strcmp(a, "velvdr04.frm") == 0 && std::strcmp(b, "velvdr03.frm") == 0
        && kindA == FIRST_PERSON_WALL_EAST_WEST && kindB == kindA
        && tileB == tileA + 4 && tileA / 200 == tileB / 200) {
        return tileA + 1;
    }
    return -1;
}

// How far a native door animation has progressed: frame 0 is closed and the
// last frame fully open, as obj_is_open and the door animations use them.
inline double first_person_door_openness(int frame, int frameCount)
{
    if (frame <= 0) return 0.0;
    if (frameCount <= 1) return 1.0;
    const double openness = static_cast<double>(frame) / (frameCount - 1);
    return openness < 0.0 ? 0.0 : (openness > 1.0 ? 1.0 : openness);
}

// The visible part of a door panel slid toward its b end by openness: the
// panel's leading edge stays in view, the rest has entered the jamb.
inline FirstPersonWallSegment first_person_slide_door_segment(const FirstPersonWallSegment& segment, double openness)
{
    return { segment.ax + (segment.bx - segment.ax) * openness,
        segment.ay + (segment.by - segment.ay) * openness,
        segment.bx, segment.by,
        segment.u0, segment.u0 + (segment.u1 - segment.u0) * (1.0 - openness) };
}

} // namespace fallout
#endif
