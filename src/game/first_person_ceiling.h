#ifndef FALLOUT_GAME_FIRST_PERSON_CEILING_H_
#define FALLOUT_GAME_FIRST_PERSON_CEILING_H_
#include <algorithm>
#include <vector>
namespace fallout {
// Ceilings for elevations with no roof tiles (vaults, caves). Barrier tiles
// are what native movement stops at: walls, doors, invisible blockers and
// blocking scenery such as cave rock. Tiles that cannot be reached from the
// map edge without crossing a barrier are indoors, but only in pockets that
// touch real walls: a pocket closed off by rocks, wagons or crates alone is
// outdoors. A square (2x2 tiles, as the roof grid) gets a ceiling when one of
// its tiles is indoors, or when it holds only barriers and borders such a
// square, so thick walls between rooms are covered too.
constexpr unsigned char kFirstPersonBarrier = 1;
constexpr unsigned char kFirstPersonWallBarrier = 2;
constexpr int kFirstPersonMinWallContacts = 3;
constexpr int kFirstPersonTileGridWidth = 200;
constexpr int kFirstPersonTileCount = 200 * 200;
constexpr int kFirstPersonSquareGridWidth = 100;
constexpr int kFirstPersonSquareCount = 100 * 100;

inline int first_person_tile_square(int tile)
{
    return (tile / kFirstPersonTileGridWidth / 2) * kFirstPersonSquareGridWidth
        + tile % kFirstPersonTileGridWidth / 2;
}

// The hex neighbour in direction 0-5 by the native dir_tile offsets, or -1
// off the map. Unlike tile_num_in_direction it does not stop at the map's
// edge rows, which are where the outdoor flood starts.
inline int first_person_hex_neighbor(int tile, int direction)
{
    constexpr int w = kFirstPersonTileGridWidth;
    static const int offsets[2][6] = {
        { -1, w - 1, w, w + 1, 1, -w },
        { -w - 1, -1, w, 1, 1 - w, -w },
    };
    if (tile < 0 || tile >= kFirstPersonTileCount || direction < 0 || direction >= 6) return -1;
    const int next = tile + offsets[(tile % w) & 1][direction];
    if (next < 0 || next >= kFirstPersonTileCount) return -1;
    const int columnStep = next % w - tile % w;
    return columnStep >= -1 && columnStep <= 1 ? next : -1;
}

// neighbor(tile, direction) returns the adjacent tile in hex direction 0-5,
// or -1 off the map.
template <typename Neighbor>
std::vector<unsigned char> first_person_enclosed_squares(const std::vector<unsigned char>& barrier, Neighbor neighbor)
{
    std::vector<unsigned char> outside(kFirstPersonTileCount, 0);
    std::vector<int> queue;
    queue.reserve(kFirstPersonTileCount);
    for (int i = 0; i < kFirstPersonTileGridWidth; i++) {
        for (int tile : { i, (kFirstPersonTileGridWidth - 1) * kFirstPersonTileGridWidth + i,
                 i * kFirstPersonTileGridWidth, i * kFirstPersonTileGridWidth + kFirstPersonTileGridWidth - 1 }) {
            if (!barrier[tile] && !outside[tile]) {
                outside[tile] = 1;
                queue.push_back(tile);
            }
        }
    }
    for (size_t head = 0; head < queue.size(); head++) {
        for (int direction = 0; direction < 6; direction++) {
            const int next = neighbor(queue[head], direction);
            if (next < 0 || next >= kFirstPersonTileCount || barrier[next] || outside[next]) continue;
            outside[next] = 1;
            queue.push_back(next);
        }
    }

    // Indoor pockets: drop those that touch fewer than kFirstPersonMinWallContacts walls.
    std::vector<unsigned char> indoors(kFirstPersonTileCount, 0);
    std::vector<int> component;
    std::vector<int> contacts;
    for (int start = 0; start < kFirstPersonTileCount; start++) {
        if (barrier[start] || outside[start]) continue;
        outside[start] = 1; // Reused as "visited".
        component.assign(1, start);
        contacts.clear();
        for (size_t head = 0; head < component.size(); head++) {
            for (int direction = 0; direction < 6; direction++) {
                const int next = neighbor(component[head], direction);
                if (next < 0 || next >= kFirstPersonTileCount) continue;
                if (barrier[next] == kFirstPersonWallBarrier) {
                    contacts.push_back(next);
                } else if (!barrier[next] && !outside[next]) {
                    outside[next] = 1;
                    component.push_back(next);
                }
            }
        }
        std::sort(contacts.begin(), contacts.end());
        if (std::unique(contacts.begin(), contacts.end()) - contacts.begin() < kFirstPersonMinWallContacts) continue;
        for (int tile : component) indoors[tile] = 1;
    }

    std::vector<unsigned char> squares(kFirstPersonSquareCount, 0);
    std::vector<unsigned char> barrierOnly(kFirstPersonSquareCount, 1);
    for (int tile = 0; tile < kFirstPersonTileCount; tile++) {
        if (barrier[tile]) continue;
        const int square = first_person_tile_square(tile);
        barrierOnly[square] = 0;
        if (indoors[tile]) squares[square] = 1;
    }
    std::vector<unsigned char> result = squares;
    for (int square = 0; square < kFirstPersonSquareCount; square++) {
        if (squares[square] || !barrierOnly[square]) continue;
        const int x = square % kFirstPersonSquareGridWidth;
        const int y = square / kFirstPersonSquareGridWidth;
        for (int dy = -1; dy <= 1 && !result[square]; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= kFirstPersonSquareGridWidth || ny >= kFirstPersonSquareGridWidth) continue;
                if (squares[ny * kFirstPersonSquareGridWidth + nx]) {
                    result[square] = 1;
                    break;
                }
            }
        }
    }
    return result;
}
}
#endif
