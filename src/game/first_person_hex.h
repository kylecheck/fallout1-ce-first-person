#ifndef FALLOUT_GAME_FIRST_PERSON_HEX_H_
#define FALLOUT_GAME_FIRST_PERSON_HEX_H_
namespace fallout {
// Fallout's 200x200 hex grid: tile = row * 200 + column, with even columns
// shifted down half a hex (the native dir_tile offsets).
constexpr int kFirstPersonTileGridWidth = 200;
constexpr int kFirstPersonTileCount = 200 * 200;

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

// Hex steps between two tiles in closed form: the grid as axial coordinates,
// q = column and r = row - (column + (column & 1)) / 2. Native tile_dist walks
// the path one step at a time, at two tile_coord calls per step.
inline int first_person_hex_distance(int tileA, int tileB)
{
    const int qa = tileA % kFirstPersonTileGridWidth;
    const int qb = tileB % kFirstPersonTileGridWidth;
    const int ra = tileA / kFirstPersonTileGridWidth - (qa + (qa & 1)) / 2;
    const int rb = tileB / kFirstPersonTileGridWidth - (qb + (qb & 1)) / 2;
    const int dq = qa - qb;
    const int dr = ra - rb;
    const int ds = dq + dr;
    return ((dq < 0 ? -dq : dq) + (dr < 0 ? -dr : dr) + (ds < 0 ? -ds : ds)) / 2;
}
}
#endif
