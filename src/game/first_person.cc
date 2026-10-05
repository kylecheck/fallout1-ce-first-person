#include "game/first_person.h"
#include "game/first_person_wall.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "game/art.h"
#include "game/map.h"
#include "game/object_types.h"
#include "game/tile.h"
#include "game/object.h"
#include "game/proto.h"
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
    unsigned int extendedFlags;
    double worldX;
    double worldY;
};

struct FirstPersonWallMaterial {
    int fid;
    int direction;
    int width;
    int height;
    std::vector<unsigned char> pixels;
};

struct FirstPersonDoorSprite {
    Object* object;
    int fid;
    int frame;
    int direction;
    int tile;
    unsigned int extendedFlags;
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

// Opt-in snapshot for inspecting actual map topology rather than inferring it
// from a filmed viewport. Export once per process, on the first FP frame after
// loading a save. Include hidden/invisible walls and scenery (including doors).
static void first_person_dump_map()
{
    static bool attempted = false;
    const char* path = std::getenv("FALLOUT_FP_MAP_DUMP");
    if (attempted || path == nullptr || *path == '\0') {
        return;
    }
    attempted = true;
    FILE* output = std::fopen(path, "w");
    if (output == nullptr) {
        std::perror("First-person map dump");
        return;
    }
    std::fprintf(output, "# map=%.16s elevation=%d player_tile=%d rotation=%d\n",
        map_data.name, map_elevation, obj_dude->tile, obj_dude->rotation);
    std::fprintf(output, "id\ttype\ttile\trotation\tfid\tpid\tflags\tproto_flags\textended_flags\tscenery_type\tart\twidth\theight\tframe_x\tframe_y\tneighbor0\tneighbor1\tneighbor2\tneighbor3\tneighbor4\tneighbor5\n");
    for (Object* object = obj_find_first_at(map_elevation);
         object != nullptr; object = obj_find_next_at()) {
        const int type = FID_TYPE(object->fid);
        if (type != OBJ_TYPE_WALL && type != OBJ_TYPE_SCENERY) {
            continue;
        }
        Proto* proto = nullptr;
        unsigned int protoFlags = 0;
        unsigned int extendedFlags = 0;
        int sceneryType = -1;
        if (proto_ptr(object->pid, &proto) == 0 && proto != nullptr) {
            if (type == OBJ_TYPE_WALL && PID_TYPE(object->pid) == OBJ_TYPE_WALL) {
                protoFlags = proto->wall.flags;
                extendedFlags = proto->wall.extendedFlags;
            } else if (type == OBJ_TYPE_SCENERY && PID_TYPE(object->pid) == OBJ_TYPE_SCENERY) {
                protoFlags = proto->scenery.flags;
                extendedFlags = proto->scenery.extendedFlags;
                sceneryType = proto->scenery.type;
            }
        }
        char artName[64] = { 0 };
        art_get_base_name(type, object->fid & 0xFFF, artName);
        int width = 0, height = 0, frameX = 0, frameY = 0;
        CacheEntry* entry = nullptr;
        Art* art = art_ptr_lock(object->fid, &entry);
        if (art != nullptr) {
            const int direction = ((object->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
            const int number = std::clamp(object->frame, 0, std::max(0, art_frame_max_frame(art) - 1));
            ArtFrame* frame = frame_ptr(art, number, direction);
            if (frame != nullptr) {
                width = frame->width;
                height = frame->height;
                frameX = frame->x;
                frameY = frame->y;
            }
            art_ptr_unlock(entry);
        }
        std::fprintf(output, "%d\t%d\t%d\t%d\t%d\t%d\t%08x\t%08x\t%08x\t%d\t%s\t%d\t%d\t%d\t%d",
            object->id, type, object->tile, object->rotation, object->fid,
            object->pid, static_cast<unsigned int>(object->flags), protoFlags,
            extendedFlags, sceneryType, artName, width, height, frameX, frameY);
        for (int direction = 0; direction < ROTATION_COUNT; direction++) {
            const int neighbor = object->tile >= 0 && object->tile < 40000
                ? tile_num_in_direction(object->tile, direction, 1) : -1;
            std::fprintf(output, "\t%d", neighbor);
        }
        std::fprintf(output, "\n");
    }
    const bool writeFailed = std::ferror(output) != 0;
    const int closeResult = std::fclose(output);
    if (writeFailed || closeResult != 0) {
        std::fprintf(stderr, "First-person map dump failed: %s\n", path);
    } else {
        std::fprintf(stderr, "First-person map dump saved: %s\n", path);
    }
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

    first_person_dump_map();

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
    auto debugWallColor = [](FirstPersonWallKind kind) {
        switch (kind) {
        case FIRST_PERSON_WALL_NORTH_SOUTH:
            return 31744; // red
        case FIRST_PERSON_WALL_EAST_WEST:
            return 992; // green
        case FIRST_PERSON_WALL_NORTH_CORNER:
            return 31; // blue
        case FIRST_PERSON_WALL_SOUTH_CORNER:
            return 32736; // yellow
        case FIRST_PERSON_WALL_EAST_CORNER:
            return 31775; // magenta
        case FIRST_PERSON_WALL_WEST_CORNER:
            return 1023; // cyan
        case FIRST_PERSON_WALL_UNKNOWN:
            return 32767; // white
        }
        return 31744;
    };

    std::vector<FirstPersonWallSprite> walls;
    std::vector<FirstPersonWallSprite> blockWallHints;
    for (Object* wall = obj_find_first_at(map_elevation);
         wall != nullptr;
         wall = obj_find_next_at()) {
        if (wall == obj_dude
            || wall->tile < 0
            || wall->tile >= kHexGridWidth * kHexGridWidth
            || (wall->flags & OBJECT_HIDDEN) != 0
            || FID_TYPE(wall->fid) != OBJ_TYPE_WALL
            || tile_dist(obj_dude->tile, wall->tile) > 18) {
            continue;
        }

        // block.frm is invisible collision/topology data. Do not render its
        // 1x1 art, but retain its tile: a chain of blocker cells between two
        // compatible visible walls is strong evidence for a wall span that the
        // original isometric artwork supplied only through overlap.
        const int frmId = wall->fid & 0xFFF;
        char artName[64] = { 0 };
        if (art_get_base_name(OBJ_TYPE_WALL, frmId, artName) == -1) {
            continue;
        }
        if (std::strcmp(artName, "block.frm") == 0) {
            double blockWorldX;
            double blockWorldY;
            tileToWorld(wall->tile, &blockWorldX, &blockWorldY);
            blockWallHints.push_back({
                wall,
                wall->fid,
                ((wall->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT,
                wall->tile,
                0,
                blockWorldX,
                blockWorldY,
            });
            continue;
        }

        unsigned int extendedFlags = 0;
        Proto* proto = nullptr;
        if (PID_TYPE(wall->pid) == OBJ_TYPE_WALL
            && proto_ptr(wall->pid, &proto) == 0
            && proto != nullptr) {
            extendedFlags = static_cast<unsigned int>(proto->wall.extendedFlags);
        }

        double wallWorldX;
        double wallWorldY;
        tileToWorld(wall->tile, &wallWorldX, &wallWorldY);
        const int direction = ((wall->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
        walls.push_back({
            wall,
            wall->fid,
            direction,
            wall->tile,
            extendedFlags,
            wallWorldX,
            wallWorldY,
        });
    }

    // Door scenery is semantic opening data. Never synthesize a structural
    // blocker bridge through a tile occupied by a door; closed/open state will
    // be handled by the scenery renderer instead of being baked into walls.
    std::vector<int> doorTiles;
    std::vector<FirstPersonDoorSprite> doors;
    for (Object* object = obj_find_first_at(map_elevation);
         object != nullptr;
         object = obj_find_next_at()) {
        if (object->tile < 0
            || FID_TYPE(object->fid) != OBJ_TYPE_SCENERY) {
            continue;
        }

        Proto* proto = nullptr;
        if (PID_TYPE(object->pid) == OBJ_TYPE_SCENERY
            && proto_ptr(object->pid, &proto) == 0
            && proto != nullptr
            && proto->scenery.type == SCENERY_TYPE_DOOR) {
            doorTiles.push_back(object->tile);

            if ((object->flags & OBJECT_HIDDEN) == 0
                && tile_dist(obj_dude->tile, object->tile) <= 18) {
                double doorWorldX;
                double doorWorldY;
                tileToWorld(object->tile, &doorWorldX, &doorWorldY);
                doors.push_back({
                    object,
                    object->fid,
                    object->frame,
                    ((object->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT,
                    object->tile,
                    static_cast<unsigned int>(proto->scenery.extendedFlags),
                    doorWorldX,
                    doorWorldY,
                });
            }
        }
    }

    auto doorAtTile = [&doorTiles](int tile) {
        return std::find(doorTiles.begin(), doorTiles.end(), tile) != doorTiles.end();
    };

    // Promote only blocker cells that form a proven bridge between visible
    // wall structure. This recovers spans such as VAULTBUR 13090 -> 13290
    // (block.frm) -> 13490 without turning every collision helper into a wall.
    //
    // A bridge is accepted only when tracing both directions on one structural
    // axis reaches compatible visible walls, passing exclusively through other
    // block.frm cells. The synthetic cell inherits art from a real wall at one
    // end, while its geometry is a normal straight structural segment.
    auto wallAtTile = [&walls](int tile) -> const FirstPersonWallSprite* {
        for (const FirstPersonWallSprite& candidate : walls) {
            if (candidate.tile == tile) {
                return &candidate;
            }
        }
        return nullptr;
    };

    auto blockAtTile = [&blockWallHints](int tile) {
        for (const FirstPersonWallSprite& block : blockWallHints) {
            if (block.tile == tile) {
                return true;
            }
        }
        return false;
    };

    auto supportsVertical = [](FirstPersonWallKind kind) {
        return kind == FIRST_PERSON_WALL_NORTH_SOUTH
            || kind == FIRST_PERSON_WALL_NORTH_CORNER
            || kind == FIRST_PERSON_WALL_SOUTH_CORNER
            || kind == FIRST_PERSON_WALL_EAST_CORNER
            || kind == FIRST_PERSON_WALL_WEST_CORNER;
    };

    auto supportsHorizontal = [](FirstPersonWallKind kind) {
        return kind == FIRST_PERSON_WALL_EAST_WEST
            || kind == FIRST_PERSON_WALL_NORTH_CORNER
            || kind == FIRST_PERSON_WALL_SOUTH_CORNER
            || kind == FIRST_PERSON_WALL_EAST_CORNER
            || kind == FIRST_PERSON_WALL_WEST_CORNER;
    };

    struct WallBridgeEnd {
        const FirstPersonWallSprite* wall;
        int steps;
    };

    auto traceBridgeEnd = [&](int startTile, int delta, bool vertical) -> WallBridgeEnd {
        int tile = startTile + delta;
        for (int step = 1; step <= 6; step++, tile += delta) {
            if (doorAtTile(tile)) {
                break;
            }
            const FirstPersonWallSprite* candidate = wallAtTile(tile);
            if (candidate != nullptr) {
                const FirstPersonWallKind kind =
                    first_person_wall_kind(candidate->extendedFlags);
                const bool compatible = vertical
                    ? supportsVertical(kind)
                    : supportsHorizontal(kind);
                return compatible
                    ? WallBridgeEnd { candidate, step }
                    : WallBridgeEnd { nullptr, step };
            }
            if (!blockAtTile(tile)) {
                break;
            }
        }
        return { nullptr, 0 };
    };

    for (const FirstPersonWallSprite& block : blockWallHints) {
        if (doorAtTile(block.tile)) {
            continue;
        }
        const WallBridgeEnd verticalA = traceBridgeEnd(block.tile, -200, true);
        const WallBridgeEnd verticalB = traceBridgeEnd(block.tile, 200, true);
        const WallBridgeEnd horizontalA = traceBridgeEnd(block.tile, -1, false);
        const WallBridgeEnd horizontalB = traceBridgeEnd(block.tile, 1, false);

        const bool verticalBridge = verticalA.wall != nullptr && verticalB.wall != nullptr;
        const bool horizontalBridge = horizontalA.wall != nullptr && horizontalB.wall != nullptr;
        if (!verticalBridge && !horizontalBridge) {
            continue;
        }

        // If a rare helper qualifies on both axes, choose the shorter proven
        // bridge instead of creating an accidental four-way wall intersection.
        bool useVertical = verticalBridge;
        if (verticalBridge && horizontalBridge) {
            useVertical = verticalA.steps + verticalB.steps
                <= horizontalA.steps + horizontalB.steps;
        }

        const WallBridgeEnd& endA = useVertical ? verticalA : horizontalA;
        const WallBridgeEnd& endB = useVertical ? verticalB : horizontalB;
        const FirstPersonWallKind preferredKind = useVertical
            ? FIRST_PERSON_WALL_NORTH_SOUTH
            : FIRST_PERSON_WALL_EAST_WEST;

        const FirstPersonWallSprite* materialSource = endA.wall;
        if (first_person_wall_kind(endB.wall->extendedFlags) == preferredKind
            && first_person_wall_kind(endA.wall->extendedFlags) != preferredKind) {
            materialSource = endB.wall;
        }

        FirstPersonWallSprite bridge = *materialSource;
        bridge.tile = block.tile;
        bridge.extendedFlags = useVertical ? 0x00000000u : 0x08000000u;
        bridge.worldX = block.worldX;
        bridge.worldY = block.worldY;
        // Keep the real wall as the pick owner; never expose the invisible
        // collision helper as an interactable rendered object.
        walls.push_back(bridge);
    }

    // Build reusable first-person materials from the original isometric wall
    // FRMs. The material owns copied pixels, so the art cache can be unlocked
    // immediately and the same material can be shared by many wall segments.
    std::vector<FirstPersonWallMaterial> wallMaterials;
    auto getWallMaterial = [&wallMaterials](int fid, int direction) -> FirstPersonWallMaterial* {
        for (FirstPersonWallMaterial& material : wallMaterials) {
            if (material.fid == fid && material.direction == direction) {
                return &material;
            }
        }

        CacheEntry* cacheEntry = nullptr;
        Art* art = art_ptr_lock(fid, &cacheEntry);
        if (art == nullptr) {
            return nullptr;
        }

        ArtFrame* frame = frame_ptr(art, 0, direction);
        unsigned char* pixels = art_frame_data(art, 0, direction);
        if (frame == nullptr || pixels == nullptr || frame->width <= 0 || frame->height <= 0) {
            art_ptr_unlock(cacheEntry);
            return nullptr;
        }

        int opaqueMinX = frame->width;
        int opaqueMaxX = -1;
        int opaqueMinY = frame->height;
        int opaqueMaxY = -1;
        std::vector<int> rowOpaqueMinX(frame->height, frame->width);
        std::vector<int> rowOpaqueMaxX(frame->height, -1);

        for (int sy = 0; sy < frame->height; sy++) {
            for (int sx = 0; sx < frame->width; sx++) {
                if (pixels[sy * frame->width + sx] != 0) {
                    rowOpaqueMinX[sy] = std::min(rowOpaqueMinX[sy], sx);
                    rowOpaqueMaxX[sy] = std::max(rowOpaqueMaxX[sy], sx);
                    opaqueMinX = std::min(opaqueMinX, sx);
                    opaqueMaxX = std::max(opaqueMaxX, sx);
                    opaqueMinY = std::min(opaqueMinY, sy);
                    opaqueMaxY = std::max(opaqueMaxY, sy);
                }
            }
        }

        if (opaqueMaxX < opaqueMinX || opaqueMaxY < opaqueMinY) {
            art_ptr_unlock(cacheEntry);
            return nullptr;
        }

        const int materialWidth = opaqueMaxX - opaqueMinX + 1;
        const int materialHeight = opaqueMaxY - opaqueMinY + 1;
        std::vector<unsigned char> rectified(
            static_cast<size_t>(materialWidth) * materialHeight, 0);

        // Preserve source texture scale instead of stretching every scanline
        // independently. Align each row by its opaque center, then repeat only
        // its edge texels where the isometric silhouette narrows. This removes
        // the diagonal cutout while keeping bricks/panels far less warped.
        for (int my = 0; my < materialHeight; my++) {
            int sourceY = opaqueMinY + my;
            if (rowOpaqueMaxX[sourceY] < rowOpaqueMinX[sourceY]) {
                for (int radius = 1; radius < frame->height; radius++) {
                    const int up = sourceY - radius;
                    const int down = sourceY + radius;
                    if (up >= opaqueMinY
                        && rowOpaqueMaxX[up] >= rowOpaqueMinX[up]) {
                        sourceY = up;
                        break;
                    }
                    if (down <= opaqueMaxY
                        && rowOpaqueMaxX[down] >= rowOpaqueMinX[down]) {
                        sourceY = down;
                        break;
                    }
                }
            }

            const int rowMinX = rowOpaqueMinX[sourceY];
            const int rowMaxX = rowOpaqueMaxX[sourceY];
            if (rowMaxX < rowMinX) {
                continue;
            }

            const double rowCenter = (rowMinX + rowMaxX) * 0.5;
            const double outputCenter = (materialWidth - 1) * 0.5;
            for (int mx = 0; mx < materialWidth; mx++) {
                int sourceX = static_cast<int>(std::lround(
                    rowCenter + (mx - outputCenter)));
                sourceX = std::clamp(sourceX, rowMinX, rowMaxX);

                unsigned char pixel = pixels[sourceY * frame->width + sourceX];
                if (pixel == 0) {
                    for (int radius = 1; radius <= rowMaxX - rowMinX && pixel == 0; radius++) {
                        const int leftX = sourceX - radius;
                        const int rightX = sourceX + radius;
                        if (leftX >= rowMinX) {
                            pixel = pixels[sourceY * frame->width + leftX];
                        }
                        if (pixel == 0 && rightX <= rowMaxX) {
                            pixel = pixels[sourceY * frame->width + rightX];
                        }
                    }
                }

                rectified[my * materialWidth + mx] = pixel;
            }
        }

        art_ptr_unlock(cacheEntry);
        wallMaterials.push_back({
            fid,
            direction,
            materialWidth,
            materialHeight,
            std::move(rectified),
        });
        return &wallMaterials.back();
    };

    auto straightMaterialForCornerArm = [&](const FirstPersonWallSprite& corner,
                                           FirstPersonWallKind cornerKind,
                                           const FirstPersonWallSegment& segment)
        -> const FirstPersonWallSprite* {
        if (!first_person_wall_is_corner(cornerKind)) {
            return &corner;
        }

        const bool horizontal =
            std::abs(segment.bx - segment.ax) >= std::abs(segment.by - segment.ay);
        const int delta = first_person_corner_neighbor_delta(cornerKind, horizontal);
        if (delta == 0) {
            return &corner;
        }

        const FirstPersonWallKind desired = horizontal
            ? FIRST_PERSON_WALL_EAST_WEST
            : FIRST_PERSON_WALL_NORTH_SOUTH;

        int tile = corner.tile + delta;
        for (int step = 1; step <= 6; step++, tile += delta) {
            const FirstPersonWallSprite* candidate = wallAtTile(tile);
            if (candidate != nullptr) {
                if (first_person_wall_kind(candidate->extendedFlags) == desired) {
                    return candidate;
                }
                // A different visible wall class is a real topology boundary;
                // don't borrow a texture through it.
                return &corner;
            }

            // Only look farther when the map explicitly supplies blocker
            // topology. This avoids stealing a material from a nearby room.
            if (!blockAtTile(tile)) {
                break;
            }
        }

        return &corner;
    };

    constexpr double kStructuralWallHeight = 1.65;

    // Structural geometry and wall material are deliberately separate. Straight
    // pieces keep their own art; each corner arm first tries to borrow the
    // continuation wall's clean straight material. This avoids folding one
    // isometric corner sprite around two perpendicular first-person planes.
    for (const FirstPersonWallSprite& wall : walls) {
        const FirstPersonWallKind wallKind =
            first_person_wall_kind(wall.extendedFlags);
        const auto segments = first_person_wall_segments(
            wall.tile, wall.extendedFlags, wall.direction, wall.worldX, wall.worldY);

        for (const FirstPersonWallSegment& sourceSegment : segments) {
            const FirstPersonWallSprite* materialWall = &wall;
            if (!debugWalls && first_person_wall_is_corner(wallKind)) {
                materialWall =
                    straightMaterialForCornerArm(wall, wallKind, sourceSegment);
            }

            FirstPersonWallMaterial* material = nullptr;
            if (!debugWalls) {
                material = getWallMaterial(materialWall->fid, materialWall->direction);
                if (material == nullptr || material->width <= 0 || material->height <= 0) {
                    // If an adjacent borrowed material is unavailable, fall
                    // back to the corner's own art before dropping geometry.
                    material = getWallMaterial(wall.fid, wall.direction);
                    materialWall = &wall;
                }
                if (material == nullptr || material->width <= 0 || material->height <= 0) {
                    continue;
                }
            }

            FirstPersonWallSegment materialSegment = sourceSegment;
            if (materialWall != &wall && first_person_wall_is_corner(wallKind)) {
                // Borrowed straight art should cover the whole corner arm.
                // Preserve the arm's original texture direction so patterns do
                // not flip at the join.
                const bool reversed = sourceSegment.u1 < sourceSegment.u0;
                materialSegment.u0 = reversed ? 1.0 : 0.0;
                materialSegment.u1 = reversed ? 0.0 : 1.0;
            }

            const double joinOverlap = first_person_wall_is_corner(wallKind)
                ? 0.055
                : 0.035;
            const FirstPersonWallSegment segment =
                first_person_overlap_wall_segment(materialSegment, joinOverlap);

            const double adx = segment.ax - playerWorldX;
            const double ady = segment.ay - playerWorldY;
            const double bdx = segment.bx - playerWorldX;
            const double bdy = segment.by - playerWorldY;
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
            const int topAY = bottomAY - static_cast<int>(focal * kStructuralWallHeight / az);
            const int topBY = bottomBY - static_cast<int>(focal * kStructuralWallHeight / bz);

            const int minX = std::max(0, std::min(screenAX, screenBX));
            const int maxX = std::min(width - 1, std::max(screenAX, screenBX));
            if (minX > maxX) {
                continue;
            }

            const double screenSpan = static_cast<double>(screenBX - screenAX);
            if (std::abs(screenSpan) < 1.0) {
                continue;
            }

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
                const double worldT =
                    ((1.0 - s) * u0 * invAz + s * u1 * invBz) / invZ;
                const int bottom = static_cast<int>(
                    bottomAY + (bottomBY - bottomAY) * s);
                const int top = static_cast<int>(
                    topAY + (topBY - topAY) * s);
                const int columnHeight = bottom - top;
                if (columnHeight <= 0) {
                    continue;
                }

                for (int screenY = std::max(0, top);
                     screenY <= std::min(height - 1, bottom);
                     screenY++) {
                    unsigned char pixel;
                    if (debugWalls) {
                        pixel = colorTable[debugWallColor(wallKind)];
                    } else {
                        const double materialU = std::clamp(worldT, 0.0, 1.0);
                        const int materialX = std::clamp(
                            static_cast<int>(std::lround(
                                materialU * std::max(0, material->width - 1))),
                            0,
                            std::max(0, material->width - 1));
                        const int materialY = std::clamp(
                            (screenY - top) * material->height / columnHeight,
                            0,
                            std::max(0, material->height - 1));
                        pixel = material->pixels[
                            materialY * material->width + materialX];
                        if (pixel == 0) {
                            continue;
                        }
                    }

                    const int destination = screenY * width + screenX;
                    if (z < depthBuffer[destination]) {
                        buffer[destination] = pixel;
                        depthBuffer[destination] = z;
                        gFirstPersonPicks[destination] = {
                            wall.object,
                            wall.object->id,
                        };
                    }
                }
            }
        }
    }

    // First-person doors are structural scenery while closed. Closed doors
    // occupy the same wall lattice as the opening and participate in depth/pick
    // testing. Fallout animates doors away from frame 0 while opening; those
    // non-zero frames are left to the live scenery billboard pass below so the
    // moving/open door remains visible instead of disappearing from the scene.
    // Native collision/use logic remains authoritative.
    constexpr double kDoorHeight = 1.55;
    for (const FirstPersonDoorSprite& door : doors) {
        if (door.frame != 0) {
            continue;
        }

        FirstPersonWallMaterial* material =
            getWallMaterial(door.fid, door.direction);
        if (material == nullptr || material->width <= 0 || material->height <= 0) {
            continue;
        }

        auto doorSegments = first_person_wall_segments(
            door.tile,
            door.extendedFlags,
            door.direction,
            door.worldX,
            door.worldY);

        for (FirstPersonWallSegment sourceSegment : doorSegments) {
            sourceSegment.u0 = 0.0;
            sourceSegment.u1 = 1.0;
            const FirstPersonWallSegment segment =
                first_person_overlap_wall_segment(sourceSegment, 0.02);

            const double adx = segment.ax - playerWorldX;
            const double ady = segment.ay - playerWorldY;
            const double bdx = segment.bx - playerWorldX;
            const double bdy = segment.by - playerWorldY;
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
            const int topAY = bottomAY - static_cast<int>(focal * kDoorHeight / az);
            const int topBY = bottomBY - static_cast<int>(focal * kDoorHeight / bz);

            const int minX = std::max(0, std::min(screenAX, screenBX));
            const int maxX = std::min(width - 1, std::max(screenAX, screenBX));
            const double screenSpan = static_cast<double>(screenBX - screenAX);
            if (minX > maxX || std::abs(screenSpan) < 1.0) {
                continue;
            }

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
                const double worldT =
                    ((1.0 - s) * u0 * invAz + s * u1 * invBz) / invZ;
                const int bottom = static_cast<int>(
                    bottomAY + (bottomBY - bottomAY) * s);
                const int top = static_cast<int>(
                    topAY + (topBY - topAY) * s);
                const int columnHeight = bottom - top;
                if (columnHeight <= 0) {
                    continue;
                }

                const int materialX = std::clamp(
                    static_cast<int>(std::lround(
                        std::clamp(worldT, 0.0, 1.0)
                        * std::max(0, material->width - 1))),
                    0,
                    std::max(0, material->width - 1));

                for (int screenY = std::max(0, top);
                     screenY <= std::min(height - 1, bottom);
                     screenY++) {
                    const int materialY = std::clamp(
                        (screenY - top) * material->height / columnHeight,
                        0,
                        std::max(0, material->height - 1));
                    const unsigned char pixel =
                        material->pixels[materialY * material->width + materialX];
                    if (pixel == 0) {
                        continue;
                    }

                    const int destination = screenY * width + screenX;
                    if (z < depthBuffer[destination]) {
                        buffer[destination] = pixel;
                        depthBuffer[destination] = z;
                        gFirstPersonPicks[destination] = {
                            door.object,
                            door.object->id,
                        };
                    }
                }
            }
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

        if (type == OBJ_TYPE_SCENERY) {
            Proto* sceneryProto = nullptr;
            if (PID_TYPE(object->pid) == OBJ_TYPE_SCENERY
                && proto_ptr(object->pid, &sceneryProto) == 0
                && sceneryProto != nullptr
                && sceneryProto->scenery.type == SCENERY_TYPE_DOOR
                && object->frame == 0) {
                // Closed doors have their own structural pass above. Once the
                // native door animation advances, keep rendering the live FRM
                // as scenery instead of making the door vanish completely.
                continue;
            }
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
