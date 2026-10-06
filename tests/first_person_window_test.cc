// Compile with section GC to exercise the real native window-stack queries.
#include "plib/gnw/gnw.cc"
#include <cassert>
#include <iostream>
int main()
{
    using namespace fallout;
    Window map {}, view {}, menu {};
    map.id = 1; view.id = 5; menu.id = 9;
    map.rect = view.rect = {0,0,1279,799};
    menu.rect = {400,250,879,549};
    window[0] = &map; window[1] = &view; window[2] = &menu;
    num_windows = 3;
    menu.flags = WINDOW_HIDDEN;
    assert(win_get_top_win(640,400) == 9); // Legacy semantics preserved.
    assert(win_get_top_visible_win(640,400) == 5);
    view.flags = WINDOW_HIDDEN; // Toggle off: Map must own native gamepad input.
    assert(win_get_top_visible_win(640,400) == 1);
    menu.flags = WINDOW_MODAL;
    assert(win_get_top_visible_win(640,400) == 9);
    assert(win_get_top_visible_win(100,100) == 1);
    view.flags = 0;
    assert(win_get_top_visible_win(640,400) == 9); // Visible modal wins over view.
    menu.flags |= WINDOW_HIDDEN;
    assert(win_get_top_visible_win(640,400) == 5);
    map.flags = view.flags = WINDOW_HIDDEN;
    assert(win_get_top_visible_win(640,400) == -1);
    assert(win_get_top_visible_win(-1,-1) == -1);
    std::cout << "PASS native visible window ownership, hidden overlays and modal windows\n";
}
