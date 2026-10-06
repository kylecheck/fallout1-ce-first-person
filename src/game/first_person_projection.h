#ifndef FALLOUT_GAME_FIRST_PERSON_PROJECTION_H_
#define FALLOUT_GAME_FIRST_PERSON_PROJECTION_H_
#include <algorithm>
#include <cmath>
#include "plib/gnw/grbuf.h"
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
// draw_line has no framebuffer clipping. The off-axis horizon can put these
// guides above the viewport during downward look, including in GPU mode.
inline void first_person_draw_depth_guides(unsigned char* buffer, int width,
    int height, int horizon, int color)
{
    if (width <= 0 || height <= 0) return;
    for (int depth = 1; depth <= 8; depth++) {
        const int y = horizon + static_cast<int>((height - horizon)
            * (1.0 - 1.0 / (1.0 + depth * 0.55)));
        if (y < 0 || y >= height) continue;
        draw_line(buffer, width, 0, y, width - 1, y, color);
    }
}
}
#endif
