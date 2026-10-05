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
#include "plib/gnw/mouse.h"

namespace fallout {

static bool gFirstPersonEnabled = false;

// v0.008: render each real Fallout wall object from its decoded FRM pixels.
// We deliberately keep this independent of the old neighbor-connectivity pass
// so a decorative/invisible wall cannot create a giant false wall plane.
struct FirstPersonWallSprite {
    int fid;
    int direction;
    double worldX;
    double worldY;
    double x;
    double z;
};

struct FirstPersonObjectSprite {
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

    int targetTile = -1;
    if (mouseX >= 0 && mouseX < width && mouseY > horizon && mouseY < height) {
        const double cameraZ = focal * kEyeHeight / (mouseY - horizon);
        if (cameraZ >= kNearPlane && cameraZ <= kFarPlane) {
            const double cameraX = (mouseX - width * 0.5) * cameraZ / focal;
            const double worldDx = rightX * cameraX + forwardX * cameraZ;
            const double worldDy = rightY * cameraX + forwardY * cameraZ;
            const int isoX = playerIsoX + static_cast<int>(std::lround(kIsoXFromWorldX * worldDx + 16.0 * worldDy));
            const int isoY = playerIsoY + static_cast<int>(std::lround(kIsoYFromWorldX * worldDx + 12.0 * worldDy));
            targetTile = tile_num(isoX, isoY, map_elevation, false);
        }
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

    // Retain sparse depth guides for this build. They make it easy to see
    // whether the newly projected floor agrees with our established geometry.
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
        walls.push_back({ wall->fid, direction, wallWorldX, wallWorldY, cameraX, cameraZ });
    }

    // Fallout's normal renderer is painter based too. Drawing distant sprites
    // first gives us a useful first pass at wall occlusion without a z-buffer.
    std::sort(walls.begin(), walls.end(), [](const FirstPersonWallSprite& a, const FirstPersonWallSprite& b) {
        return a.z > b.z;
    });

    // v0.015: reconstruct walls on Fallout's actual hex-chain axes.
    //
    // The previous pass used 0/+60/-60 degree planes. Those are the EDGE axes
    // of our hexes, but Fallout places consecutive wall objects on HEX CENTERS.
    // In the world basis used by tileToWorld, neighboring centers lie on
    // +30/+90/-30 degree axes. That 30-degree error was enough to make a room
    // explode into crossing strips as the camera rotated.
    //
    // Prefer topology over artwork metadata: nearby wall objects tell us which
    // way a wall chain actually runs. FRM offsets remain a fallback for isolated
    // end/corner/special pieces. We still use the original art as texture.
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

        // Find the opaque art bounds. Sampling only this region prevents the
        // large transparent margins/anchors in isometric FRMs from becoming
        // stretched empty sections of a first-person wall.
        int opaqueMinX = frame->width;
        int opaqueMaxX = -1;
        int opaqueMinY = frame->height;
        int opaqueMaxY = -1;
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

        // Determine the structural axis from neighboring wall tiles. This is
        // much stronger evidence than the shape of a 2D isometric sprite.
        // Fold opposite directions together because a wall plane has no arrow.
        constexpr double kChainAngles[3] = { kPi / 6.0, kPi / 2.0, -kPi / 6.0 };
        int axisVotes[3] = { 0, 0, 0 };
        for (const FirstPersonWallSprite& neighbor : walls) {
            if (&neighbor == &wall) {
                continue;
            }

            const double ndx = neighbor.worldX - wall.worldX;
            const double ndy = neighbor.worldY - wall.worldY;
            const double distance = std::sqrt(ndx * ndx + ndy * ndy);
            if (distance < 0.70 || distance > 1.15) {
                continue;
            }

            const double nx = ndx / distance;
            const double ny = ndy / distance;
            double bestAlignment = -1.0;
            int bestAxis = 0;
            for (int candidate = 0; candidate < 3; candidate++) {
                const double candidateX = std::cos(kChainAngles[candidate]);
                const double candidateY = std::sin(kChainAngles[candidate]);
                const double alignment = std::abs(nx * candidateX + ny * candidateY);
                if (alignment > bestAlignment) {
                    bestAlignment = alignment;
                    bestAxis = candidate;
                }
            }
            if (bestAlignment > 0.92) {
                axisVotes[bestAxis]++;
            }
        }

