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

static bool close(double a, double b) { return std::abs(a - b) < 1e-10; }

static std::vector<FirstPersonWallSegment> segments(int tile, unsigned int flags, int rotation = 0)
{
    double x, y;
    world(tile, &x, &y);
    return first_person_wall_segments(tile, flags, rotation, x, y);
}

static bool endpoint_matches(const FirstPersonWallSegment& a,
    const FirstPersonWallSegment& b)
{
    const double pointsA[2][2] = { { a.ax, a.ay }, { a.bx, a.by } };
    const double pointsB[2][2] = { { b.ax, b.ay }, { b.bx, b.by } };
    for (const auto& pa : pointsA) {
        for (const auto& pb : pointsB) {
            if (close(pa[0], pb[0]) && close(pa[1], pb[1])) {
                return true;
            }
        }
    }
    return false;
}

int main()
{
    constexpr unsigned int ns = 0x00000000u;
    constexpr unsigned int ew = 0x08000000u;
    constexpr unsigned int northCorner = 0x10000000u;
    constexpr unsigned int southCorner = 0x20000000u;
    constexpr unsigned int eastCorner = 0x40000000u;
    constexpr unsigned int westCorner = 0x80000000u;

    // Low action bits are independent of the high wall-light geometry class.
    assert(first_person_wall_kind(0x00002000u) == FIRST_PERSON_WALL_NORTH_SOUTH);
    assert(first_person_wall_kind(0x08002000u) == FIRST_PERSON_WALL_EAST_WEST);
    assert(first_person_wall_kind(0x08002800u) == FIRST_PERSON_WALL_EAST_WEST);

    // East/West walls form one straight line despite alternating hex-center Y.
    for (int tile : { 20100, 20101 }) {
        const auto a = segments(tile, ew);
        const auto b = segments(tile + 1, ew);
        assert(a.size() == 1 && b.size() == 1);
        assert(close(a[0].ay, a[0].by));
        assert(close(b[0].ay, b[0].by));
        assert(close(a[0].ay, b[0].ay));
        assert(endpoint_matches(a[0], b[0]));
    }

    // North/South walls on consecutive rows meet exactly.
    {
        const int tile = 20100;
        const auto a = segments(tile, ns);
        const auto b = segments(tile + 200, ns);
        assert(a.size() == 1 && b.size() == 1);
        assert(close(a[0].ax, a[0].bx));
        assert(close(b[0].ax, b[0].bx));
        assert(endpoint_matches(a[0], b[0]));
    }

    // Real VAULTBUR samples from the topology dump. These assert that corners
    // meet the actual straight-wall lattice, including the asymmetric vertical
    // distance introduced by the East/West parity correction.
    //
    // 13078 North corner -> 13079 East/West run.
    {
        const auto corner = segments(13078, northCorner);
        const auto horizontal = segments(13079, ew);
        assert(corner.size() == 2 && horizontal.size() == 1);
        assert(endpoint_matches(corner[0], horizontal[0]));
        // The vertical arm ends exactly where a North/South wall on tile+200
        // would begin, even though that particular map cell is a blocker.
        const auto vertical = segments(13278, ns);
        assert(endpoint_matches(corner[1], vertical[0]));
    }

    // 13878 South corner closes the run from 13877 and joins the real
    // North/South wall at 13678.
    {
        const auto corner = segments(13878, southCorner);
        const auto horizontal = segments(13877, ew);
        const auto vertical = segments(13678, ns);
        assert(corner.size() == 2);
        assert(endpoint_matches(corner[0], horizontal[0]));
        assert(endpoint_matches(corner[1], vertical[0]));
    }

    // 13890 East corner turns from the 13891 East/West run toward 13690.
    {
        const auto corner = segments(13890, eastCorner);
        const auto horizontal = segments(13891, ew);
        const auto vertical = segments(13690, ns);
        assert(corner.size() == 2);
        assert(endpoint_matches(corner[0], horizontal[0]));
        assert(endpoint_matches(corner[1], vertical[0]));
    }

    // West corners mirror the same lattice relationship.
    {
        const int tile = 13090;
        const auto corner = segments(tile, westCorner);
        const auto horizontal = segments(tile - 1, ew);
        const auto vertical = segments(tile + 200, ns);
        assert(corner.size() == 2);
        assert(endpoint_matches(corner[0], horizontal[0]));
        assert(endpoint_matches(corner[1], vertical[0]));
    }

    // Unknown/custom types retain deterministic fallback geometry.
    {
        const auto fallback = segments(tile, 0x18000000u, 2);
        assert(fallback.size() == 1);
        assert(close(std::hypot(fallback[0].bx - fallback[0].ax,
                         fallback[0].by - fallback[0].ay),
            1.0));
    }


    // Seam overlap extends geometry and UVs symmetrically without changing the
    // segment axis. Adjacent pieces therefore overlap slightly instead of
    // exposing a one-pixel crack after projection.
    {
        FirstPersonWallSegment s { 0.0, 0.0, 1.0, 0.0, 0.0, 1.0 };
        const auto overlapped = first_person_overlap_wall_segment(s, 0.05);
        assert(close(overlapped.ax, -0.05));
        assert(close(overlapped.bx, 1.05));
        assert(close(overlapped.ay, 0.0) && close(overlapped.by, 0.0));
        assert(close(overlapped.u0, -0.05));
        assert(close(overlapped.u1, 1.05));
    }

    // Near-plane clipping must preserve the correct texture fraction.
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

    std::cout << "Wall type lattice and near-plane regression tests passed\n";
}
