// Standalone regression tests; no Fallout data or SDL required.
#include "game/first_person_wall.h"
#include <cassert>
#include <iostream>

using namespace fallout;

static void world(int tile, double* x, double* y)
{
    *x = -(tile % 200) * 0.8660254037844386;
    *y = tile / 200 - (tile % 200 & 1) * 0.5;
}

// Fixture for tile.cc's initialized 200-column native neighbor table.
static int neighbor(int tile, int direction)
{
    if (tile < 200 || tile >= 39800 || tile % 200 == 0 || tile % 200 == 199) {
        return tile;
    }
    const int offsets[2][6] = { { -1, 199, 200, 201, 1, -200 },
        { -201, -1, 200, 1, -199, -200 } };
    return tile + offsets[tile % 200 & 1][direction];
}

static bool close(double a, double b) { return std::abs(a - b) < 1e-10; }

static std::vector<FirstPersonWallSegment> segments(int tile,
    const std::unordered_set<int>& tiles, int& count)
{
    double x, y;
    world(tile, &x, &y);
    return first_person_wall_segments(tile, 0, x, y, tiles, neighbor, world, count);
}

int main()
{
    // Both column parities and all six native directions: endpoints from
    // adjacent objects must coincide exactly, regardless of camera position.
    for (int tile : { 20100, 20101 }) {
        for (int d = 0; d < 6; ++d) {
            const int next = neighbor(tile, d);
            assert(neighbor(next, (d + 3) % 6) == tile);
            int count;
            const std::unordered_set<int> tiles { tile, next };
            const auto a = segments(tile, tiles, count);
            assert(count == 1 && a.size() == 2);
            const auto b = segments(next, tiles, count);
            assert(close(a[0].bx, b[0].bx) && close(a[0].by, b[0].by));
            assert(close(a[0].u1 + b[0].u1, 1.0));
            assert(close(std::hypot(a[0].bx - a[1].bx, a[0].by - a[1].by), 1.0));
        }
    }
    const int tile = 20100;
    int count;
    // Corners preserve both arms; straight chains and junctions retain every
    // native neighbor, with no full-length plane through an unrelated axis.
    for (auto directions : { std::vector<int>{0, 2}, {0, 3}, {0, 2, 4} }) {
        std::unordered_set<int> tiles { tile };
        for (int d : directions) tiles.insert(neighbor(tile, d));
        const auto result = segments(tile, tiles, count);
        assert(count == static_cast<int>(directions.size()));
        assert(result.size() == directions.size());
        for (const auto& segment : result) {
            assert(close(std::hypot(segment.bx - segment.ax, segment.by - segment.ay), 0.5));
        }
    }
    // Isolated and map-edge tiles must not connect to themselves.
    for (int isolated : { tile, 0, 199, 39999 }) {
        const auto result = segments(isolated, {isolated}, count);
        assert(count == 0 && result.size() == 1);
        assert(close(std::hypot(result[0].bx - result[0].ax, result[0].by - result[0].ay), 1.0));
    }
    // A wall with its center behind the near plane can still have a visible
    // endpoint. Clipping must retain the correct fraction of the source art.
    double ax = 0, az = 0, bx = 2, bz = 2, u0 = 0, u1 = 1;
    assert(first_person_clip_wall(ax, az, bx, bz, u0, u1, 0.5));
    assert(close(ax, 0.5) && close(az, 0.5) && close(u0, 0.25));
    ax = 2; az = 2; bx = 0; bz = 0; u0 = 1; u1 = 0;
    assert(first_person_clip_wall(ax, az, bx, bz, u0, u1, 0.5));
    assert(close(bx, 0.5) && close(bz, 0.5) && close(u1, 0.25));
    az = 0; bz = 0.25;
    assert(!first_person_clip_wall(ax, az, bx, bz, u0, u1, 0.5));
    az = bz = 0.5;
    assert(first_person_clip_wall(ax, az, bx, bz, u0, u1, 0.5));
    std::cout << "Wall topology and near-plane regression tests passed\n";
}