        int axis = 0;
        if (axisVotes[1] > axisVotes[axis]) {
            axis = 1;
        }
        if (axisVotes[2] > axisVotes[axis]) {
            axis = 2;
        }

        // Isolated pieces have no topology vote. Use the signed FRM anchor as
        // a fallback, but map it onto the corrected center-to-center axes.
        if (axisVotes[0] == 0 && axisVotes[1] == 0 && axisVotes[2] == 0) {
            if (frame->x > 3) {
                axis = 0;
            } else if (frame->x < -3) {
                axis = 2;
            } else {
                axis = 1;
            }
        }

        const double axisX = std::cos(kChainAngles[axis]);
        const double axisY = std::sin(kChainAngles[axis]);

        // Adjacent hex centers are one world unit apart in this basis. Normal
        // wall pieces should therefore occupy one unit regardless of how much
        // transparent/isometric padding their FRM happens to contain. Wider
        // special art gets a modest extension rather than the old 1.5x stretch.
        const int opaqueWidth = opaqueMaxX - opaqueMinX + 1;
        const int opaqueHeight = opaqueMaxY - opaqueMinY + 1;
        double worldWidth = 1.02;
        if (opaqueWidth >= 44) {
            worldWidth = 1.35;
        }

        // Scale visible height from the common ~110px Fallout wall artwork,
        // with sane limits for short/special pieces.
        const double worldHeight = std::clamp(opaqueHeight * (1.65 / 110.0), 0.65, 1.85);

        const double endpointAX = wall.worldX - axisX * worldWidth * 0.5;
        const double endpointAY = wall.worldY - axisY * worldWidth * 0.5;
        const double endpointBX = wall.worldX + axisX * worldWidth * 0.5;
        const double endpointBY = wall.worldY + axisY * worldWidth * 0.5;

        const double adx = endpointAX - playerWorldX;
        const double ady = endpointAY - playerWorldY;
        const double bdx = endpointBX - playerWorldX;
        const double bdy = endpointBY - playerWorldY;
        double ax = adx * rightX + ady * rightY;
        double az = adx * forwardX + ady * forwardY;
        double bx = bdx * rightX + bdy * rightY;
        double bz = bdx * forwardX + bdy * forwardY;

        // Clip the segment against the near plane so walking right up to a wall
        // cannot explode its projection or drop the entire piece.
        if (az <= kNearPlane && bz <= kNearPlane) {
            art_ptr_unlock(cacheEntry);
            continue;
        }
        if (az <= kNearPlane) {
            const double t = (kNearPlane - az) / (bz - az);
            ax += (bx - ax) * t;
            az = kNearPlane;
        } else if (bz <= kNearPlane) {
            const double t = (kNearPlane - bz) / (az - bz);
            bx += (ax - bx) * t;
            bz = kNearPlane;
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
            art_ptr_unlock(cacheEntry);
            continue;
        }

        const double screenSpan = static_cast<double>(screenBX - screenAX);
        if (std::abs(screenSpan) < 1.0) {
            art_ptr_unlock(cacheEntry);
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
            const double perspectiveT = ((1.0 - s) * invAz) / invZ;
            const double worldT = 1.0 - perspectiveT;
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
                const unsigned char pixel = pixels[sourceY * frame->width + sourceX];
                if (pixel == 0) {
                    continue;
                }

                const int destination = screenY * width + screenX;
                if (z < depthBuffer[destination]) {
                    buffer[destination] = pixel;
                    depthBuffer[destination] = z;
                }
            }
        }

        art_ptr_unlock(cacheEntry);
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
        objectSprites.push_back({ object->fid, object->frame, direction, type, cameraX, cameraZ });
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
                    }
                }
            }
        }

        art_ptr_unlock(cacheEntry);
    }

    draw_line(buffer, width, width / 2 - 7, height / 2, width / 2 + 7, height / 2, crosshairColor);
    draw_line(buffer, width, width / 2, height / 2 - 7, width / 2, height / 2 + 7, crosshairColor);
}

} // namespace fallout
