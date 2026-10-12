#include "game/first_person_seal.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace fallout;

static void world(int tile, double* x, double* y)
{
    const int column = tile % 200;
    const int row = tile / 200;
    *x = -column * 0.8660254037844386;
    *y = row - (column & 1) * 0.5;
}

static int at(double x, double y)
{
    const int column = static_cast<int>(std::lround(-x / 0.8660254037844386));
    const int row = static_cast<int>(std::lround(y + (column & 1) * 0.5));
    return column < 0 || column >= 200 || row < 0 || row >= 200 ? -1 : row * 200 + column;
}

int main()
{
    // One walkable hex next to one wall hex.
    const int walk = 100 * 200 + 100;
    const int wall = first_person_hex_neighbor(walk, 2);
    std::vector<unsigned char> reachable(kFirstPersonTileCount, 0), walls(kFirstPersonTileCount, 0);
    const std::vector<unsigned char> none(kFirstPersonTileCount, 0);
    reachable[walk] = 1;
    walls[wall] = 1;
    double wx, wy, rx, ry;
    world(wall, &wx, &wy);
    world(walk, &rx, &ry);

    // No segments anywhere: nothing to take a material from, no face.
    assert(first_person_seal_faces(reachable, walls, none, {}, world, at).empty());

    // A drawn wall segment far from the shared edge: the edge gets one face,
    // perpendicular to the line between the hexes, centred on it.
    const FirstPersonWallSegment away { wx - 0.2, wy + 0.0, wx - 0.2, wy + 0.01, 0.0, 1.0 };
    std::vector<std::pair<int, FirstPersonWallSegment>> segments { { wall, away } };
    // A wall face well beyond the edge (2.5 past the wall hex): seen through.
    const double ux = (wx - rx), uy = (wy - ry);
    segments[0].second = { wx + ux * 2.5 - uy * 2.0, wy + uy * 2.5 + ux * 2.0,
        wx + ux * 2.5 + uy * 2.0, wy + uy * 2.5 - ux * 2.0, 0.0, 1.0 };
    std::vector<FirstPersonSealFace> faces = first_person_seal_faces(reachable, walls, none, segments, world, at);
    assert(faces.size() == 1 && faces[0].materialTile == wall);
    const double mx = (faces[0].ax + faces[0].bx) * 0.5, my = (faces[0].ay + faces[0].by) * 0.5;
    assert(std::fabs(mx - (rx + wx) * 0.5) < 1e-9 && std::fabs(my - (ry + wy) * 0.5) < 1e-9);
    const double fx = faces[0].bx - faces[0].ax, fy = faces[0].by - faces[0].ay;
    assert(std::fabs(fx * ux + fy * uy) < 1e-9);                        // Perpendicular.
    assert(std::fabs(std::hypot(fx, fy) - 2 * (0.2887 + 0.03)) < 1e-9); // A hex side plus overlap.

    // A segment through the edge's midpoint covers it, and so does a real wall
    // face through the wall hex's centre, just behind the edge.
    segments[0].second = { mx - uy * 0.3, my + ux * 0.3, mx + uy * 0.3, my - ux * 0.3, 0.0, 1.0 };
    assert(first_person_seal_faces(reachable, walls, none, segments, world, at).empty());
    segments[0].second = { wx - uy * 0.5, wy + ux * 0.5, wx + uy * 0.5, wy - ux * 0.5, 0.0, 1.0 };
    assert(first_person_seal_faces(reachable, walls, none, segments, world, at).empty());
    // A face too narrow to cover the edge's ends leaves it open.
    segments[0].second = { wx - uy * 0.05, wy + ux * 0.05, wx + uy * 0.05, wy - ux * 0.05, 0.0, 1.0 };
    assert(first_person_seal_faces(reachable, walls, none, segments, world, at).size() == 1);

    // A row of invisible blockers in front of the wall: the face stands a hex
    // further back but behind solid wall hexes, so the edge is covered; with
    // the blocker hex open instead, the same face is seen through a gap.
    {
        const int behind = first_person_hex_neighbor(wall, 2);
        double bx, by;
        world(behind, &bx, &by);
        walls[behind] = 1;
        segments[0] = { behind, { bx + ux * 0.25 - uy * 2.0, by + uy * 0.25 + ux * 2.0,
                                    bx + ux * 0.25 + uy * 2.0, by + uy * 0.25 - ux * 2.0, 0.0, 1.0 } };
        assert(first_person_seal_faces(reachable, walls, none, segments, world, at).empty());
        walls[behind] = 0;
        assert(first_person_seal_faces(reachable, walls, none, segments, world, at).size() == 1);
    }

    // A floorless (void) neighbour is sealed like a wall hex.
    {
        walls[wall] = 0;
        std::vector<unsigned char> voids(kFirstPersonTileCount, 0);
        voids[wall] = 1;
        const auto voidFaces = first_person_seal_faces(reachable, walls, voids, segments, world, at);
        assert(voidFaces.size() == 1 && voidFaces[0].wallTile == wall && voidFaces[0].walkTile == walk);
        walls[wall] = 1;
    }

    // Unwalkable on both sides, or not a wall hex: no face.
    segments[0].second = { wx + ux * 2.5, wy + uy * 2.5, wx + ux * 2.5 + 0.01, wy + uy * 2.5, 0.0, 1.0 };
    reachable[wall] = 1;
    assert(first_person_seal_faces(reachable, walls, none, segments, world, at).empty());
    reachable[wall] = 0;
    walls[wall] = 0;
    assert(first_person_seal_faces(reachable, walls, none, segments, world, at).empty());
    walls[wall] = 1;

    // An invisible blocker hex with no segment of its own borrows a wall's
    // material from up to three hexes away.
    int far = wall;
    for (int i = 0; i < 3; i++) far = first_person_hex_neighbor(far, 2);
    segments.assign(1, { far, { 0, 0, 0.01, 0, 0, 1 } });
    world(far, &segments[0].second.ax, &segments[0].second.ay);
    segments[0].second.bx = segments[0].second.ax + 0.01;
    segments[0].second.by = segments[0].second.ay;
    faces = first_person_seal_faces(reachable, walls, none, segments, world, at);
    assert(faces.size() == 1 && faces[0].materialTile == far);
    std::cout << "PASS first-person seal faces: uncovered edges, cover, orientation and materials\n";
}
