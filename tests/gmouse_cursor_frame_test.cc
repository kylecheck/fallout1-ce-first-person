// Exercise the native 3D-cursor pick/menu frame builders with Fallout 1's
// interface art sizes. Link with section GC to omit unrelated gmouse code.
#include "game/gmouse.cc"
#include "plib/gnw/grbuf.cc"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace fallout {
struct FakeArt {
    Art art {};
    int width;
    int height;
    std::vector<unsigned char> pixels;
    FakeArt(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * h, 7) {}
};
// ACTARROW and its mirror are 29x23; action menu items are 40x40.
static FakeArt arrow(29, 23), mirroredArrow(29, 23), menuItem(40, 40);
static FakeArt* fake(Art* art) { return reinterpret_cast<FakeArt*>(art); }
int art_id(int, int frmId, int, int, int) { return frmId; }
Art* art_ptr_lock(int fid, CacheEntry** entry)
{
    *entry = nullptr;
    if (fid == 250) return &arrow.art;
    if (fid == 285) return &mirroredArrow.art;
    return &menuItem.art;
}
int art_ptr_unlock(CacheEntry*) { return 0; }
int art_frame_width(Art* art, int, int) { return fake(art)->width; }
int art_frame_length(Art* art, int, int) { return fake(art)->height; }
unsigned char* art_frame_data(Art* art, int, int) { return fake(art)->pixels.data(); }
Sound* gsound_load_sound(const char*, Object*) { return nullptr; }
int gsound_play_sound(Sound*) { return 0; }
}

using namespace fallout;

// Destination frame followed by a guard region standing in for the next
// art-cache blocks. Any write past the frame changes the guard.
struct Frame {
    Art art {};
    std::vector<unsigned char> memory;
    int width;
    int height;
    Frame(int w, int h) : memory(static_cast<size_t>(w) * h + 16384, 0xAB), width(w), height(h) {}
    unsigned char* data() { return memory.data(); }
    bool guardIntact() const
    {
        for (size_t i = static_cast<size_t>(width) * height; i < memory.size(); i++) {
            if (memory[i] != 0xAB) return false;
        }
        return true;
    }
    int lastOpaqueRow() const
    {
        for (int y = height - 1; y >= 0; y--) {
            for (int x = 0; x < width; x++) {
                if (memory[static_cast<size_t>(y) * width + x] != 0) return y;
            }
        }
        return -1;
    }
};

int main()
{
    // A 1280x800 screen: gmouse_bk_process passes the map height above the bar.
    constexpr int screenWidth = 1280;
    constexpr int mapHeight = 799 - 99;

    Frame pick(69, 62);
    gmouse_3d_pick_frame = &pick.art;
    gmouse_3d_pick_frame_width = pick.width;
    gmouse_3d_pick_frame_height = pick.height;
    gmouse_3d_pick_frame_size = pick.width * pick.height;
    gmouse_3d_pick_frame_data = pick.data();

    for (int x : { 600, 1250 }) { // Arrow left of the item, then mirrored.
        for (int y = 0; y < 800; y++) {
            std::fill(pick.memory.begin(), pick.memory.end(), 0xAB);
            assert(gmouse_3d_build_pick_frame(x, y, GAME_MOUSE_ACTION_MENU_ITEM_USE, screenWidth, mapHeight) == 0);
            assert(pick.guardIntact());
            int hotX, hotY;
            gmouse_3d_pick_frame_hot(&hotX, &hotY);
            // Native placement where it already fit; clamped to the frame below.
            const int nativeShift = y + 40 - 1 >= mapHeight ? y + 40 - 1 - mapHeight + 2 : 0;
            assert(hotY == std::min(nativeShift, pick.height - 23));
            assert(pick.lastOpaqueRow() == std::max(39, hotY + 22));
        }
    }

    Frame menu(69, 222);
    gmouse_3d_menu_frame = &menu.art;
    gmouse_3d_menu_frame_width = menu.width;
    gmouse_3d_menu_frame_height = menu.height;
    gmouse_3d_menu_frame_size = menu.width * menu.height;
    gmouse_3d_menu_frame_data = menu.data();
    const int items[] = { GAME_MOUSE_ACTION_MENU_ITEM_USE, GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
        GAME_MOUSE_ACTION_MENU_ITEM_CANCEL };
    for (int x : { 600, 1250 }) {
        for (int y = 0; y < 800; y++) {
            std::fill(menu.memory.begin(), menu.memory.end(), 0xAB);
            assert(gmouse_3d_build_menu_frame(x, y, items, 3, screenWidth, mapHeight) == 0);
            assert(menu.guardIntact());
            int hotX, hotY;
            gmouse_3d_menu_frame_hot(&hotX, &hotY);
            const int bottom = y + 3 * 40 - 1;
            const int nativeShift = bottom >= mapHeight ? bottom - mapHeight + 2 : 0;
            assert(hotY == std::min(nativeShift, menu.height - 23));
        }
    }

    std::cout << "PASS cursor pick/menu frames stay in bounds below the native map area\n";
}
