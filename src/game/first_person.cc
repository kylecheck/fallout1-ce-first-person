#include "game/first_person.h"

#include <algorithm>
#include <cmath>

#include "game/map.h"
#include "game/object.h"
#include "plib/color/color.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"

namespace fallout {

// Prototype test: start enabled so Steam Deck testing does not depend on keyboard mappings.
static bool gFirstPersonEnabled = true;

static void drawQuad(unsigned char* buffer, int pitch,
    int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, int color)
{
    draw_line(buffer, pitch, x0, y0, x1, y1, color);
    draw_line(buffer, pitch, x1, y1, x2, y2, color);
    draw_line(buffer, pitch, x2, y2, x3, y3, color);
    draw_line(buffer, pitch, x3, y3, x0, y0, color);
}

bool first_person_is_enabled()
{
    return gFirstPersonEnabled;
}

void first_person_toggle()
{
    gFirstPersonEnabled = !gFirstPersonEnabled;
}

void first_person_render()
{
    if (!gFirstPersonEnabled || obj_dude == nullptr || display_win == -1) {
        return;
    }

    unsigned char* buffer = win_get_buf(display_win);
    if (buffer == nullptr) {
        return;
    }

    const int width = win_width(display_win);
    const int height = win_height(display_win);
    if (width <= 0 || height <= 0) {
        return;
    }

    // Prototype v0.001: prove that CE can substitute a perspective viewport
    // while the original Fallout map/simulation continues running underneath.
    const int sky = colorTable[0];
    const int ground = colorTable[10570];
    const int nearLine = colorTable[31744];
    const int farLine = colorTable[992];

    const int horizon = height * 43 / 100;
    buf_fill(buffer, width, horizon, width, sky);
    buf_fill(buffer + horizon * width, width, height - horizon, width, ground);

    // Fallout has six facing directions. Offset the vanishing point slightly
    // with the player's rotation so turning is immediately visible.
    const int rotation = ((obj_dude->rotation % 6) + 6) % 6;
    const double angle = rotation * (3.14159265358979323846 / 3.0);
    const int vanishX = width / 2 + static_cast<int>(std::sin(angle) * width * 0.08);
    const int vanishY = horizon;

    // Perspective floor grid. This is deliberately geometry-only for the first
    // milestone; later revisions will derive floor/wall surfaces from MAP data.
    const int depthBands = 9;
    int previousY = height - 1;
    for (int depth = 1; depth <= depthBands; depth++) {
        const double t = static_cast<double>(depth) / depthBands;
        const double perspective = t * t;
        const int y = height - 1 - static_cast<int>((height - 1 - horizon) * perspective);
        const int halfWidth = static_cast<int>((width * 0.62) * (1.0 - perspective) + 8.0);
        const int color = depth < 5 ? nearLine : farLine;

        draw_line(buffer, width,
            std::max(0, vanishX - halfWidth), y,
            std::min(width - 1, vanishX + halfWidth), y,
            color);
        previousY = y;
    }

    // Radial lines converge at the vanishing point and make the hex-world
    // orientation obvious without yet depending on Fallout art assets.
    for (int lane = -6; lane <= 6; lane++) {
        const int bottomX = width / 2 + lane * width / 10;
        draw_line(buffer, width,
            std::clamp(bottomX, 0, width - 1), height - 1,
            vanishX, vanishY,
            lane == 0 ? nearLine : farLine);
    }

    // Temporary "wall" blocks ahead of the player. These give us an immediate
    // perspective/camera sanity check before MAP scenery is projected.
    const int wallBottom = horizon + (height - horizon) * 3 / 5;
    const int wallTop = horizon - height / 7;
    const int wallHalf = std::max(24, width / 10);
    drawQuad(buffer, width,
        vanishX - wallHalf, wallBottom,
        vanishX + wallHalf, wallBottom,
        vanishX + wallHalf * 2 / 3, wallTop,
        vanishX - wallHalf * 2 / 3, wallTop,
        nearLine);

    // Crosshair.
    draw_line(buffer, width, width / 2 - 7, height / 2, width / 2 + 7, height / 2, nearLine);
    draw_line(buffer, width, width / 2, height / 2 - 7, width / 2, height / 2 + 7, nearLine);
}

} // namespace fallout
