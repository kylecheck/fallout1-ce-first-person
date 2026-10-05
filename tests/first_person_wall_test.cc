// Standalone regression tests; no Fallout data or SDL required.
#include "game/first_person_wall.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace fallout;

static void world(int tile, double* x, double* y)
{
    *x = -(tile % 200) * 0.8660254037844386;
    *y = tile / 200 - (tile % 200 & 1) * 0.5;
}

static bool close(double a, double b)
{
    return std::abs(a - b) < 1e-10;
}

static std::vector<FirstPersonWallSegment> segments(int tile, FirstPersonWallType type)
{
    double x;
    double y;
    world(tile, &x, &y);
    return first_person_wall_segments(tile, type, x, y);
}

int main()
{
    // Low action bits must not change the wall-light classification.
    assert(first_person_wall_type(0x00000000u) == FirstPersonWallType::NorthSouth);
    assert(first_person_wall_type(0x00002000u) == FirstPersonWallType::NorthSouth);
    assert(first_person_wall_type(0x08000000u) == FirstPersonWallType::EastWest);
    assert(first_person_wall_type(0x08002000u) == FirstPersonWallType::EastWest);
    assert(first_person_wall_type(0x10000000u) == FirstPersonWallType::NorthCorner);
    assert(first_person_wall_type(0x20000000u) == FirstPersonWallType::SouthCorner);
    assert(first_person_wall_type(0x40000000u) == FirstPersonWallType::EastCorner);
    assert(first_person_wall_type(0x80000000u) == FirstPersonWallType::WestCorner);

    // North/south pieces follow the established world Y basis and meet exactly.
    {
        const int aTile = 20100;
        const int bTile = 20300;
        const auto a = segments(aTile, FirstPersonWallType::NorthSouth);
        const auto b = segments(bTile, FirstPersonWallType::NorthSouth);
        assert(a.size() == 1 && b.size() == 1);
        assert(close(a[0].ax, a[0].bx));
        assert(close(b[0].ax, b[0].bx));
        assert(close(a[0].bx, b[0].ax));
        assert(close(a[0].by, b[0].ay));
    }

    // Consecutive east/west wall objects live on alternating hex-center Y
    // values. The parity correction must put both pieces on one straight line.
    {
        const int aTile = 20100;
        const int bTile = 20101;
        const auto a = segments(aTile, FirstPersonWallType::EastWest);
        const auto b = segments(bTile, FirstPersonWallType::EastWest);
        assert(a.size() == 1 && b.size() == 1);
        assert(close(a[0].ay, a[0].by));
        assert(close(b[0].ay, b[0].by));
        assert(close(a[0].ay, b[0].ay));
        assert(close(a[0].ax, b[0].bx));
    }

    // Corner classes emit exactly two perpendicular half-segments and preserve
    // the direction pattern observed in the Vault wall-map snapshot.
    {
        const int tile = 20100;
        const auto north = segments(tile, FirstPersonWallType::NorthCorner);
        const auto south = segments(tile, FirstPersonWallType::SouthCorner);
        const auto east = segments(tile, FirstPersonWallType::EastCorner);
        const auto west = segments(tile, FirstPersonWallType::WestCorner);
        for (const auto* result : { &north, &south, &east, &west }) {
            assert(result->size() == 2);
            const auto& h = (*result)[0];
            const auto& v = (*result)[1];
            assert(close(h.ay, h.by));
            assert(close(v.ax, v.bx));
            assert(close(h.ax, v.ax));
            assert(close(h.ay, v.ay));
        }
        assert(north[0].bx < north[0].ax && north[1].by > north[1].ay);
        assert(south[0].bx > south[0].ax && south[1].by < south[1].ay);
        assert(east[0].bx < east[0].ax && east[1].by < east[1].ay);
        assert(west[0].bx > west[0].ax && west[1].by > west[1].ay);
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

    std::cout << "Wall prototype geometry and near-plane regression tests passed\n";
}
