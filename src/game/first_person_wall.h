#ifndef FALLOUT_GAME_FIRST_PERSON_WALL_H_
#define FALLOUT_GAME_FIRST_PERSON_WALL_H_

#include <cmath>
#include <unordered_set>
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

// The caller supplies native tile neighbors and the established world mapping.
// No camera or art inputs: turning and FRM padding cannot alter connectivity.
template <typename NeighborAt, typename TileToWorld>
std::vector<FirstPersonWallSegment> first_person_wall_segments(int tile,
    int rotation, double worldX, double worldY,
    const std::unordered_set<int>& wallTiles, NeighborAt neighborAt,
    TileToWorld tileToWorld, int& neighborCount)
{
    std::vector<FirstPersonWallSegment> segments;
    neighborCount = 0;
    double endDx = 0.0;
    double endDy = 0.0;
    for (int direction = 0; direction < 6; direction++) {
        const int neighbor = neighborAt(tile, direction);
        // The native API returns the input tile at the map boundary.
        if (neighbor == tile || wallTiles.count(neighbor) == 0) {
            continue;
        }
        double nx;
        double ny;
        tileToWorld(neighbor, &nx, &ny);
        endDx = (nx - worldX) * 0.5;
        endDy = (ny - worldY) * 0.5;
        // Keep texture orientation consistent on opposite half-edges.
        const bool positive = direction < 3;
        segments.push_back({ worldX, worldY,
            worldX + endDx, worldY + endDy,
            0.5, positive ? 1.0 : 0.0 });
        neighborCount++;
    }
    if (neighborCount == 1) {
        const double endU = 1.0 - segments.front().u1;
        segments.push_back({ worldX, worldY,
            worldX - endDx, worldY - endDy, 0.5, endU });
    } else if (neighborCount == 0) {
        // Rotation is only a provisional structural fallback for isolated
        // pieces. FRM anchors and opaque width must not change the footprint.
        const double angle = -3.14159265358979323846 / 6.0 + (rotation % 3) * 3.14159265358979323846 / 3.0;
        const double dx = std::cos(angle) * 0.5;
        const double dy = std::sin(angle) * 0.5;
        segments.push_back({ worldX - dx, worldY - dy,
            worldX + dx, worldY + dy, 0.0, 1.0 });
    }
    return segments;
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
