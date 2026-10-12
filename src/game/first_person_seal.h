#ifndef FALLOUT_GAME_FIRST_PERSON_SEAL_H_
#define FALLOUT_GAME_FIRST_PERSON_SEAL_H_
#include <cmath>
#include <utility>
#include <vector>
#include "game/first_person_hex.h"
#include "game/first_person_wall.h"
namespace fallout {
// Where walkable space meets a wall hex (a visible wall or an invisible
// blocker) without any drawn wall face covering that hex edge, the isometric
// art relied on overlap and first person would show a gap into the void.
// A seal face closes it: a wall face along the shared hex edge.
struct FirstPersonSealFace {
    double ax;
    double ay;
    double bx;
    double by;
    // A nearby hex with drawn wall segments, whose wall supplies the material.
    int materialTile;
    // The walkable hex and the wall hex whose shared edge this face closes.
    int walkTile;
    int wallTile;
};

// reachable and wallTile are per-hex masks; tileWorld(tile, &x, &y) gives a
// hex's renderer world position and worldTile(x, y) the hex at a position (or
// -1); wallSegments lists each drawn wall segment with the hex of the wall it
// belongs to. An edge counts as covered when rays from its middle and from
// near its ends, straight across it towards the wall hex, all meet a drawn
// segment within maxDepth while passing only through wall hexes: a real face
// stands behind solid wall (as where a row of invisible blockers fronts a
// wall), and a seal would only sit in front of it. A ray that crosses an open
// hex first is looking through a gap.
// voidTile marks hexes with no floor: the empty space around a map's
// interiors. Walkable space meeting one is sealed the same way.
template <typename TileWorld, typename WorldTile>
std::vector<FirstPersonSealFace> first_person_seal_faces(const std::vector<unsigned char>& reachable,
    const std::vector<unsigned char>& wallTile, const std::vector<unsigned char>& voidTile,
    const std::vector<std::pair<int, FirstPersonWallSegment>>& wallSegments, TileWorld tileWorld,
    WorldTile worldTile, double maxDepth = 3.0)
{
    std::vector<std::vector<int>> segmentsAt(kFirstPersonTileCount);
    for (size_t i = 0; i < wallSegments.size(); i++) {
        const int tile = wallSegments[i].first;
        if (tile >= 0 && tile < kFirstPersonTileCount) segmentsAt[tile].push_back(static_cast<int>(i));
    }
    // Hexes within two steps of a hex, the reach of any segment near an edge.
    auto nearby = [](int tile, int rings, std::vector<int>& out) {
        out.assign(1, tile);
        size_t start = 0;
        for (int ring = 0; ring < rings; ring++) {
            const size_t end = out.size();
            for (size_t i = start; i < end; i++) {
                for (int direction = 0; direction < 6; direction++) {
                    const int next = first_person_hex_neighbor(out[i], direction);
                    bool seen = next < 0;
                    for (size_t j = 0; j < out.size() && !seen; j++) seen = out[j] == next;
                    if (!seen) out.push_back(next);
                }
            }
            start = end;
        }
    };
    // Where the ray from (ox, oy) through (px, py) meets segment s, in units of
    // the ray's length to (px, py), or -1 when it misses.
    auto rayHit = [](double ox, double oy, double px, double py, const FirstPersonWallSegment& s) {
        const double rx = px - ox;
        const double ry = py - oy;
        const double sx = s.bx - s.ax;
        const double sy = s.by - s.ay;
        const double denominator = rx * sy - ry * sx;
        if (std::fabs(denominator) < 1e-12) return -1.0;
        const double t = ((s.ax - ox) * sy - (s.ay - oy) * sx) / denominator;
        const double u = ((s.ax - ox) * ry - (s.ay - oy) * rx) / denominator;
        return u >= -0.02 && u <= 1.02 ? t : -1.0;
    };

    constexpr double kHalfEdge = 0.2887 + 0.03; // Half a hex side, plus a seam overlap.
    std::vector<FirstPersonSealFace> faces;
    std::vector<int> around;
    for (int tile = 0; tile < kFirstPersonTileCount; tile++) {
        if (!reachable[tile]) continue;
        double rx;
        double ry;
        tileWorld(tile, &rx, &ry);
        for (int direction = 0; direction < 6; direction++) {
            const int wall = first_person_hex_neighbor(tile, direction);
            if (wall < 0 || reachable[wall] || (!wallTile[wall] && !voidTile[wall])) continue;
            double wx;
            double wy;
            tileWorld(wall, &wx, &wy);
            const double mx = (rx + wx) * 0.5;
            const double my = (ry + wy) * 0.5;
            const double centreDistance = std::hypot(wx - rx, wy - ry);
            if (centreDistance <= 0.0) continue;
            const double edgeX = -(wy - ry) / centreDistance;
            const double edgeY = (wx - rx) / centreDistance;
            nearby(wall, 3, around);
            int materialTile = -1;
            bool covered = true;
            const double acrossX = (wx - rx) / centreDistance;
            const double acrossY = (wy - ry) / centreDistance;
            for (double along : { -0.2, 0.0, 0.2 }) {
                const double px = mx + edgeX * along;
                const double py = my + edgeY * along;
                double nearest = -1.0;
                for (int candidate : around) {
                    for (int index : segmentsAt[candidate]) {
                        if (materialTile < 0) materialTile = candidate;
                        const double t = rayHit(px, py, px + acrossX, py + acrossY, wallSegments[index].second);
                        if (t >= -0.05 && t <= maxDepth && (nearest < 0.0 || t < nearest)) nearest = t;
                    }
                }
                bool hit = nearest >= 0.0;
                for (double step = 0.1; hit && step < nearest - 0.05; step += 0.2) {
                    const int crossed = worldTile(px + acrossX * step, py + acrossY * step);
                    hit = crossed >= 0 && (wallTile[crossed] || reachable[crossed]);
                }
                if (!hit) {
                    covered = false;
                    break;
                }
            }
            if (covered) continue;
            if (materialTile < 0) {
                for (int candidate : around) {
                    if (!segmentsAt[candidate].empty()) {
                        materialTile = candidate;
                        break;
                    }
                }
            }
            if (materialTile < 0) continue;
            faces.push_back({ mx - edgeX * kHalfEdge, my - edgeY * kHalfEdge, mx + edgeX * kHalfEdge,
                my + edgeY * kHalfEdge, materialTile, tile, wall });
        }
    }
    return faces;
}
}
#endif
