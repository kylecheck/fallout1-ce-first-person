#include "game/first_person_gpu.h"

#include <SDL.h>

#include "game/first_person.h"
#include "game/gmouse.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/svga.h"

namespace fallout {

void first_person_gpu_present()
{
    if (!first_person_is_enabled()
        || gSdlRenderer == nullptr
        || first_person_window() == -1) {
        return;
    }

    const int width = screenGetWidth();
    const int height = screenGetHeight();
    if (width <= 0 || height <= 0) {
        return;
    }

    // Native modal windows retain normal ownership. Only draw the first-person
    // GPU overlay while the center of the viewport is actually on our window.
    if (win_get_top_win(width / 2, height / 2) != first_person_window()) {
        return;
    }

    Uint8 r = 220;
    Uint8 g = 220;
    Uint8 b = 220;

    switch (gmouse_3d_get_mode()) {
    case GAME_MOUSE_MODE_ARROW:
        r = 96;
        g = 255;
        b = 96;
        break;
    case GAME_MOUSE_MODE_CROSSHAIR:
        r = 64;
        g = 255;
        b = 64;
        break;
    case GAME_MOUSE_MODE_MOVE:
    default:
        break;
    }

    SDL_SetRenderDrawBlendMode(gSdlRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(gSdlRenderer, r, g, b, 255);

    const int cx = width / 2;
    const int cy = height / 2;
    constexpr int kGap = 3;
    constexpr int kLength = 8;

    SDL_RenderDrawLine(gSdlRenderer, cx - kLength, cy, cx - kGap, cy);
    SDL_RenderDrawLine(gSdlRenderer, cx + kGap, cy, cx + kLength, cy);
    SDL_RenderDrawLine(gSdlRenderer, cx, cy - kLength, cx, cy - kGap);
    SDL_RenderDrawLine(gSdlRenderer, cx, cy + kGap, cx, cy + kLength);

    // Restore the renderer's default opaque white state for subsequent frames.
    SDL_SetRenderDrawColor(gSdlRenderer, 255, 255, 255, 255);
    SDL_SetRenderDrawBlendMode(gSdlRenderer, SDL_BLENDMODE_NONE);
}

} // namespace fallout
