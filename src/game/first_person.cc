#include "game/first_person.h"

#include <algorithm>
#include <cmath>

#include "game/map.h"
#include "game/object_types.h"
#include "game/tile.h"
#include "game/object.h"
#include "plib/color/color.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"

namespace fallout {

// Prototype test: start enabled so Steam Deck testing does not depend on keyboard mappings.
static bool gFirstPersonEnabled = false;

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

    const int sky = colorTable[0];
    const int ground = colorTable[10570];
    const int wallColor = colorTable[31744];
    const int gridColor = colorTable[992];
    const int crosshairColor = colorTable[31744];

    const int horizon = height * 43 / 100;
    buf_fill(buffer, width, horizon, width, sky);
    buf_fill(buffer + horizon * width, width, height - horizon, width, ground);

    constexpr int kHexGridWidth = 200;
    constexpr double kSqrt3Over2 = 0.8660254037844386;
    constexpr double kPi = 3.14159265358979323846;

    auto tileToWorld = [](int tile, double* x, double* y) {
        const int column = tile % kHexGridWidth;
        const int row = tile / kHexGridWidth;
        *x = -column * kSqrt3Over2;
        *y = row - (column & 1) * 0.5;
    };

    double playerWorldX;
    double playerWorldY;
    tileToWorld(obj_dude->tile, &playerWorldX, &playerWorldY);

    const int rotation = ((obj_dude->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
    const double yaw = -kPi / 6.0 + rotation * (kPi / 3.0);
    const double forwardX = std::cos(yaw);
    const double forwardY = std::sin(yaw);
    const double rightX = -forwardY;
    const double rightY = forwardX;
    const double focal = width * 0.70;

    // Keep simple depth guides while the geometry conversion is being
    // validated. They will disappear once textured floors are in place.
    for (int depth = 1; depth <= 8; depth++) {
        const int y = horizon + static_cast<int>((height - horizon) * (1.0 - 1.0 / (1.0 + depth * 0.55)));
        draw_line(buffer, width, 0, y, width - 1, y, gridColor);
    }

    // v0.004: build wall connectivity from the real hex map. Each nearby wall
    // hex is connected to adjacent wall hexes, producing perspective line
    // segments that follow the room/corridor structure instead of drawing one
    // camera-facing rectangle per object.
    for (Object* wall = obj_find_first_at(map_elevation);
         wall != nullptr;
         wall = obj_find_next_at()) {
        if (wall == obj_dude || wall->tile < 0 || FID_TYPE(wall->fid) != OBJ_TYPE_WALL) {
            continue;
        }

        if (tile_dist(obj_dude->tile, wall->tile) > 18) {
            continue;
        }

        double wallX;
        double wallY;
        tileToWorld(wall->tile, &wallX, &wallY);

        // Only inspect half the six directions so each neighboring pair is
        // emitted once.
        for (int direction = 0; direction < 3; direction++) {
            const int neighborTile = tile_num_in_direction(wall->tile, direction, 1);
            if (neighborTile == wall->tile) {
                continue;
            }

            bool hasWallNeighbor = false;
            for (Object* candidate = obj_find_first_at(map_elevation);
                 candidate != nullptr;
                 candidate = obj_find_next_at()) {
                if (candidate->tile == neighborTile && FID_TYPE(candidate->fid) == OBJ_TYPE_WALL) {
                    hasWallNeighbor = true;
                    break;
                }
            }

            if (!hasWallNeighbor) {
                continue;
            }

            double neighborX;
            double neighborY;
            tileToWorld(neighborTile, &neighborX, &neighborY);

            const double dx0 = wallX - playerWorldX;
            const double dy0 = wallY - playerWorldY;
            const double dx1 = neighborX - playerWorldX;
            const double dy1 = neighborY - playerWorldY;

            const double cameraX0 = dx0 * rightX + dy0 * rightY;
            const double cameraZ0 = dx0 * forwardX + dy0 * forwardY;
            const double cameraX1 = dx1 * rightX + dy1 * rightY;
            const double cameraZ1 = dx1 * forwardX + dy1 * forwardY;

            if (cameraZ0 <= 0.35 || cameraZ1 <= 0.35 || cameraZ0 > 36.0 || cameraZ1 > 36.0) {
                continue;
            }

            const int screenX0 = width / 2 + static_cast<int>(cameraX0 * focal / cameraZ0);
            const int screenX1 = width / 2 + static_cast<int>(cameraX1 * focal / cameraZ1);
            const int bottom0 = horizon + std::clamp(static_cast<int>(focal * 0.50 / cameraZ0), 0, height - horizon - 1);
            const int bottom1 = horizon + std::clamp(static_cast<int>(focal * 0.50 / cameraZ1), 0, height - horizon - 1);
            const int top0 = bottom0 - std::clamp(static_cast<int>(focal * 1.25 / cameraZ0), 6, height);
            const int top1 = bottom1 - std::clamp(static_cast<int>(focal * 1.25 / cameraZ1), 6, height);

            if ((screenX0 < 0 && screenX1 < 0)
                || (screenX0 >= width && screenX1 >= width)
                || (bottom0 < 0 && bottom1 < 0)
                || (top0 >= height && top1 >= height)) {
                continue;
            }

            drawQuad(buffer, width,
                std::clamp(screenX0, 0, width - 1), std::clamp(bottom0, 0, height - 1),
                std::clamp(screenX1, 0, width - 1), std::clamp(bottom1, 0, height - 1),
                std::clamp(screenX1, 0, width - 1), std::clamp(top1, 0, height - 1),
                std::clamp(screenX0, 0, width - 1), std::clamp(top0, 0, height - 1),
                wallColor);
        }
    }

    draw_line(buffer, width, width / 2 - 7, height / 2, width / 2 + 7, height / 2, crosshairColor);
    draw_line(buffer, width, width / 2, height / 2 - 7, width / 2, height / 2 + 7, crosshairColor);
}


} // namespace fallout
