#include "game/first_person_gpu.cc"
#include <cassert>
#include <iostream>
namespace fallout {
SDL_Renderer* gSdlRenderer = nullptr;
SDL_Surface* gSdlSurface = nullptr;
static bool overlayVisible = true;
bool first_person_overlay_visible() { return overlayVisible; }
int first_person_window() { return 5; }
int first_person_mode() { return GAME_MOUSE_MODE_MOVE; }
int screenGetWidth() { return 64; }
int screenGetHeight() { return 64; }
}
int main()
{
    using namespace fallout;
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface* output = SDL_CreateRGBSurfaceWithFormat(0,64,64,32,SDL_PIXELFORMAT_RGBA8888);
    assert(output != nullptr);
    gSdlRenderer = SDL_CreateSoftwareRenderer(output);
    gSdlSurface = SDL_CreateRGBSurfaceWithFormat(0,2,2,8,SDL_PIXELFORMAT_INDEX8);
    assert(gSdlRenderer != nullptr && gSdlSurface != nullptr);
    SDL_Color red {255,0,0,255};
    SDL_SetPaletteColors(gSdlSurface->format->palette,&red,1,1);
    const unsigned char pixels[4] {1,1,1,1};
    first_person_gpu_submit_indexed_sprite(123,pixels,2,2,0,0,2,2,0,0,8,8);
    auto present = [&](bool visible) {
        overlayVisible = visible;
        SDL_SetRenderDrawColor(gSdlRenderer,0,0,0,255);
        SDL_RenderClear(gSdlRenderer);
        first_person_gpu_present();
        Uint32 pixel = 0;
        SDL_Rect sample {0,0,1,1};
        assert(SDL_RenderReadPixels(gSdlRenderer,&sample,SDL_PIXELFORMAT_RGBA8888,&pixel,4) == 0);
        Uint8 r,g,b,a;
        SDL_GetRGBA(pixel,output->format,&r,&g,&b,&a);
        assert(r == (visible ? 255 : 0) && g == 0 && b == 0);
    };
    present(true);
    present(false); // Native UI must not receive a stale queued weapon sprite.
    present(true); // Returning to the world restores presentation.
    for (auto& texture : gFirstPersonGpuTextures) SDL_DestroyTexture(texture.texture);
    gFirstPersonGpuTextures.clear();
    SDL_DestroyRenderer(gSdlRenderer);
    SDL_FreeSurface(gSdlSurface); SDL_FreeSurface(output); SDL_Quit();
    std::cout << "PASS actual SDL GPU sprite presentation across native UI ownership\n";
}
