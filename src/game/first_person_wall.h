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
// laid over the movement hexes. Consecutive East/West wall tiles alternate
// above and below that line because of hex-column parity, so recenter them by
// one quarter hex before building planes.
inline std::vector<FirstPersonWallSegment> first_person_wall_segments(int tile,
    unsigned int extendedFlags, int rotation, double worldX, double worldY)
{
    constexpr double kHalfColumnSpacing = 0.4330127018922193; // sqrt(3) / 4
    constexpr double kHalfRowSpacing = 0.5;

    const double centerX = worldX;
    const double centerY = worldY + (((tile % 200) & 1) != 0 ? 0.25 : -0.25);
    const FirstPersonWallKind kind = first_person_wall_kind(extendedFlags);

    std::vector<FirstPersonWallSegment> segments;
    switch (kind) {
    case FIRST_PERSON_WALL_NORTH_SOUTH:
        segments.push_back({ centerX, centerY - kHalfRowSpacing,
            centerX, centerY + kHalfRowSpacing, 0.0, 1.0 });
        break;
    case FIRST_PERSON_WALL_EAST_WEST:
        segments.push_back({ centerX - kHalfColumnSpacing, centerY,
            centerX + kHalfColumnSpacing, centerY, 0.0, 1.0 });
        break;
    case FIRST_PERSON_WALL_NORTH_CORNER:
        // Tile-space right + down.
        segments.push_back({ centerX, centerY,
            centerX - kHalfColumnSpacing, centerY, 0.5, 0.0 });
        segments.push_back({ centerX, centerY,
            centerX, centerY + kHalfRowSpacing, 0.5, 1.0 });
        break;
    case FIRST_PERSON_WALL_SOUTH_CORNER:
        // Tile-space left + up.
        segments.push_back({ centerX, centerY,
            centerX + kHalfColumnSpacing, centerY, 0.5, 0.0 });
        segments.push_back({ centerX, centerY,
            centerX, centerY - kHalfRowSpacing, 0.5, 1.0 });
        break;
    case FIRST_PERSON_WALL_EAST_CORNER:
        // Tile-space right + up.
        segments.push_back({ centerX, centerY,
            centerX - kHalfColumnSpacing, centerY, 0.5, 0.0 });
        segments.push_back({ centerX, centerY,
            centerX, centerY - kHalfRowSpacing, 0.5, 1.0 });
        break;
    case FIRST_PERSON_WALL_WEST_CORNER:
        // Tile-space left + down.
        segments.push_back({ centerX, centerY,
            centerX + kHalfColumnSpacing, centerY, 0.5, 0.0 });
        segments.push_back({ centerX, centerY,
            centerX, centerY + kHalfRowSpacing, 0.5, 1.0 });
        break;
    case FIRST_PERSON_WALL_UNKNOWN:
        {
            const double angle = -3.14159265358979323846 / 6.0
                + (rotation % 3) * 3.14159265358979323846 / 3.0;
            const double dx = std::cos(angle) * 0.5;
            const double dy = std::sin(angle) * 0.5;
            segments.push_back({ centerX - dx, centerY - dy,
                centerX + dx, centerY + dy, 0.0, 1.0 });
        }
        break;
    }

    return segments;
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
