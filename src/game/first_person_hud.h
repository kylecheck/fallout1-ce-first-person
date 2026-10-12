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
// As first_person_hud_blit for a whole sprite, leaving its transparent
// (index 0) pixels unwritten, e.g. an action icon over the world.
inline void first_person_hud_blit_sprite(const unsigned char* source, int sourceWidth, int sourceHeight,
    int x, int y, int scale, unsigned char* dest, int destWidth, int destHeight)
{
    for (int sy = 0; sy < sourceHeight * scale; sy++) {
        const int dy = y + sy;
        if (dy < 0 || dy >= destHeight) continue;
        for (int sx = 0; sx < sourceWidth * scale; sx++) {
            const int dx = x + sx;
            const unsigned char pixel = source[(sy / scale) * sourceWidth + sx / scale];
            if (dx < 0 || dx >= destWidth || pixel == 0) continue;
            dest[dy * destWidth + dx] = pixel;
        }
    }
}

// Pip-Boy date/time strip: the top-left of PIP.FRM, filled with NUMBERS.FRM
// digits and MONTHS.FRM names at the positions the Pip-Boy itself uses.
constexpr FirstPersonHudRect kFirstPersonHudPipDate { 0, 0, 222, 48 };
struct FirstPersonHudArt {
    const unsigned char* data;
    int width;
    int height;
};
// Opaque copy, as the Pip-Boy's buf_to_buf, clipped to both images.
inline void first_person_hud_copy(const FirstPersonHudArt& art, int sx, int sy, int w, int h,
    unsigned char* dest, int destWidth, int destHeight, int dx, int dy)
{
    if (art.data == nullptr) return;
    for (int y = 0; y < h; y++) {
        if (sy + y < 0 || sy + y >= art.height || dy + y < 0 || dy + y >= destHeight) continue;
        for (int x = 0; x < w; x++) {
            if (sx + x < 0 || sx + x >= art.width || dx + x < 0 || dx + x >= destWidth) continue;
            dest[(dy + y) * destWidth + dx + x] = art.data[(sy + y) * art.width + sx + x];
        }
    }
}
// Native pip_num: right-aligned 9x17 digits.
inline void first_person_hud_number(const FirstPersonHudArt& numbers, int value, int digits,
    int x, int y, unsigned char* dest, int destWidth, int destHeight)
{
    value = std::max(0, value);
    for (int i = digits - 1; i >= 0; i--) {
        first_person_hud_copy(numbers, 9 * (value % 10), 0, 9, 17, dest, destWidth, destHeight, x + 9 * i, y);
        value /= 10;
    }
}
// Builds the strip into dest (kFirstPersonHudPipDate size). hhmm as
// game_time_hour(); month 1-12 as game_time_date().
inline void first_person_hud_compose_date(const FirstPersonHudArt& background,
    const FirstPersonHudArt& numbers, const FirstPersonHudArt& months,
    int month, int day, int year, int hhmm, unsigned char* dest)
{
    const FirstPersonHudRect r = kFirstPersonHudPipDate;
    first_person_hud_copy(background, r.x, r.y, r.width, r.height, dest, r.width, r.height, 0, 0);
    first_person_hud_number(numbers, day, 2, 20 - r.x, 17 - r.y, dest, r.width, r.height);
    if (month >= 1 && month <= 12) {
        first_person_hud_copy(months, 0, 15 * (month - 1), 29, 14, dest, r.width, r.height, 46 - r.x, 18 - r.y);
    }
    first_person_hud_number(numbers, year, 4, 83 - r.x, 17 - r.y, dest, r.width, r.height);
    first_person_hud_number(numbers, hhmm, 4, 155 - r.x, 17 - r.y, dest, r.width, r.height);
}
// Top-left, at the same whole-number scale as the bottom pieces.
inline FirstPersonHudPlacement first_person_hud_date_placement(int scale)
{
    return { { 0, 0, kFirstPersonHudPipDate.width, kFirstPersonHudPipDate.height }, 4 * scale, 4 * scale };
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
