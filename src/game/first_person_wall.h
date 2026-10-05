#ifndef FALLOUT_GAME_FIRST_PERSON_WALL_H_
#define FALLOUT_GAME_FIRST_PERSON_WALL_H_

#include <algorithm>
#include <cmath>
#include <vector>

namespace fallout {

enum class FirstPersonWallType {
    NorthSouth = 0,
    EastWest = 1,
    NorthCorner = 2,
    SouthCorner = 3,
    EastCorner = 4,
    WestCorner = 5,
};

// Fallout stores the wall-light type in the high bits of prototype
// extendedFlags. Low action bits (for example 0x2000) must not affect
// structural classification.
inline FirstPersonWallType first_person_wall_type(unsigned int extendedFlags)
{
    switch (extendedFlags & 0xF8000000u) {
    case 0x08000000u:
        return FirstPersonWallType::EastWest;
    case 0x10000000u:
        return FirstPersonWallType::NorthCorner;
    case 0x20000000u:
        return FirstPersonWallType::SouthCorner;
    case 0x40000000u:
        return FirstPersonWallType::EastCorner;
    case 0x80000000u:
        return FirstPersonWallType::WestCorner;
    default:
        return FirstPersonWallType::NorthSouth;
    }
}

struct FirstPersonWallSegment {
    double ax;
    double ay;
    double bx;
    double by;
    double u0;
    double u1;
};

// Structural wall geometry comes from the wall-light type encoded in the PRO,
// not from object rotation, FRM dimensions, or whatever neighbors happen to be
// camera-visible. The existing world mapping places north/south wall centers
// on a straight Y chain. Consecutive east/west wall objects alternate by a
// half-step in Y because of hex-column parity; shift them onto the shared wall
// line before emitting the segment.
inline std::vector<FirstPersonWallSegment> first_person_wall_segments(int tile,
    FirstPersonWallType type, double worldX, double worldY)
{
    constexpr double kSqrt3Over2 = 0.8660254037844386;
    constexpr double kEastWestHalfLength = kSqrt3Over2 * 0.5;
    constexpr double kNorthSouthHalfLength = 0.5;

    const bool oddColumn = (tile % 200 & 1) != 0;
    const double eastWestY = worldY + (oddColumn ? 0.25 : -0.25);

    auto horizontal = [worldX, eastWestY](double sign) {
        return FirstPersonWallSegment {
            worldX,
            eastWestY,
            worldX + sign * kEastWestHalfLength,
            eastWestY,
            sign < 0.0 ? 1.0 : 0.0,
            0.5,
        };
    };
    auto vertical = [worldX, eastWestY](double sign) {
        return FirstPersonWallSegment {
            worldX,
            eastWestY,
            worldX,
            eastWestY + sign * kNorthSouthHalfLength,
            sign < 0.0 ? 0.5 : 0.5,
            sign < 0.0 ? 0.0 : 1.0,
        };
    };

    switch (type) {
    case FirstPersonWallType::NorthSouth:
        return { {
            worldX,
            worldY - kNorthSouthHalfLength,
            worldX,
            worldY + kNorthSouthHalfLength,
            0.0,
            1.0,
        } };
    case FirstPersonWallType::EastWest:
        return { {
            worldX - kEastWestHalfLength,
            eastWestY,
            worldX + kEastWestHalfLength,
            eastWestY,
            0.0,
            1.0,
        } };
    case FirstPersonWallType::NorthCorner:
        // Vault map samples show this corner terminating an east/west run on
        // its west side while turning toward increasing world Y.
        return { horizontal(-1.0), vertical(1.0) };
    case FirstPersonWallType::SouthCorner:
        return { horizontal(1.0), vertical(-1.0) };
    case FirstPersonWallType::EastCorner:
        return { horizontal(-1.0), vertical(-1.0) };
    case FirstPersonWallType::WestCorner:
        return { horizontal(1.0), vertical(1.0) };
    }

    return {};
}

// Interpolate UVs together with camera-space coordinates at the near plane.
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
