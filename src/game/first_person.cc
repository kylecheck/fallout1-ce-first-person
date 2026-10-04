#include "game/first_person.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/art.h"
#include "game/map.h"
#include "game/object_types.h"
#include "game/tile.h"
#include "game/object.h"
#include "plib/color/color.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"

namespace fallout {

static bool gFirstPersonEnabled = false;

// v0.008: render each real Fallout wall object from its decoded FRM pixels.
// We deliberately keep this independent of the old neighbor-connectivity pass
// so a decorative/invisible wall cannot create a giant false wall plane.
struct FirstPersonWallSprite {
    int fid;
    int direction;
    double x;
    double z;
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

    for (int depth = 1; depth <= 8; depth++) {
        const int y = horizon + static_cast<int>((height - horizon) * (1.0 - 1.0 / (1.0 + depth * 0.55)));
        draw_line(buffer, width, 0, y, width - 1, y, gridColor);
    }

    std::vector<FirstPersonWallSprite> walls;
    for (Object* wall = obj_find_first_at(map_elevation);
         wall != nullptr;
         wall = obj_find_next_at()) {
        if (wall == obj_dude
            || wall->tile < 0
            || FID_TYPE(wall->fid) != OBJ_TYPE_WALL
            || tile_dist(obj_dude->tile, wall->tile) > 18) {
            continue;
        }

        // block.frm is Fallout's 1x1 invisible collision wall. It belongs in
        // future collision handling, not in the visible first-person scene.
        const int frmId = wall->fid & 0xFFF;
        char artName[64] = { 0 };
        if (art_get_base_name(OBJ_TYPE_WALL, frmId, artName) == -1
            || std::strcmp(artName, "block.frm") == 0) {
            continue;
        }

        double wallWorldX;
        double wallWorldY;
        tileToWorld(wall->tile, &wallWorldX, &wallWorldY);
        const double dx = wallWorldX - playerWorldX;
        const double dy = wallWorldY - playerWorldY;
        const double cameraX = dx * rightX + dy * rightY;
        const double cameraZ = dx * forwardX + dy * forwardY;
        if (cameraZ < kNearPlane || cameraZ > kFarPlane) {
            continue;
        }

        const int direction = ((wall->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
        walls.push_back({ wall->fid, direction, cameraX, cameraZ });
    }

    // Fallout's normal renderer is painter based too. Drawing distant sprites
    // first gives us a useful first pass at wall occlusion without a z-buffer.
    std::sort(walls.begin(), walls.end(), [](const FirstPersonWallSprite& a, const FirstPersonWallSprite& b) {
        return a.z > b.z;
    });

    for (const FirstPersonWallSprite& wall : walls) {
        CacheEntry* cacheEntry = nullptr;
        Art* art = art_ptr_lock(wall.fid, &cacheEntry);
        if (art == nullptr) {
            continue;
        }

        ArtFrame* frame = frame_ptr(art, 0, wall.direction);
        unsigned char* pixels = art_frame_data(art, 0, wall.direction);
        if (frame == nullptr || pixels == nullptr || frame->width <= 0 || frame->height <= 0) {
            art_ptr_unlock(cacheEntry);
            continue;
        }

        // The FRM width is useful structural information: common 16/32/48 px
        // Fallout wall pieces become half/full/one-and-a-half hex wide here.
        // Height is normalized because the original FRM includes isometric
        // projection rather than a front-facing physical measurement.
        const double worldWidth = std::max(0.35, frame->width / 32.0);
        constexpr double kWallHeight = 1.65;
        const int projectedWidth = std::max(1, static_cast<int>(focal * worldWidth / wall.z));
        const int projectedHeight = std::max(1, static_cast<int>(focal * kWallHeight / wall.z));
        const int centerX = width / 2 + static_cast<int>(wall.x * focal / wall.z);
        const int bottom = horizon + std::clamp(static_cast<int>(focal * 0.50 / wall.z), 0, height - horizon - 1);
        const int left = centerX - projectedWidth / 2;
        const int top = bottom - projectedHeight;

        for (int screenY = std::max(0, top); screenY <= std::min(height - 1, bottom); screenY++) {
            const int sourceY = std::clamp((screenY - top) * frame->height / projectedHeight, 0, frame->height - 1);
            for (int screenX = std::max(0, left); screenX < std::min(width, left + projectedWidth); screenX++) {
                const int sourceX = std::clamp((screenX - left) * frame->width / projectedWidth, 0, frame->width - 1);
                const unsigned char pixel = pixels[sourceY * frame->width + sourceX];

                // Palette index 0 is transparent in Fallout FRM artwork. Copy
                // every other index directly: our target is the same 8-bit
                // framebuffer/palette used by the original renderer.
                if (pixel != 0) {
                    buffer[screenY * width + screenX] = pixel;
                }
            }
        }

        art_ptr_unlock(cacheEntry);
    }

    draw_line(buffer, width, width / 2 - 7, height / 2, width / 2 + 7, height / 2, crosshairColor);
    draw_line(buffer, width, width / 2, height / 2 - 7, width / 2, height / 2 + 7, crosshairColor);
}

} // namespace fallout
