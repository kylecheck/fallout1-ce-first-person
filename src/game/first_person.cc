#include "game/first_person.h"
#include "game/first_person_wall.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <vector>

#include "game/art.h"
#include "game/map.h"
#include "game/object_types.h"
#include "game/tile.h"
#include "game/object.h"
#include "plib/color/color.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/mouse.h"

namespace fallout {

static bool gFirstPersonEnabled = false;

// Pick IDs are written only when a visible scene pixel wins the depth test.
// Resolve against live map objects before returning; never dereference cached
// object pointers after a script might have deleted an object.
struct FirstPersonPick {
    Object* object;
    int id;
};
static std::vector<FirstPersonPick> gFirstPersonPicks;
static int gPickWidth = 0;
static int gPickHeight = 0;
static int gPickTile = -1;
static int gPickRotation = -1;
static int gPickElevation = -1;

// Structural inputs are collected before any camera-space clipping.
struct FirstPersonWallSprite {
    Object* object;
    int fid;
    int direction;
    int tile;
    double worldX;
    double worldY;
};

struct FirstPersonObjectSprite {
    Object* object;
    int fid;
    int frame;
    int direction;
    int type;
    double x;
    double z;
};

struct FirstPersonFloorArt {
    int fid;
    Art* art;
    ArtFrame* frame;
    unsigned char* pixels;
    CacheEntry* cacheEntry;
};

bool first_person_is_enabled()
{
    return gFirstPersonEnabled;
}

void first_person_toggle()
{
    gFirstPersonEnabled = !gFirstPersonEnabled;
    gFirstPersonPicks.clear();
}

// Input coordinates are desktop/window coordinates, whereas projection uses
// viewport-local coordinates. Keep a single conversion for highlight and input.
int first_person_target_tile(int screenX, int screenY)
{
    if (!gFirstPersonEnabled || obj_dude == nullptr || display_win == -1) {
        return -1;
    }
    Rect rect;
    if (win_get_rect(display_win, &rect) != 0) {
        return -1;
    }
    const int width = win_width(display_win);
    const int height = win_height(display_win);
    const int x = screenX - rect.ulx;
    const int y = screenY - rect.uly;
    const int horizon = height * 43 / 100;
    if (width <= 0 || height <= 0 || x < 0 || x >= width || y <= horizon || y >= height) {
        return -1;
    }
    const double focal = width * 0.70;
    const double z = focal * 0.50 / (y - horizon);
    if (z < 0.45 || z > 36.0) {
        return -1;
    }
    const double cameraX = (x - width * 0.5) * z / focal;
    constexpr double pi = 3.14159265358979323846;
    const int rotation = ((obj_dude->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
    const double yaw = -pi / 6.0 + rotation * pi / 3.0;
    const double dx = -std::sin(yaw) * cameraX + std::cos(yaw) * z;
    const double dy = std::cos(yaw) * cameraX + std::sin(yaw) * z;
    int isoX;
    int isoY;
    if (tile_coord(obj_dude->tile, &isoX, &isoY, map_elevation) != 0) {
        return -1;
    }
    isoX += static_cast<int>(std::lround(27.712812921102035 * dx + 16.0 * dy));
    isoY += static_cast<int>(std::lround(-6.928203230275509 * dx + 12.0 * dy));
    return tile_num(isoX, isoY, map_elevation, false);
}

Object* first_person_object_at(int screenX, int screenY, int objectType, bool includeDude, int elevation)
{
    if (!gFirstPersonEnabled || obj_dude == nullptr || display_win == -1
        || elevation != map_elevation || elevation != gPickElevation
        || obj_dude->tile != gPickTile || obj_dude->rotation != gPickRotation
        || gPickWidth != win_width(display_win) || gPickHeight != win_height(display_win)
        || gFirstPersonPicks.empty()) {
        return nullptr;
    }
    Rect rect;
    if (win_get_rect(display_win, &rect) != 0) {
        return nullptr;
    }
    const int x = screenX - rect.ulx;
    const int y = screenY - rect.uly;
    if (x < 0 || x >= gPickWidth || y < 0 || y >= gPickHeight) {
        return nullptr;
    }
    const FirstPersonPick pick = gFirstPersonPicks[y * gPickWidth + x];
    if (pick.object == nullptr) {
        return nullptr;
    }
    for (Object* object = obj_find_first_at(elevation); object != nullptr; object = obj_find_next_at()) {
        if (object == pick.object && object->id == pick.id
            && (object->flags & OBJECT_HIDDEN) == 0
            && (includeDude || object != obj_dude)
            && (objectType == -1 || FID_TYPE(object->fid) == objectType)) {
            return object;
        }
    }
    return nullptr;
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

    gPickWidth = width;
    gPickHeight = height;
    gPickTile = obj_dude->tile;
    gPickRotation = obj_dude->rotation;
    gPickElevation = map_elevation;
    gFirstPersonPicks.assign(static_cast<size_t>(width) * height, { nullptr, -1 });

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

    // v0.012: a small software depth buffer shared by the first-person passes.
    // Fallout's original renderer can rely on isometric draw order; once we
    // project sprites into perspective that is no longer enough. Keeping depth
    // per framebuffer pixel gives later wall geometry a proper foundation and
    // stops distant cardboard sprites from drawing through nearer ones.
    std::vector<double> depthBuffer(static_cast<size_t>(width) * height, kFarPlane + 1.0);

    // v0.009: perspective-map Fallout's real floor tiles onto the ground.
    //
    // Rather than inventing a second floor coordinate system, convert each
    // first-person ground sample back into the isometric screen coordinates
    // that Fallout already uses. square_num/square_coord then tell us exactly
    // which original floor FRM and which pixel belongs at that location.
    int playerIsoX = 0;
    int playerIsoY = 0;
    tile_coord(obj_dude->tile, &playerIsoX, &playerIsoY, map_elevation);

    std::vector<FirstPersonFloorArt> floorArts;
    auto getFloorArt = [&floorArts](int fid) -> FirstPersonFloorArt* {
        for (FirstPersonFloorArt& entry : floorArts) {
            if (entry.fid == fid) {
                return &entry;
            }
        }

        CacheEntry* cacheEntry = nullptr;
        Art* art = art_ptr_lock(fid, &cacheEntry);
        if (art == nullptr) {
            return nullptr;
        }

        ArtFrame* frame = frame_ptr(art, 0, 0);
        unsigned char* pixels = art_frame_data(art, 0, 0);
        if (frame == nullptr || pixels == nullptr || frame->width <= 0 || frame->height <= 0) {
            art_ptr_unlock(cacheEntry);
            return nullptr;
        }

        floorArts.push_back({ fid, art, frame, pixels, cacheEntry });
        return &floorArts.back();
    };

    constexpr double kEyeHeight = 0.50;
    constexpr double kIsoXFromWorldX = 27.712812921102035;
    constexpr double kIsoYFromWorldX = -6.928203230275509;

    for (int screenY = horizon + 1; screenY < height; screenY++) {
        const double cameraZ = focal * kEyeHeight / (screenY - horizon);
        if (cameraZ < kNearPlane || cameraZ > kFarPlane) {
            continue;
        }

        for (int screenX = 0; screenX < width; screenX++) {
            const double cameraX = (screenX - width * 0.5) * cameraZ / focal;

            const double worldDx = rightX * cameraX + forwardX * cameraZ;
            const double worldDy = rightY * cameraX + forwardY * cameraZ;

            // These are the inverse of the hex-world basis used above:
            // +1 world Y = (+16,+12) isometric pixels.
            // +1 world X = (+27.713,-6.928) isometric pixels.
            const int isoX = playerIsoX + static_cast<int>(std::lround(kIsoXFromWorldX * worldDx + 16.0 * worldDy));
            const int isoY = playerIsoY + static_cast<int>(std::lround(kIsoYFromWorldX * worldDx + 12.0 * worldDy));

            const int squareTile = square_num(isoX, isoY, map_elevation);
            if (squareTile < 0 || squareTile >= SQUARE_GRID_SIZE) {
                continue;
            }

            const int floorData = square[map_elevation]->field_0[squareTile];
            const int frmId = floorData & 0xFFF;
            const int fid = art_id(OBJ_TYPE_TILE, frmId, 0, 0, 0);

            int floorIsoX = 0;
            int floorIsoY = 0;
            if (square_coord(squareTile, &floorIsoX, &floorIsoY, map_elevation) != 0) {
                continue;
            }

            FirstPersonFloorArt* floorArt = getFloorArt(fid);
            if (floorArt == nullptr) {
                continue;
            }

            const int sourceX = isoX - floorIsoX;
            const int sourceY = isoY - floorIsoY;
            if (sourceX < 0 || sourceX >= floorArt->frame->width
                || sourceY < 0 || sourceY >= floorArt->frame->height) {
                continue;
            }

            const unsigned char pixel = floorArt->pixels[sourceY * floorArt->frame->width + sourceX];
            if (pixel != 0) {
                const int destination = screenY * width + screenX;
                buffer[destination] = pixel;
                depthBuffer[destination] = cameraZ;
            }
        }
    }

    for (FirstPersonFloorArt& entry : floorArts) {
        art_ptr_unlock(entry.cacheEntry);
    }

    // v0.014: first-person mouse-to-world targeting.
    //
    // The mouse position defines a camera ray. Intersect that ray with the
    // ground plane, convert the hit back through Fallout's isometric mapping,
    // then let tile_num resolve the actual engine hex. The highlighted hex is
    // therefore Fallout's own tile, not a second approximation of the map.
    int mouseX = width / 2;
    int mouseY = height / 2;
    mouse_get_position(&mouseX, &mouseY);

    const int targetTile = first_person_target_tile(mouseX, mouseY);
    Rect viewportRect;
    if (win_get_rect(display_win, &viewportRect) == 0) {
        mouseX -= viewportRect.ulx;
        mouseY -= viewportRect.uly;
    }

    if (targetTile >= 0) {
        double targetWorldX;
        double targetWorldY;
        tileToWorld(targetTile, &targetWorldX, &targetWorldY);

        constexpr double kHexRadius = 0.32;
        int hexX[6];
        int hexY[6];
        bool hexVisible = true;
        for (int corner = 0; corner < 6; corner++) {
            const double angle = corner * kPi / 3.0;
            const double worldX = targetWorldX + std::cos(angle) * kHexRadius;
            const double worldY = targetWorldY + std::sin(angle) * kHexRadius;
            const double dx = worldX - playerWorldX;
            const double dy = worldY - playerWorldY;
            const double cameraX = dx * rightX + dy * rightY;
            const double cameraZ = dx * forwardX + dy * forwardY;
            if (cameraZ <= kNearPlane) {
                hexVisible = false;
                break;
            }
            hexX[corner] = width / 2 + static_cast<int>(cameraX * focal / cameraZ);
            hexY[corner] = horizon + static_cast<int>(focal * kEyeHeight / cameraZ);
            // Fallout's software draw_line routine does not clip endpoints.
            // Never hand it an off-screen coordinate: doing so can write past
            // the framebuffer and crash the game. For now, hide a partially
            // off-screen target hex rather than trying to draw only part of it.
            if (hexX[corner] < 0 || hexX[corner] >= width
                || hexY[corner] < 0 || hexY[corner] >= height) {
                hexVisible = false;
                break;
            }
        }

        if (hexVisible) {
            const int highlightColor = colorTable[31744];
            for (int corner = 0; corner < 6; corner++) {
                const int next = (corner + 1) % 6;
                draw_line(buffer, width, hexX[corner], hexY[corner], hexX[next], hexY[next], highlightColor);
            }
        }
    }

    // Retain sparse depth guides for this build. They make it easy to see
    // whether the newly projected floor agrees with our established geometry.
    for (int depth = 1; depth <= 8; depth++) {
        const int y = horizon + static_cast<int>((height - horizon) * (1.0 - 1.0 / (1.0 + depth * 0.55)));
        draw_line(buffer, width, 0, y, width - 1, y, gridColor);
    }

    // Debug geometry uses the same clipping/depth path as textured walls.
    static const bool debugWalls = std::getenv("FALLOUT_FP_WALL_DEBUG") != nullptr;
    std::vector<FirstPersonWallSprite> walls;
    std::unordered_set<int> wallTiles;
    for (Object* wall = obj_find_first_at(map_elevation);
         wall != nullptr;
         wall = obj_find_next_at()) {
        if (wall == obj_dude
            || wall->tile < 0
            || wall->tile >= kHexGridWidth * kHexGridWidth
            || (wall->flags & OBJECT_HIDDEN) != 0
            || FID_TYPE(wall->fid) != OBJ_TYPE_WALL
            || tile_dist(obj_dude->tile, wall->tile) > 19) {
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
        const int direction = ((wall->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
        walls.push_back({ wall, wall->fid, direction, wall->tile, wallWorldX, wallWorldY });
        wallTiles.insert(wall->tile);
    }

    // One extra ring supplies neighbors for every rendered tile (radius 18).
    // Each object owns the half-edge to each occupied native neighbor. Both
    // halves meet at the same midpoint, including bends and junctions. No
    // floating-point distance voting or view-dependent topology is involved.
    for (const FirstPersonWallSprite& wall : walls) {
        if (tile_dist(obj_dude->tile, wall.tile) > 18) {
            continue;
        }
        int neighborCount;
        const auto segments = first_person_wall_segments(wall.tile, wall.direction,
            wall.worldX, wall.worldY, wallTiles,
            [](int tile, int direction) { return tile_num_in_direction(tile, direction, 1); },
            tileToWorld, neighborCount);

        CacheEntry* cacheEntry = nullptr;
        ArtFrame* frame = nullptr;
        unsigned char* pixels = nullptr;
        int opaqueMinX = 0;
        int opaqueMaxX = 0;
        int opaqueMinY = 0;
        int opaqueMaxY = 0;
        if (!debugWalls) {
            Art* art = art_ptr_lock(wall.fid, &cacheEntry);
            if (art == nullptr) {
                continue;
            }

            frame = frame_ptr(art, 0, wall.direction);
            pixels = art_frame_data(art, 0, wall.direction);
            if (frame == nullptr || pixels == nullptr || frame->width <= 0 || frame->height <= 0) {
                art_ptr_unlock(cacheEntry);
                continue;
            }

            // Find the opaque art bounds. Sampling only this region prevents the
            // large transparent margins/anchors in isometric FRMs from becoming
            // stretched empty sections of a first-person wall.
            opaqueMinX = frame->width;
            opaqueMaxX = -1;
            opaqueMinY = frame->height;
            opaqueMaxY = -1;
            for (int sy = 0; sy < frame->height; sy++) {
                for (int sx = 0; sx < frame->width; sx++) {
                    if (pixels[sy * frame->width + sx] != 0) {
                        opaqueMinX = std::min(opaqueMinX, sx);
                        opaqueMaxX = std::max(opaqueMaxX, sx);
                        opaqueMinY = std::min(opaqueMinY, sy);
                        opaqueMaxY = std::max(opaqueMaxY, sy);
                    }
                }
            }
            if (opaqueMaxX < opaqueMinX || opaqueMaxY < opaqueMinY) {
                art_ptr_unlock(cacheEntry);
                continue;
            }

        }

        // FRM reconstruction is a separate material approximation: keep its
        // existing crop and height scaling, never its width/anchor as topology.
        const int opaqueWidth = opaqueMaxX - opaqueMinX + 1;
        const int opaqueHeight = opaqueMaxY - opaqueMinY + 1;
        const double worldHeight = debugWalls ? 1.65
            : std::clamp(opaqueHeight * (1.65 / 110.0), 0.65, 1.85);

        for (const FirstPersonWallSegment& segment : segments) {
            const double endpointAX = segment.ax;
            const double endpointAY = segment.ay;
            const double endpointBX = segment.bx;
            const double endpointBY = segment.by;
            const double adx = endpointAX - playerWorldX;
            const double ady = endpointAY - playerWorldY;
            const double bdx = endpointBX - playerWorldX;
            const double bdy = endpointBY - playerWorldY;
            double ax = adx * rightX + ady * rightY;
            double az = adx * forwardX + ady * forwardY;
            double bx = bdx * rightX + bdy * rightY;
            double bz = bdx * forwardX + bdy * forwardY;

            double u0 = segment.u0;
            double u1 = segment.u1;
            if (!first_person_clip_wall(ax, az, bx, bz, u0, u1, kNearPlane)) {
                continue;
            }

            const int screenAX = width / 2 + static_cast<int>(ax * focal / az);
            const int screenBX = width / 2 + static_cast<int>(bx * focal / bz);
            const int bottomAY = horizon + static_cast<int>(focal * kEyeHeight / az);
            const int bottomBY = horizon + static_cast<int>(focal * kEyeHeight / bz);
            const int topAY = bottomAY - static_cast<int>(focal * worldHeight / az);
            const int topBY = bottomBY - static_cast<int>(focal * worldHeight / bz);

            const int minX = std::max(0, std::min(screenAX, screenBX));
            const int maxX = std::min(width - 1, std::max(screenAX, screenBX));
            if (minX > maxX) {
                continue;
            }

            const double screenSpan = static_cast<double>(screenBX - screenAX);
            if (std::abs(screenSpan) < 1.0) {
                continue;
            }

            // Perspective-correct interpolation along the wall. 1/z is linear in
            // screen space, so it supplies both depth testing and texture position.
            const double invAz = 1.0 / az;
            const double invBz = 1.0 / bz;
            for (int screenX = minX; screenX <= maxX; screenX++) {
                const double s = (screenX - screenAX) / screenSpan;
                if (s < 0.0 || s > 1.0) {
                    continue;
                }

                const double invZ = invAz + (invBz - invAz) * s;
                if (invZ <= 0.0) {
                    continue;
                }
                const double z = 1.0 / invZ;
                const double worldT = ((1.0 - s) * u0 * invAz + s * u1 * invBz) / invZ;
                const int bottom = static_cast<int>(bottomAY + (bottomBY - bottomAY) * s);
                const int top = static_cast<int>(topAY + (topBY - topAY) * s);
                const int columnHeight = bottom - top;
                if (columnHeight <= 0) {
                    continue;
                }

                const int sourceX = std::clamp(
                    opaqueMinX + static_cast<int>(worldT * std::max(0, opaqueWidth - 1)),
                    opaqueMinX,
                    opaqueMaxX);
                for (int screenY = std::max(0, top); screenY <= std::min(height - 1, bottom); screenY++) {
                    const int sourceY = std::clamp(
                        opaqueMinY + (screenY - top) * opaqueHeight / columnHeight,
                        opaqueMinY,
                        opaqueMaxY);
                    const unsigned char pixel = debugWalls
                        ? colorTable[neighborCount == 0 ? 31744 : (neighborCount > 2 ? 32736 : 992)]
                        : pixels[sourceY * frame->width + sourceX];
                    if (pixel == 0) {
                        continue;
                    }

                    const int destination = screenY * width + screenX;
                    if (z < depthBuffer[destination]) {
                        buffer[destination] = pixel;
                        depthBuffer[destination] = z;
                        gFirstPersonPicks[destination] = { wall.object, wall.object->id };
                    }
                }
            }
        }

        if (cacheEntry != nullptr) {
            art_ptr_unlock(cacheEntry);
        }
    }

    // v0.011: expose the rest of Fallout's map objects in first person.
    // This is intentionally a billboard pass for now: scenery, critters,
    // items and misc objects use their live FRM/frame/rotation, anchored to
    // their real map hex. It gives us a much more complete scene while the
    // wall reconstruction work remains independent.
    std::vector<FirstPersonObjectSprite> objectSprites;
    for (Object* object = obj_find_first_at(map_elevation);
         object != nullptr;
         object = obj_find_next_at()) {
        if (object == obj_dude
            || object->tile < 0
            || (object->flags & OBJECT_HIDDEN) != 0
            || tile_dist(obj_dude->tile, object->tile) > 18) {
            continue;
        }

        const int type = FID_TYPE(object->fid);
        if (type != OBJ_TYPE_ITEM
            && type != OBJ_TYPE_CRITTER
            && type != OBJ_TYPE_SCENERY
            && type != OBJ_TYPE_MISC) {
            continue;
        }

        double objectWorldX;
        double objectWorldY;
        tileToWorld(object->tile, &objectWorldX, &objectWorldY);
        const double dx = objectWorldX - playerWorldX;
        const double dy = objectWorldY - playerWorldY;
        const double cameraX = dx * rightX + dy * rightY;
        const double cameraZ = dx * forwardX + dy * forwardY;
        if (cameraZ < kNearPlane || cameraZ > kFarPlane) {
            continue;
        }

        const int direction = ((object->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
        objectSprites.push_back({ object, object->fid, object->frame, direction, type, cameraX, cameraZ });
    }

    std::sort(objectSprites.begin(), objectSprites.end(), [](const FirstPersonObjectSprite& a, const FirstPersonObjectSprite& b) {
        return a.z > b.z;
    });

    for (const FirstPersonObjectSprite& object : objectSprites) {
        CacheEntry* cacheEntry = nullptr;
        Art* art = art_ptr_lock(object.fid, &cacheEntry);
        if (art == nullptr) {
            continue;
        }

        const int maxFrame = art_frame_max_frame(art);
        const int frameNumber = std::clamp(object.frame, 0, std::max(0, maxFrame - 1));
        ArtFrame* frame = frame_ptr(art, frameNumber, object.direction);
        unsigned char* pixels = art_frame_data(art, frameNumber, object.direction);
        if (frame == nullptr || pixels == nullptr || frame->width <= 0 || frame->height <= 0) {
            art_ptr_unlock(cacheEntry);
            continue;
        }

        // Preserve each sprite's original aspect ratio. These dimensions are
        // deliberately approximate; the purpose of this pass is to expose the
        // real map contents, not pretend that the 2D art is finished geometry.
        double worldHeight;
        switch (object.type) {
        case OBJ_TYPE_CRITTER:
            worldHeight = 1.35;
            break;
        case OBJ_TYPE_ITEM:
            worldHeight = std::clamp(frame->height / 80.0, 0.18, 0.70);
            break;
        case OBJ_TYPE_SCENERY:
            worldHeight = std::clamp(frame->height / 72.0, 0.45, 2.40);
            break;
        default:
            worldHeight = std::clamp(frame->height / 80.0, 0.25, 1.60);
            break;
        }

        const double worldWidth = worldHeight * frame->width / frame->height;
        const int projectedWidth = std::max(1, static_cast<int>(focal * worldWidth / object.z));
        const int projectedHeight = std::max(1, static_cast<int>(focal * worldHeight / object.z));
        const int centerX = width / 2 + static_cast<int>(object.x * focal / object.z);
        const int bottom = horizon + std::clamp(static_cast<int>(focal * kEyeHeight / object.z), 0, height - horizon - 1);
        const int left = centerX - projectedWidth / 2;
        const int top = bottom - projectedHeight;

        for (int screenY = std::max(0, top); screenY <= std::min(height - 1, bottom); screenY++) {
            const int sourceY = std::clamp((screenY - top) * frame->height / projectedHeight, 0, frame->height - 1);
            for (int screenX = std::max(0, left); screenX < std::min(width, left + projectedWidth); screenX++) {
                const int sourceX = std::clamp((screenX - left) * frame->width / projectedWidth, 0, frame->width - 1);
                const unsigned char pixel = pixels[sourceY * frame->width + sourceX];
                if (pixel != 0) {
                    const int destination = screenY * width + screenX;
                    if (object.z < depthBuffer[destination]) {
                        buffer[destination] = pixel;
                        depthBuffer[destination] = object.z;
                        gFirstPersonPicks[destination] = { object.object, object.object->id };
                    }
                }
            }
        }

        art_ptr_unlock(cacheEntry);
    }

    // Draw an explicit first-person pointer after our scene has covered the
    // normal Fallout map cursor. This is intentionally simple and high contrast
    // for the prototype; the important part is that its tip and highlighted
    // engine hex now agree.
    if (mouseX >= 0 && mouseX < width && mouseY >= 0 && mouseY < height) {
        const int pointerColor = colorTable[31744];
        const int pointerShadow = colorTable[0];
        for (int i = 0; i <= 9; i++) {
            if (mouseY + i < height) {
                buffer[(mouseY + i) * width + mouseX] = pointerShadow;
                if (mouseX + 1 < width) {
                    buffer[(mouseY + i) * width + mouseX + 1] = pointerColor;
                }
            }
            if (mouseX + i < width) {
                buffer[mouseY * width + mouseX + i] = pointerShadow;
                if (mouseY + 1 < height) {
                    buffer[(mouseY + 1) * width + mouseX + i] = pointerColor;
                }
            }
        }
    }

    draw_line(buffer, width, width / 2 - 7, height / 2, width / 2 + 7, height / 2, crosshairColor);
    draw_line(buffer, width, width / 2, height / 2 - 7, width / 2, height / 2 + 7, crosshairColor);
}

} // namespace fallout
