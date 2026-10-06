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
    std::cout << "PASS first-person HUD layout, bar copies, clipping and redraw signature\n";
}
