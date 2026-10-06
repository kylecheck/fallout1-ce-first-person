#ifndef FALLOUT_GAME_FIRST_PERSON_HUD_H_
#define FALLOUT_GAME_FIRST_PERSON_HUD_H_
#include <algorithm>
#include <cstdint>
namespace fallout {
// Pieces of the original 640x100 interface bar shown in first person. Native
// code keeps drawing HP/AC, the ammo bar, AP lights and monitor messages into
// the hidden bar, so these regions mirror live native state.
struct FirstPersonHudRect {
    int x;
    int y;
    int width;
    int height;
};
constexpr FirstPersonHudRect kFirstPersonHudMonitor { 0, 0, 212, 97 };
// HP/AC counters; x=463 is the native ammo bar.
constexpr FirstPersonHudRect kFirstPersonHudCounters { 462, 22, 58, 75 };
constexpr FirstPersonHudRect kFirstPersonHudActionPoints { 306, 8, 106, 14 };

struct FirstPersonHudPlacement {
    FirstPersonHudRect source;
    int x;
    int y;
};
struct FirstPersonHudLayout {
    int scale;
    FirstPersonHudPlacement monitor;
    FirstPersonHudPlacement counters;
    FirstPersonHudPlacement actionPoints;
};
// Whole-number scaling keeps the pixel art crisp: 2x at 1280x800.
inline FirstPersonHudLayout first_person_hud_layout(int width, int height)
{
    const int s = std::max(1, std::min(width / 640, height / 400));
    FirstPersonHudLayout layout { s, {}, {}, {} };
    const FirstPersonHudRect monitor = kFirstPersonHudMonitor;
    const FirstPersonHudRect counters = kFirstPersonHudCounters;
    const FirstPersonHudRect lights = kFirstPersonHudActionPoints;
    layout.monitor = { monitor, 8 * s, height - 6 * s - monitor.height * s };
    // Keep the counters' native height relative to the monitor.
    layout.counters = { counters, layout.monitor.x + (monitor.width + 2) * s,
        layout.monitor.y + (counters.y - monitor.y) * s };
    const int countersRight = layout.counters.x + counters.width * s;
    layout.actionPoints = { lights,
        std::max(width / 2 - lights.width * s / 2, countersRight + 4 * s),
        height - 6 * s - lights.height * s };
    return layout;
}
// Nearest-neighbour copy of a source region, clipped to both buffers.
inline void first_person_hud_blit(const unsigned char* source, int sourceWidth, int sourceHeight,
    const FirstPersonHudPlacement& piece, int scale,
    unsigned char* dest, int destWidth, int destHeight)
{
    const FirstPersonHudRect& r = piece.source;
    for (int y = 0; y < r.height * scale; y++) {
        const int sy = r.y + y / scale;
        const int dy = piece.y + y;
        if (sy < 0 || sy >= sourceHeight || dy < 0 || dy >= destHeight) continue;
        for (int x = 0; x < r.width * scale; x++) {
            const int sx = r.x + x / scale;
            const int dx = piece.x + x;
            if (sx < 0 || sx >= sourceWidth || dx < 0 || dx >= destWidth) continue;
            dest[dy * destWidth + dx] = source[sy * sourceWidth + sx];
        }
    }
}
// Fingerprint of the mirrored regions, to redraw when native state changes.
inline std::uint64_t first_person_hud_signature(const unsigned char* source, int sourceWidth, int sourceHeight)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const FirstPersonHudRect& r : { kFirstPersonHudMonitor, kFirstPersonHudCounters,
             kFirstPersonHudActionPoints }) {
        for (int y = std::max(0, r.y); y < std::min(sourceHeight, r.y + r.height); y++) {
            for (int x = std::max(0, r.x); x < std::min(sourceWidth, r.x + r.width); x++) {
                hash = (hash ^ source[y * sourceWidth + x]) * 1099511628211ull;
            }
        }
    }
    return hash;
}
}
#endif
