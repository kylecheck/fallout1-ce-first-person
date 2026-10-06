#ifndef FALLOUT_GAME_FIRST_PERSON_PROJECTION_H_
#define FALLOUT_GAME_FIRST_PERSON_PROJECTION_H_
#include <algorithm>
#include <cmath>
namespace fallout {
// Vertical look is an off-axis projection. Its horizon can lie above the
// viewport when looking at nearby ground; framebuffer fill bounds cannot.
inline int first_person_projected_horizon(int width, int height, double pitchDegrees)
{
    constexpr double pi = 3.14159265358979323846;
    return height * 43 / 100 + static_cast<int>(std::lround(
        width * 0.70 * std::tan(pitchDegrees * pi / 180.0)));
}
inline int first_person_background_split(int height, int horizon)
{
    return std::clamp(horizon, 0, height);
}
}
#endif
