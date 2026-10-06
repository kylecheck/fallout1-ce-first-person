#ifndef FALLOUT_GAME_FIRST_PERSON_WALL_H_
#define FALLOUT_GAME_FIRST_PERSON_WALL_H_

#include <cmath>
#include <vector>

namespace fallout {

struct FirstPersonWallSegment {
    double ax;
    double ay;
    double bx;
    double by;
    double u0;
    double u1;
};

enum FirstPersonWallKind {
    FIRST_PERSON_WALL_NORTH_SOUTH,
    FIRST_PERSON_WALL_EAST_WEST,
    FIRST_PERSON_WALL_NORTH_CORNER,
    FIRST_PERSON_WALL_SOUTH_CORNER,
    FIRST_PERSON_WALL_EAST_CORNER,
    FIRST_PERSON_WALL_WEST_CORNER,
    FIRST_PERSON_WALL_UNKNOWN,
};

inline FirstPersonWallKind first_person_wall_kind(unsigned int extendedFlags)
{
    switch (extendedFlags & 0xF8000000u) {
    case 0x00000000u:
        return FIRST_PERSON_WALL_NORTH_SOUTH;
    case 0x08000000u:
        return FIRST_PERSON_WALL_EAST_WEST;
    case 0x10000000u:
        return FIRST_PERSON_WALL_NORTH_CORNER;
    case 0x20000000u:
        return FIRST_PERSON_WALL_SOUTH_CORNER;
    case 0x40000000u:
        return FIRST_PERSON_WALL_EAST_CORNER;
    case 0x80000000u:
        return FIRST_PERSON_WALL_WEST_CORNER;
    default:
        return FIRST_PERSON_WALL_UNKNOWN;
    }
}

// Fallout's visible walls are authored on an orthogonal structural lattice
// laid over the movement hexes. East/West pieces need a parity correction
// because consecutive hex centers zigzag by half a world-Y unit. North/South
// pieces do not: their centers already lie on the same world-X line.
//
// Corner sprites live at the intersection of those two lattices. Their
// horizontal arm ends half-way to the neighboring East/West piece. Their
// vertical arm must end at the actual North/South segment boundary
// (worldY +/- 0.5), not at a fixed half-length from the shifted corner vertex.
// That distinction is what closes the quarter-hex holes visible at corners.
inline std::vector<FirstPersonWallSegment> first_person_wall_segments(int tile,
    unsigned int extendedFlags, int rotation, double worldX, double worldY)
{
    constexpr double kHalfColumnSpacing = 0.4330127018922193; // sqrt(3) / 4
    constexpr double kHalfRowSpacing = 0.5;

    const bool oddColumn = ((tile % 200) & 1) != 0;
    const double horizontalY = worldY + (oddColumn ? 0.25 : -0.25);
    const FirstPersonWallKind kind = first_person_wall_kind(extendedFlags);

    std::vector<FirstPersonWallSegment> segments;
    switch (kind) {
    case FIRST_PERSON_WALL_NORTH_SOUTH:
        segments.push_back({ worldX, worldY - kHalfRowSpacing,
            worldX, worldY + kHalfRowSpacing, 0.0, 1.0 });
        break;
    case FIRST_PERSON_WALL_EAST_WEST:
        segments.push_back({ worldX - kHalfColumnSpacing, horizontalY,
            worldX + kHalfColumnSpacing, horizontalY, 0.0, 1.0 });
        break;
    case FIRST_PERSON_WALL_NORTH_CORNER:
        // Tile-space right + down: horizontal joins tile + 1, vertical joins
        // the North/South piece on the next row.
        segments.push_back({ worldX, horizontalY,
            worldX - kHalfColumnSpacing, horizontalY, 0.5, 0.0 });
        segments.push_back({ worldX, horizontalY,
            worldX, worldY + kHalfRowSpacing, 0.5, 1.0 });
        break;
    case FIRST_PERSON_WALL_SOUTH_CORNER:
        // Tile-space left + up.
        segments.push_back({ worldX, horizontalY,
            worldX + kHalfColumnSpacing, horizontalY, 0.5, 0.0 });
        segments.push_back({ worldX, horizontalY,
            worldX, worldY - kHalfRowSpacing, 0.5, 1.0 });
        break;
    case FIRST_PERSON_WALL_EAST_CORNER:
        // Tile-space right + up.
        segments.push_back({ worldX, horizontalY,
            worldX - kHalfColumnSpacing, horizontalY, 0.5, 0.0 });
        segments.push_back({ worldX, horizontalY,
            worldX, worldY - kHalfRowSpacing, 0.5, 1.0 });
        break;
    case FIRST_PERSON_WALL_WEST_CORNER:
        // Tile-space left + down.
        segments.push_back({ worldX, horizontalY,
            worldX + kHalfColumnSpacing, horizontalY, 0.5, 0.0 });
        segments.push_back({ worldX, horizontalY,
            worldX, worldY + kHalfRowSpacing, 0.5, 1.0 });
        break;
    case FIRST_PERSON_WALL_UNKNOWN:
        {
            const double angle = -3.14159265358979323846 / 6.0
                + (rotation % 3) * 3.14159265358979323846 / 3.0;
            const double dx = std::cos(angle) * 0.5;
            const double dy = std::sin(angle) * 0.5;
            segments.push_back({ worldX - dx, worldY - dy,
                worldX + dx, worldY + dy, 0.0, 1.0 });
        }
        break;
    }

    return segments;
}

inline bool first_person_wall_is_corner(FirstPersonWallKind kind)
{
    return kind == FIRST_PERSON_WALL_NORTH_CORNER
        || kind == FIRST_PERSON_WALL_SOUTH_CORNER
        || kind == FIRST_PERSON_WALL_EAST_CORNER
        || kind == FIRST_PERSON_WALL_WEST_CORNER;
}

// Return the tile delta of the straight wall that continues each corner arm.
// This is structural topology only; it is also used to choose a clean material
// for each first-person corner face instead of folding one isometric corner FRM
// around both planes.
inline int first_person_corner_neighbor_delta(FirstPersonWallKind kind, bool horizontal)
{
    switch (kind) {
    case FIRST_PERSON_WALL_NORTH_CORNER:
        return horizontal ? 1 : 200;
    case FIRST_PERSON_WALL_SOUTH_CORNER:
        return horizontal ? -1 : -200;
    case FIRST_PERSON_WALL_EAST_CORNER:
        return horizontal ? 1 : -200;
    case FIRST_PERSON_WALL_WEST_CORNER:
        return horizontal ? -1 : 200;
    default:
        return 0;
    }
}

inline FirstPersonWallSegment first_person_overlap_wall_segment(
    FirstPersonWallSegment segment, double overlap)
{
    const double dx = segment.bx - segment.ax;
    const double dy = segment.by - segment.ay;
    const double length = std::hypot(dx, dy);
    if (length <= 0.0 || overlap <= 0.0) {
        return segment;
    }

    const double scale = overlap / length;
    const double du = segment.u1 - segment.u0;

    // Push both structural endpoints slightly through their nominal joins.
    // Extend UVs by the same fraction; the renderer clamps source lookup to the
    // opaque FRM bounds, so the extra sliver repeats an edge texel instead of
    // stretching the whole sprite. This hides raster/alpha cracks without
    // changing the established wall lattice.
    segment.ax -= dx * scale;
    segment.ay -= dy * scale;
    segment.bx += dx * scale;
    segment.by += dy * scale;
    segment.u0 -= du * scale;
    segment.u1 += du * scale;
    return segment;
}

inline bool first_person_clip_wall(double& ax, double& az, double& bx,
    double& bz, double& u0, double& u1, double nearPlane)
{
    if (az < nearPlane && bz < nearPlane) {
        return false;
    }
    if (az < nearPlane) {
        const double t = (nearPlane - az) / (bz - az);
        ax += (bx - ax) * t;
        u0 += (u1 - u0) * t;
        az = nearPlane;
    } else if (bz < nearPlane) {
        const double t = (nearPlane - bz) / (az - bz);
        bx += (ax - bx) * t;
        u1 += (u0 - u1) * t;
        bz = nearPlane;
    }
    return true;
}

} // namespace fallout

#endif
