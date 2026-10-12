#include "game/first_person_reach.h"
#include <cassert>
#include <iostream>

using namespace fallout;

int main()
{
    // A room with walls at columns/rows 40 and 60 on the real hex grid.
    std::vector<unsigned char> blocked(kFirstPersonTileCount, 0);
    for (int i = 40; i <= 60; i++) {
        blocked[40 * 200 + i] = blocked[60 * 200 + i] = 1;
        blocked[i * 200 + 40] = blocked[i * 200 + 60] = 1;
    }
    std::vector<unsigned char> inside = first_person_reachable_tiles(blocked, { 50 * 200 + 50 });
    assert(inside[45 * 200 + 45] && inside[55 * 200 + 55] && !inside[30 * 200 + 30] && !inside[40 * 200 + 50]);
    // Seeds on blocked hexes still count; invalid seeds are ignored.
    std::vector<unsigned char> outside = first_person_reachable_tiles(blocked, { 0, -5, kFirstPersonTileCount });
    assert(outside[30 * 200 + 30] && !outside[50 * 200 + 50]);
    // An opening joins the two.
    blocked[40 * 200 + 50] = 0;
    assert(first_person_reachable_tiles(blocked, { 0 })[50 * 200 + 50]);
    blocked[40 * 200 + 50] = 1;

    const std::vector<unsigned char> distance = first_person_reach_distance(inside, 4);
    assert(distance[50 * 200 + 50] == 0);
    assert(distance[40 * 200 + 50] == 1);                         // The wall.
    assert(distance[39 * 200 + 50] == 2 || distance[38 * 200 + 50] == 2); // Just behind it.
    assert(distance[10 * 200 + 10] == 4);                         // Capped.
    for (int tile = 0; tile < kFirstPersonTileCount; tile++) {
        if (distance[tile] == 0) assert(inside[tile]);
        if (inside[tile]) assert(distance[tile] == 0);
    }
    std::cout << "PASS first-person reachability and distance to reachable hexes\n";
}
