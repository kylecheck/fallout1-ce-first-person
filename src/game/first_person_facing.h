#ifndef FALLOUT_GAME_FIRST_PERSON_FACING_H_
#define FALLOUT_GAME_FIRST_PERSON_FACING_H_
#include <cmath>
namespace fallout {
// Critter FRMs hold six directions as seen by the fixed isometric camera,
// which looks toward screen-up, midway between hex directions 0 (NE) and 5
// (NW). A first-person viewer sees a critter from a different angle, so the
// sprite shown is the direction that has the same angle to the isometric view
// as the critter's real facing has to the line from the viewer to it.
//
// Angles are in the renderer's world plane. directionAngles[r] is the world
// angle of a step in native direction r (measured from tile neighbours, so no
// handedness is assumed); viewAngle is the angle from the viewer to the critter.
inline double first_person_angle_difference(double a, double b)
{
    constexpr double kTwoPi = 6.283185307179586;
    double d = std::fmod(a - b, kTwoPi);
    if (d > kTwoPi / 2) d -= kTwoPi;
    if (d < -kTwoPi / 2) d += kTwoPi;
    return d;
}

inline int first_person_billboard_direction(const double directionAngles[6], double viewAngle, int rotation)
{
    rotation = ((rotation % 6) + 6) % 6;
    const double isoView = directionAngles[0]
        + first_person_angle_difference(directionAngles[5], directionAngles[0]) / 2;
    const double shown = directionAngles[rotation] + first_person_angle_difference(isoView, viewAngle);
    int best = rotation;
    double bestDistance = 10.0;
    for (int r = 0; r < 6; r++) {
        const double distance = std::fabs(first_person_angle_difference(directionAngles[r], shown));
        // Prefer the native direction on an exact tie, so a viewer at the
        // isometric angle always sees the native sprite.
        if (distance < bestDistance - 1e-9 || (std::fabs(distance - bestDistance) <= 1e-9 && r == rotation)) {
            best = r;
            bestDistance = distance;
        }
    }
    return best;
}
}
#endif
