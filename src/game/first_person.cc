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
include "game/first_person.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
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

            const int frmId = wall->fid & 0xFFF;
            const long long variantKey =
                (static_cast<long long>(wall->fid) << 8)
                ^ (static_cast<long long>(wall->rotation & 0xFF));
            if (gReportedWallVariants.insert(variantKey).second) {
                char artName[64] = { 0 };
                if (art_get_base_name(OBJ_TYPE_WALL, frmId, artName) == -1) {
                    std::snprintf(artName, sizeof(artName), "<unknown>");
                }

                CacheEntry* artCacheEntry = nullptr;
                Art* art = art_ptr_lock(wall->fid, &artCacheEntry);
                if (art != nullptr) {
                    const int direction = ((wall->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
                    ArtFrame* frame = frame_ptr(art, 0, direction);
                    int frameOffsetX = 0;
                    int frameOffsetY = 0;
                    art_frame_offset(art, direction, &frameOffsetX, &frameOffsetY);

                    std::printf("FPWALL tile=%d pid=0x%08X fid=0x%08X frm=%d rot=%d art=%s flags=0x%08X size=%dx%d frameXY=%d,%d dirOffset=%d,%d
",
                        wall->tile,
                        static_cast<unsigned int>(wall->pid),
                        static_cast<unsigned int>(wall->fid),
                        frmId,
                        wall->rotation,
                        artName,
                        static_cast<unsigned int>(wall->flags),
                        frame != nullptr ? frame->width : 0,
                        frame != nullptr ? frame->height : 0,
                        frame != nullptr ? frame->x : 0,
                        frame != nullptr ? frame->y : 0,
                        frameOffsetX,
                        frameOffsetY);

                    art_ptr_unlock(artCacheEntry);
                } else {
                    std::printf("FPWALL tile=%d pid=0x%08X fid=0x%08X frm=%d rot=%d art=%s flags=0x%08X art=<lock-failed>
",
                        wall->tile,
                        static_cast<unsigned int>(wall->pid),
                        static_cast<unsigned int>(wall->fid),
                        frmId,
                        wall->rotation,
                        artName,
                        static_cast<unsigned int>(wall->flags));
                }
                std::fflush(stdout);
            }
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
