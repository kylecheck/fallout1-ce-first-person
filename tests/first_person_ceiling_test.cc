#include "game/first_person_ceiling.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

using namespace fallout;

// A stand-in hex adjacency on the 200x200 grid: four sides plus two diagonals.
static int neighbor(int tile, int direction)
{
    const int x = tile % 200;
    const int y = tile / 200;
    static const int dx[6] = { 1, 1, 0, -1, -1, 0 };
    static const int dy[6] = { -1, 0, 1, 1, 0, -1 };
    const int nx = x + dx[direction];
    const int ny = y + dy[direction];
    return nx < 0 || ny < 0 || nx >= 200 || ny >= 200 ? -1 : ny * 200 + nx;
}

static void ring(std::vector<unsigned char>& barrier, int x0, int y0, int x1, int y1,
    unsigned char kind = kFirstPersonWallBarrier)
{
    for (int x = x0; x <= x1; x++) barrier[y0 * 200 + x] = barrier[y1 * 200 + x] = kind;
    for (int y = y0; y <= y1; y++) barrier[y * 200 + x0] = barrier[y * 200 + x1] = kind;
}

static int count(const std::vector<unsigned char>& squares)
{
    int n = 0;
    for (unsigned char s : squares) n += s;
    return n;
}

int main()
{
    assert(first_person_tile_square(0) == 0 && first_person_tile_square(1) == 0 && first_person_tile_square(200) == 0);
    assert(first_person_tile_square(2) == 1 && first_person_tile_square(400) == 100);
    assert(first_person_tile_square(199 * 200 + 199) == 9999);

    // Open ground: nothing is indoors.
    std::vector<unsigned char> barrier(kFirstPersonTileCount, 0);
    assert(count(first_person_enclosed_squares(barrier, neighbor)) == 0);

    // A closed room (walls at tiles 40 and 60) is indoors; outside it is not.
    ring(barrier, 40, 40, 60, 60);
    std::vector<unsigned char> squares = first_person_enclosed_squares(barrier, neighbor);
    assert(squares[25 * 100 + 25] == 1);           // Interior.
    assert(squares[20 * 100 + 20] == 1);           // Square with wall corner and interior tile 41,41.
    assert(squares[10 * 100 + 10] == 0 && squares[35 * 100 + 35] == 0);
    const int room = count(squares);

    // The diagonal step must not leak through a wall corner... but a gap does.
    barrier[40 * 200 + 50] = 0;
    assert(count(first_person_enclosed_squares(barrier, neighbor)) == 0);
    barrier[40 * 200 + 50] = kFirstPersonWallBarrier;

    // A thick wall between two rooms (only barrier tiles in its squares) is
    // covered; a thick wall on the outside edge of a room borders it too.
    for (int y = 40; y <= 60; y++) for (int x = 61; x <= 63; x++) barrier[y * 200 + x] = kFirstPersonWallBarrier;
    ring(barrier, 63, 40, 80, 60);
    squares = first_person_enclosed_squares(barrier, neighbor);
    assert(squares[25 * 100 + 31] == 1); // Tiles 62-63, rows 50-51: barriers only, between the rooms.
    assert(count(squares) > room);

    // A pocket closed off only by scenery (rocks, wagons) is outdoors; one
    // closed by walls on one side and scenery on the rest is indoors.
    std::vector<unsigned char> rocks(kFirstPersonTileCount, 0);
    ring(rocks, 100, 100, 110, 110, kFirstPersonBarrier);
    assert(count(first_person_enclosed_squares(rocks, neighbor)) == 0);
    for (int x = 100; x <= 110; x++) rocks[100 * 200 + x] = kFirstPersonWallBarrier;
    assert(first_person_enclosed_squares(rocks, neighbor)[52 * 100 + 52] == 1);

    // Barriers on the map edge are not seeds; the interior beyond them is open.
    std::vector<unsigned char> edge(kFirstPersonTileCount, 0);
    for (int x = 0; x < 200; x++) edge[x] = 1;
    assert(count(first_person_enclosed_squares(edge, neighbor)) == 0);
    // Native hex adjacency: six distinct neighbours, symmetric, never wrapping.
    for (int tile : { 0, 1, 199, 200, 201, 20100, 20101, 39999, 39800, 12345 }) {
        for (int direction = 0; direction < 6; direction++) {
            const int next = first_person_hex_neighbor(tile, direction);
            if (next == -1) continue;
            assert(std::abs(next % 200 - tile % 200) <= 1 && std::abs(next / 200 - tile / 200) <= 1);
            bool back = false;
            for (int d = 0; d < 6; d++) back = back || first_person_hex_neighbor(next, d) == tile;
            assert(back);
        }
    }
    assert(first_person_hex_neighbor(0, 0) == -1 && first_person_hex_neighbor(0, 5) == -1);
    assert(first_person_hex_neighbor(199, 4) == -1 || first_person_hex_neighbor(199, 4) / 200 != 0);
    // A closed room on the real hex grid, flooded from the map edge.
    std::vector<unsigned char> hexBarrier(kFirstPersonTileCount, 0);
    ring(hexBarrier, 40, 40, 60, 60);
    const std::vector<unsigned char> hexSquares = first_person_enclosed_squares(hexBarrier, first_person_hex_neighbor);
    assert(hexSquares[25 * 100 + 25] == 1 && hexSquares[10 * 100 + 10] == 0 && count(hexSquares) < 200);
    std::cout << "PASS first-person enclosed ceilings: rooms, leaks, thick walls, scenery pockets and map edges\n";
}
