#include "game/first_person.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/map.h"
#include "game/object_types.h"
#include "game/tile.h"
#include "game/object.h"
#include "plib/color/color.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"

namespace fallout {

static bool gFirstPersonEnabled = false;

struct FirstPersonWall {
    double x0;
    double z0;
    double x1;
    double z1;
    double depth;
};

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
    const int wallColor = colorTable[21140];
    const int wallEdgeColor = colorTable[31744];
    const int gridColor = colorTable[992];
    const int crosshairColor = colorTable[31744];

    const int horizon = height * 43 / 100;
    buf_fill(buffer, width, horizon, width, sky);
    buf_fill(buffer + horizon * width, width, height - horizon, width, ground);

    constexpr int kHexGridWidth = 200;
    constexpr double kSqrt3Over2 = 0.8660254037844386;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kNearPlane = 0.45;
    constexpr double kFarPlane = 36.0;

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

    // Keep the depth guides for one more build so the solid-wall projection is
    // easy to judge against the previous wireframe prototype.
    for (int depth = 1; depth <= 8; depth++) {
        const int y = horizon + static_cast<int>((height - horizon) * (1.0 - 1.0 / (1.0 + depth * 0.55)));
        draw_line(buffer, width, 0, y, width - 1, y, gridColor);
    }

    std::vector<int> wallTiles;
    for (Object* wall = obj_find_first_at(map_elevation);
         wall != nullptr;
         wall = obj_find_next_at()) {
        if (wall != obj_dude
            && wall->tile >= 0
            && FID_TYPE(wall->fid) == OBJ_TYPE_WALL
            && tile_dist(obj_dude->tile, wall->tile) <= 18) {
            wallTiles.push_back(wall->tile);
        }
    }

    std::vector<FirstPersonWall> walls;

    for (int wallTile : wallTiles) {
        double wallX;
        double wallY;
        tileToWorld(wallTile, &wallX, &wallY);

        for (int direction = 0; direction < 3; direction++) {
            const int neighborTile = tile_num_in_direction(wallTile, direction, 1);
            if (neighborTile == wallTile
                || std::find(wallTiles.begin(), wallTiles.end(), neighborTile) == wallTiles.end()) {
                continue;
            }

            double neighborX;
            double neighborY;
            tileToWorld(neighborTile, &neighborX, &neighborY);

            const double dx0 = wallX - playerWorldX;
            const double dy0 = wallY - playerWorldY;
            const double dx1 = neighborX - playerWorldX;
            const double dy1 = neighborY - playerWorldY;

            double cameraX0 = dx0 * rightX + dy0 * rightY;
            double cameraZ0 = dx0 * forwardX + dy0 * forwardY;
            double cameraX1 = dx1 * rightX + dy1 * rightY;
            double cameraZ1 = dx1 * forwardX + dy1 * forwardY;

            if ((cameraZ0 < kNearPlane && cameraZ1 < kNearPlane)
                || (cameraZ0 > kFarPlane && cameraZ1 > kFarPlane)) {
                continue;
            }

            // Clip a segment crossing the camera instead of dropping the whole
            // wall or allowing a near endpoint to explode across the screen.
            if (cameraZ0 < kNearPlane) {
                const double t = (kNearPlane - cameraZ0) / (cameraZ1 - cameraZ0);
                cameraX0 += (cameraX1 - cameraX0) * t;
                cameraZ0 = kNearPlane;
            }
            if (cameraZ1 < kNearPlane) {
                const double t = (kNearPlane - cameraZ1) / (cameraZ0 - cameraZ1);
                cameraX1 += (cameraX0 - cameraX1) * t;
                cameraZ1 = kNearPlane;
            }

            if (cameraZ0 > kFarPlane || cameraZ1 > kFarPlane) {
                continue;
            }

            walls.push_back({ cameraX0, cameraZ0, cameraX1, cameraZ1, (cameraZ0 + cameraZ1) * 0.5 });
        }
    }

    // Painter's algorithm is sufficient for this prototype: draw distant wall
    // planes first so nearer planes naturally cover them.
    std::sort(walls.begin(), walls.end(), [](const FirstPersonWall& a, const FirstPersonWall& b) {
        return a.depth > b.depth;
    });

    for (const FirstPersonWall& wall : walls) {
        const int screenX0 = width / 2 + static_cast<int>(wall.x0 * focal / wall.z0);
        const int screenX1 = width / 2 + static_cast<int>(wall.x1 * focal / wall.z1);
        const int bottom0 = horizon + std::clamp(static_cast<int>(focal * 0.50 / wall.z0), 0, height - horizon - 1);
        const int bottom1 = horizon + std::clamp(static_cast<int>(focal * 0.50 / wall.z1), 0, height - horizon - 1);
        const int top0 = bottom0 - std::clamp(static_cast<int>(focal * 1.25 / wall.z0), 6, height);
        const int top1 = bottom1 - std::clamp(static_cast<int>(focal * 1.25 / wall.z1), 6, height);

        const int minX = std::max(0, std::min(screenX0, screenX1));
        const int maxX = std::min(width - 1, std::max(screenX0, screenX1));
        if (minX > maxX) {
            continue;
        }

        // Fill the projected quadrilateral one screen column at a time. This
        // stays entirely inside Fallout's existing 8-bit software framebuffer.
        const double denominator = static_cast<double>(screenX1 - screenX0);
        for (int x = minX; x <= maxX; x++) {
            double t = denominator == 0.0 ? 0.0 : (x - screenX0) / denominator;
            t = std::clamp(t, 0.0, 1.0);
            int top = static_cast<int>(top0 + (top1 - top0) * t);
            int bottom = static_cast<int>(bottom0 + (bottom1 - bottom0) * t);
            if (top > bottom) {
                std::swap(top, bottom);
            }
            top = std::clamp(top, 0, height - 1);
            bottom = std::clamp(bottom, 0, height - 1);
            draw_line(buffer, width, x, top, x, bottom, wallColor);
        }

        // Retain bright edges for debugging the inferred Fallout wall topology.
        draw_line(buffer, width,
            std::clamp(screenX0, 0, width - 1), std::clamp(bottom0, 0, height - 1),
            std::clamp(screenX1, 0, width - 1), std::clamp(bottom1, 0, height - 1), wallEdgeColor);
        draw_line(buffer, width,
            std::clamp(screenX0, 0, width - 1), std::clamp(top0, 0, height - 1),
            std::clamp(screenX1, 0, width - 1), std::clamp(top1, 0, height - 1), wallEdgeColor);
    }

    draw_line(buffer, width, width / 2 - 7, height / 2, width / 2 + 7, height / 2, crosshairColor);
    draw_line(buffer, width, width / 2, height / 2 - 7, width / 2, height / 2 + 7, crosshairColor);
}

} // namespace fallout
