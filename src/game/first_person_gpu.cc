#include "game/first_person_gpu.h"

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "game/first_person.h"
#include "game/gmouse.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/svga.h"

namespace fallout {

struct FirstPersonGpuTexture {
    long long key;
    int width;
    int height;
    SDL_Texture* texture;
};

struct FirstPersonGpuSprite {
    long long key;
    int sourceX;
    int sourceY;
    int sourceWidth;
    int sourceHeight;
    int destinationX;
    int destinationY;
    int destinationWidth;
    int destinationHeight;
};

static std::vector<FirstPersonGpuTexture> gFirstPersonGpuTextures;
static std::vector<FirstPersonGpuSprite> gFirstPersonGpuSprites;

static SDL_Texture* first_person_gpu_texture(
    long long key,
    const unsigned char* pixels,
    int width,
    int height)
{
    for (FirstPersonGpuTexture& entry : gFirstPersonGpuTextures) {
        if (entry.key == key && entry.width == width && entry.height == height) {
            return entry.texture;
        }
    }

    if (pixels == nullptr
        || width <= 0
        || height <= 0
        || gSdlRenderer == nullptr
        || gSdlSurface == nullptr
        || gSdlSurface->format == nullptr
        || gSdlSurface->format->palette == nullptr) {
        return nullptr;
    }

    std::vector<std::uint32_t> rgba(static_cast<size_t>(width) * height, 0);
    SDL_Palette* palette = gSdlSurface->format->palette;
    SDL_PixelFormat* rgbaFormat = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
    if (rgbaFormat == nullptr) {
        return nullptr;
    }

    for (int index = 0; index < width * height; index++) {
        const unsigned char pixel = pixels[index];
        if (pixel == 0) {
            rgba[index] = SDL_MapRGBA(rgbaFormat, 0, 0, 0, 0);
            continue;
        }

        const SDL_Color color = palette->colors[pixel];
        rgba[index] = SDL_MapRGBA(
            rgbaFormat,
            color.r,
            color.g,
            color.b,
            255);
    }

    SDL_FreeFormat(rgbaFormat);

    SDL_Texture* texture = SDL_CreateTexture(
        gSdlRenderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STATIC,
        width,
        height);
    if (texture == nullptr) {
        return nullptr;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    if (SDL_UpdateTexture(
            texture,
            nullptr,
            rgba.data(),
            width * static_cast<int>(sizeof(std::uint32_t)))
        != 0) {
        SDL_DestroyTexture(texture);
        return nullptr;
    }

    gFirstPersonGpuTextures.push_back({ key, width, height, texture });
    return texture;
}

void first_person_gpu_begin_frame()
{
    gFirstPersonGpuSprites.clear();
}

void first_person_gpu_submit_indexed_sprite(
    long long key,
    const unsigned char* pixels,
    int sourceWidth,
    int sourceHeight,
    int sourceX,
    int sourceY,
    int sourceCropWidth,
    int sourceCropHeight,
    int destinationX,
    int destinationY,
    int destinationWidth,
    int destinationHeight)
{
    if (pixels == nullptr
        || sourceWidth <= 0
        || sourceHeight <= 0
        || sourceCropWidth <= 0
        || sourceCropHeight <= 0
        || destinationWidth <= 0
        || destinationHeight <= 0) {
        return;
    }

    if (first_person_gpu_texture(key, pixels, sourceWidth, sourceHeight) == nullptr) {
        return;
    }

    gFirstPersonGpuSprites.push_back({
        key,
        sourceX,
        sourceY,
        sourceCropWidth,
        sourceCropHeight,
        destinationX,
        destinationY,
        destinationWidth,
        destinationHeight,
    });
}

void first_person_gpu_present()
{
    if (!first_person_overlay_visible()
        || gSdlRenderer == nullptr
        || first_person_window() == -1) {
        return;
    }

    const int width = screenGetWidth();
    const int height = screenGetHeight();
    if (width <= 0 || height <= 0) {
        return;
    }

    for (const FirstPersonGpuSprite& sprite : gFirstPersonGpuSprites) {
        SDL_Texture* texture = nullptr;
        for (FirstPersonGpuTexture& entry : gFirstPersonGpuTextures) {
            if (entry.key == sprite.key) {
                texture = entry.texture;
                break;
            }
        }
        if (texture == nullptr) {
            continue;
        }

        SDL_Rect source {
            sprite.sourceX,
            sprite.sourceY,
            sprite.sourceWidth,
            sprite.sourceHeight,
        };
        SDL_Rect destination {
            sprite.destinationX,
            sprite.destinationY,
            sprite.destinationWidth,
            sprite.destinationHeight,
        };
        SDL_RenderCopy(gSdlRenderer, texture, &source, &destination);
    }

    Uint8 r = 220;
    Uint8 g = 220;
    Uint8 b = 220;

    switch (first_person_mode()) {
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

    SDL_SetRenderDrawColor(gSdlRenderer, 255, 255, 255, 255);
    SDL_SetRenderDrawBlendMode(gSdlRenderer, SDL_BLENDMODE_NONE);
}

} // namespace fallout
