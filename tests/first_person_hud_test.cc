#include "game/first_person_hud.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace fallout;

static bool inside(const FirstPersonHudPlacement& p, int scale, int width, int height)
{
    return p.x >= 0 && p.y >= 0 && p.x + p.source.width * scale <= width
        && p.y + p.source.height * scale <= height;
}

static bool overlaps(const FirstPersonHudPlacement& a, const FirstPersonHudPlacement& b, int s)
{
    return a.x < b.x + b.source.width * s && b.x < a.x + a.source.width * s
        && a.y < b.y + b.source.height * s && b.y < a.y + a.source.height * s;
}

int main()
{
    // The Deck: original art at exactly 2x, bottom-left cluster, lights centred.
    FirstPersonHudLayout deck = first_person_hud_layout(1280, 800);
    assert(deck.scale == 2);
    assert(deck.monitor.x == 16 && deck.monitor.y == 800 - 12 - 194);
    assert(deck.counters.x == 16 + 214 * 2 && deck.counters.y == deck.monitor.y + 44);
    assert(deck.counters.y + 75 * 2 == deck.monitor.y + 97 * 2); // Bottoms align, as on the bar.
    // Centred would be x=534, under the counters (which end at 560): moved clear.
    assert(deck.actionPoints.x == 560 + 8 && deck.actionPoints.y == 800 - 12 - 28);
    assert(first_person_hud_layout(1920, 1080).actionPoints.x == 960 - 106);

    for (auto size : { std::pair<int, int> { 1280, 800 }, { 640, 480 }, { 800, 600 },
             { 1920, 1080 }, { 1280, 720 }, { 2560, 1600 } }) {
        const FirstPersonHudLayout l = first_person_hud_layout(size.first, size.second);
        assert(l.scale >= 1);
        for (const auto* p : { &l.monitor, &l.counters, &l.actionPoints }) {
            assert(inside(*p, l.scale, size.first, size.second));
        }
        assert(!overlaps(l.monitor, l.counters, l.scale));
        assert(!overlaps(l.counters, l.actionPoints, l.scale));
        assert(!overlaps(l.monitor, l.actionPoints, l.scale));
    }
    assert(first_person_hud_layout(640, 480).scale == 1);
    assert(first_person_hud_layout(1920, 1080).scale == 2);
    assert(first_person_hud_layout(2560, 1600).scale == 4);

    // Copy: each bar pixel becomes a scale x scale block; nothing else changes.
    std::vector<unsigned char> bar(640 * 100);
    for (int i = 0; i < 640 * 100; i++) bar[i] = static_cast<unsigned char>(i * 7 + 1);
    constexpr int W = 1280, H = 800;
    std::vector<unsigned char> screen(W * H + 64, 0xEE);
    for (const auto* p : { &deck.monitor, &deck.counters, &deck.actionPoints }) {
        first_person_hud_blit(bar.data(), 640, 100, *p, 2, screen.data(), W, H);
    }
    for (int i = W * H; i < W * H + 64; i++) assert(screen[i] == 0xEE);
    for (int y = 0; y < 97 * 2; y++) {
        for (int x = 0; x < 212 * 2; x++) {
            assert(screen[(deck.monitor.y + y) * W + deck.monitor.x + x] == bar[(y / 2) * 640 + x / 2]);
        }
    }
    assert(screen[(deck.counters.y + 5) * W + deck.counters.x + 3] == bar[(22 + 2) * 640 + 462 + 1]);
    assert(screen[(deck.actionPoints.y + 27) * W + deck.actionPoints.x + 211] == bar[(8 + 13) * 640 + 306 + 105]);
    assert(screen[0] == 0xEE && screen[(H - 1) * W + W - 1] == 0xEE);

    // Clipping at every edge of both buffers.
    std::vector<unsigned char> small(100 * 60 + 64, 0xEE);
    for (FirstPersonHudPlacement p : { FirstPersonHudPlacement { kFirstPersonHudMonitor, -50, -30 },
             FirstPersonHudPlacement { kFirstPersonHudMonitor, 80, 40 },
             FirstPersonHudPlacement { { 600, 90, 80, 30 }, 0, 0 },
             FirstPersonHudPlacement { { -20, -20, 40, 40 }, 0, 0 } }) {
        first_person_hud_blit(bar.data(), 640, 100, p, 3, small.data(), 100, 60);
        for (int i = 100 * 60; i < 100 * 60 + 64; i++) assert(small[i] == 0xEE);
    }
    // (0,0) was last written by the source clipped at the bar's corner; the
    // final case starts outside the bar, so its first pixels are skipped.
    assert(small[0] == bar[90 * 640 + 600]);
    assert(small[99] == bar[90 * 640 + 600 + 33]);

    // Signature follows the mirrored regions only.
    const std::uint64_t base = first_person_hud_signature(bar.data(), 640, 100);
    bar[50 * 640 + 100]++; // Monitor.
    assert(first_person_hud_signature(bar.data(), 640, 100) != base);
    bar[50 * 640 + 100]--;
    bar[40 * 640 + 480]++; // HP counter.
    assert(first_person_hud_signature(bar.data(), 640, 100) != base);
    bar[40 * 640 + 480]--;
    bar[60 * 640 + 463]++; // Ammo bar.
    assert(first_person_hud_signature(bar.data(), 640, 100) != base);
    bar[60 * 640 + 463]--;
    bar[14 * 640 + 316]++; // First AP light.
    assert(first_person_hud_signature(bar.data(), 640, 100) != base);
    bar[14 * 640 + 316]--;
    bar[50 * 640 + 300]++; // Item slot is not mirrored.
    bar[90 * 640 + 600]++; // Neither are the buttons.
    assert(first_person_hud_signature(bar.data(), 640, 100) == base);
    // Pip-Boy date strip: native pip_date/pip_num positions inside PIP.FRM.
    std::vector<unsigned char> pip(640 * 480);
    for (int i = 0; i < 640 * 480; i++) pip[i] = static_cast<unsigned char>(i % 13 + 100);
    std::vector<unsigned char> digits(360 * 17);
    for (int i = 0; i < 360 * 17; i++) digits[i] = static_cast<unsigned char>((i % 360) / 9); // Glyph index.
    std::vector<unsigned char> months(29 * 180);
    for (int i = 0; i < 29 * 180; i++) months[i] = static_cast<unsigned char>(200 + (i / 29) / 15); // Month index.
    constexpr int DW = kFirstPersonHudPipDate.width, DH = kFirstPersonHudPipDate.height;
    std::vector<unsigned char> strip(DW * DH + 64, 0xEE);
    first_person_hud_compose_date({ pip.data(), 640, 480 }, { digits.data(), 360, 17 },
        { months.data(), 29, 180 }, 7, 5, 2161, 1342, strip.data());
    for (int i = DW * DH; i < DW * DH + 64; i++) assert(strip[i] == 0xEE);
    assert(strip[2 * DW + 2] == pip[2 * 640 + 2]); // Background outside the fields.
    assert(strip[17 * DW + 20] == 0 && strip[17 * DW + 29] == 5); // Day "05".
    assert(strip[18 * DW + 46] == 206 && strip[31 * DW + 74] == 206); // July, 29x14.
    assert(strip[32 * DW + 46] == pip[32 * 640 + 46]);
    assert(strip[17 * DW + 83] == 2 && strip[17 * DW + 92] == 1 && strip[17 * DW + 101] == 6
        && strip[33 * DW + 118] == 1); // Year "2161".
    assert(strip[17 * DW + 155] == 1 && strip[17 * DW + 164] == 3 && strip[17 * DW + 173] == 4
        && strip[17 * DW + 182] == 2); // Time "1342".
    assert(strip[17 * DW + 191] == pip[17 * 640 + 191]);
    // Missing or small art never reads or writes outside its image.
    std::vector<unsigned char> tiny(4 * 4, 9);
    first_person_hud_compose_date({ tiny.data(), 4, 4 }, { nullptr, 0, 0 }, { tiny.data(), 4, 4 },
        13, -3, 99999, 0, strip.data());
    for (int i = DW * DH; i < DW * DH + 64; i++) assert(strip[i] == 0xEE);
    assert(strip[0] == 9 && strip[17 * DW + 20] == 0);
    const FirstPersonHudPlacement date = first_person_hud_date_placement(deck.scale);
    assert(date.x == 8 && date.y == 8 && date.x + DW * 2 < 1280 - 570); // Clear of the top-right hints.
    // Sprite copy keeps transparency and clips.
    const unsigned char icon[2 * 2] = { 0, 5, 6, 0 };
    std::vector<unsigned char> view(6 * 6 + 16, 0xEE);
    first_person_hud_blit_sprite(icon, 2, 2, 1, 1, 2, view.data(), 6, 6);
    assert(view[1 * 6 + 1] == 0xEE && view[1 * 6 + 3] == 5 && view[2 * 6 + 4] == 5 && view[3 * 6 + 2] == 6);
    assert(view[4 * 6 + 4] == 0xEE);
    first_person_hud_blit_sprite(icon, 2, 2, 4, 4, 2, view.data(), 6, 6);
    first_person_hud_blit_sprite(icon, 2, 2, -3, -3, 2, view.data(), 6, 6);
    for (int i = 36; i < 52; i++) assert(view[i] == 0xEE);
    assert(view[0] == 0xEE && view[4 * 6 + 2] == 6 && view[5 * 6 + 5] == 0xEE); // Fully clipped copies change nothing.
    std::cout << "PASS first-person HUD layout, bar copies, clipping, Pip-Boy date strip, sprites and redraw signature\n";
}
