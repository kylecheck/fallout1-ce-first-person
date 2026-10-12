#ifndef FALLOUT_GAME_FIRST_PERSON_REACH_H_
#define FALLOUT_GAME_FIRST_PERSON_REACH_H_
#include <vector>
#include "game/first_person_hex.h"
namespace fallout {
// Hexes the player can walk to: a flood over native adjacency from seed hexes
// (the entrance, critters, exit grids), stopped by blocked hexes. Doors do not
// block: they can be opened. Returns 1 for reachable hexes.
inline std::vector<unsigned char> first_person_reachable_tiles(const std::vector<unsigned char>& blocked,
    const std::vector<int>& seeds)
{
    std::vector<unsigned char> reachable(kFirstPersonTileCount, 0);
    std::vector<int> queue;
    for (int seed : seeds) {
        if (seed < 0 || seed >= kFirstPersonTileCount || reachable[seed]) continue;
        reachable[seed] = 1;
        queue.push_back(seed);
    }
    for (size_t head = 0; head < queue.size(); head++) {
        for (int direction = 0; direction < 6; direction++) {
            const int next = first_person_hex_neighbor(queue[head], direction);
            if (next < 0 || reachable[next] || blocked[next]) continue;
            reachable[next] = 1;
            queue.push_back(next);
        }
    }
    return reachable;
}

// Hex steps from each hex to the nearest reachable hex, capped at limit
// (reachable hexes are 0; hexes limit or more steps away are limit).
inline std::vector<unsigned char> first_person_reach_distance(const std::vector<unsigned char>& reachable, int limit)
{
    std::vector<unsigned char> distance(kFirstPersonTileCount, static_cast<unsigned char>(limit));
    std::vector<int> queue;
    for (int tile = 0; tile < kFirstPersonTileCount; tile++) {
        if (reachable[tile]) {
            distance[tile] = 0;
            queue.push_back(tile);
        }
    }
    for (size_t head = 0; head < queue.size(); head++) {
        const int step = distance[queue[head]] + 1;
        if (step >= limit) continue;
        for (int direction = 0; direction < 6; direction++) {
            const int next = first_person_hex_neighbor(queue[head], direction);
            if (next < 0 || distance[next] <= step) continue;
            distance[next] = static_cast<unsigned char>(step);
            queue.push_back(next);
        }
    }
    return distance;
}
}
#endif
