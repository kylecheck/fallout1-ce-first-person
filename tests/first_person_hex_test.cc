#include "game/first_person_hex.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace fallout;

int main()
{
    // The closed form must equal the shortest path over the native adjacency,
    // from even and odd columns, the corners and the middle, to every tile.
    for (int source : { 0, 1, 199, 200, 201, 20100, 20101, 39999, 39800, 12345, 7777 }) {
        std::vector<int> steps(kFirstPersonTileCount, -1);
        std::vector<int> queue { source };
        steps[source] = 0;
        for (size_t head = 0; head < queue.size(); head++) {
            for (int direction = 0; direction < 6; direction++) {
                const int next = first_person_hex_neighbor(queue[head], direction);
                if (next < 0 || steps[next] >= 0) continue;
                steps[next] = steps[queue[head]] + 1;
                queue.push_back(next);
            }
        }
        for (int tile = 0; tile < kFirstPersonTileCount; tile++) {
            assert(steps[tile] >= 0);
            assert(first_person_hex_distance(source, tile) == steps[tile]);
            assert(first_person_hex_distance(tile, source) == steps[tile]);
        }
    }
    std::cout << "PASS first-person hex distance equals the native adjacency's shortest paths\n";
}
