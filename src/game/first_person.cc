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

    // v0.002: the camera is now anchored to the player's real Fallout hex.
    // Nearby OBJ_TYPE_WALL objects from the loaded map are projected into a
    // simple perspective view. Art/texturing comes later.
    const int sky = colorTable[0];
    const int ground = colorTable[10570];
    const int wallColor = colorTable[31744];
    const int gridColor = colorTable[992];
    const int crosshairColor = colorTable[31744];

    const int horizon = height * 43 / 100;
    buf_fill(buffer, width, horizon, width, sky);
    buf_fill(buffer + horizon * width, width, height - horizon, width, ground);

    int playerX;
    int playerY;
    if (tile_coord(obj_dude->tile, &playerX, &playerY, map_elevation) != 0) {
        return;
    }

    const int rotation = ((obj_dude->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
    const double yaw = rotation * (3.14159265358979323846 / 3.0);
    // tile_coord is screen/isometric space. This is only an approximate world
    // basis for v0.002, but it preserves real map-relative wall placement.
    const double forwardX = std::sin(yaw);
    const double forwardY = -std::cos(yaw);
    const double rightX = std::cos(yaw);
    const double rightY = std::sin(yaw);
    const double focal = width * 0.70;

    // A few depth guides make it easier to judge whether real wall positions
    // agree with the map while we validate the projection.
    for (int depth = 1; depth <= 8; depth++) {
        const int y = horizon + static_cast<int>((height - horizon) * (1.0 - 1.0 / (1.0 + depth * 0.55)));
        draw_line(buffer, width, 0, y, width - 1, y, gridColor);
    }

    for (Object* object = obj_find_first_at(map_elevation);
         object != nullptr;
         object = obj_find_next_at()) {
        if (object == obj_dude || object->tile < 0 || FID_TYPE(object->fid) != OBJ_TYPE_WALL) {
            continue;
        }

        // Keep the prototype local so distant map objects do not clutter the
        // view or waste time.
        const int hexDistance = tile_dist(obj_dude->tile, object->tile);
        if (hexDistance > 18) {
            continue;
        }

        int wallX;
        int wallY;
        if (tile_coord(object->tile, &wallX, &wallY, map_elevation) != 0) {
            continue;
        }

        // Normalize Fallout's isometric screen deltas into a rough 2D world
        // plane before rotating them into camera space.
        const double dx = (wallX - playerX) / 32.0;
        const double dy = (wallY - playerY) / 12.0;
        const double cameraX = dx * rightX + dy * rightY;
        const double cameraZ = dx * forwardX + dy * forwardY;

        if (cameraZ <= 0.35 || cameraZ > 36.0) {
            continue;
        }

        const int centerX = width / 2 + static_cast<int>(cameraX * focal / cameraZ);
        const int halfWidth = std::clamp(static_cast<int>(focal * 0.42 / cameraZ), 2, width / 3);
        const int wallHeight = std::clamp(static_cast<int>(focal * 1.25 / cameraZ), 6, height);
        const int bottom = horizon + std::clamp(static_cast<int>(focal * 0.50 / cameraZ), 0, height - horizon - 1);
        const int top = bottom - wallHeight;

        if (centerX + halfWidth < 0 || centerX - halfWidth >= width || bottom < 0 || top >= height) {
            continue;
        }

        drawQuad(buffer, width,
            std::clamp(centerX - halfWidth, 0, width - 1), std::clamp(bottom, 0, height - 1),
            std::clamp(centerX + halfWidth, 0, width - 1), std::clamp(bottom, 0, height - 1),
            std::clamp(centerX + halfWidth, 0, width - 1), std::clamp(top, 0, height - 1),
            std::clamp(centerX - halfWidth, 0, width - 1), std::clamp(top, 0, height - 1),
            wallColor);
    }

    draw_line(buffer, width, width / 2 - 7, height / 2, width / 2 + 7, height / 2, crosshairColor);
    draw_line(buffer, width, width / 2, height / 2 - 7, width / 2, height / 2 + 7, crosshairColor);
}

} // namespace fallout
