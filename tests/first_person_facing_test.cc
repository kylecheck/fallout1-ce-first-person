#include "game/first_person_facing.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace fallout;

int main()
{
    constexpr double kPi = 3.14159265358979323846;
    // Both handednesses: the choice must not depend on the world's mirroring.
    for (const double sense : { 1.0, -1.0 }) {
        for (const double origin : { -kPi / 6.0, 1.0, 3.0 }) {
            double a[6];
            for (int r = 0; r < 6; r++) a[r] = origin + sense * r * kPi / 3.0;
            const double isoView = origin + sense * 5.5 * kPi / 3.0;
            for (int rotation = 0; rotation < 6; rotation++) {
                // Viewed from the isometric camera's angle: the native sprite.
                assert(first_person_billboard_direction(a, isoView, rotation) == rotation);
                assert(first_person_billboard_direction(a, isoView + 2 * kPi, rotation + 6) == rotation);
                // From behind the isometric view: the opposite sprite.
                assert(first_person_billboard_direction(a, isoView + kPi, rotation) == (rotation + 3) % 6);
                // Walking 60 degrees around the critter turns the sprite one step back.
                assert(first_person_billboard_direction(a, isoView + sense * kPi / 3.0, rotation)
                    == (rotation + 5) % 6);
                assert(first_person_billboard_direction(a, isoView - sense * kPi / 3.0, rotation)
                    == (rotation + 1) % 6);
            }
            // A critter facing straight at the viewer shows a sprite facing the
            // isometric camera (SE or SW), wherever the viewer stands.
            for (int step = 0; step < 24; step++) {
                const double view = step * kPi / 12.0;
                double best = 10.0;
                int rotation = 0;
                for (int r = 0; r < 6; r++) {
                    const double d = std::fabs(first_person_angle_difference(a[r], view + kPi));
                    if (d < best) { best = d; rotation = r; }
                }
                if (best > kPi / 6.0 - 1e-6) continue; // Exactly between two directions.
                const int shown = first_person_billboard_direction(a, view, rotation);
                assert(shown == 2 || shown == 3);
            }
        }
    }
    std::cout << "PASS first-person billboard facing\n";
}
