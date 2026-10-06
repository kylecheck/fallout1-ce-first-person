#ifndef FALLOUT_GAME_FIRST_PERSON_WORLD_GPU_H_
#define FALLOUT_GAME_FIRST_PERSON_WORLD_GPU_H_
#include <cstdint>
namespace fallout {
// Camera-space vertices use screen-pixel numerators. The shader divides by z,
// preserving perspective through near-plane clipping, including floor tiles.
struct FirstPersonGpuVertex { float x, y, z, u, v; };
bool first_person_world_gpu_begin(int width, int height, int horizon,
    unsigned char sky, unsigned char ground);
void first_person_world_gpu_quad(const unsigned char* pixels, int width, int height,
    const FirstPersonGpuVertex* vertices, std::uint32_t owner, bool proxy = false, float farLimit = 128.0f);
bool first_person_world_gpu_read(unsigned char* indexed, float* depth,
    std::uint32_t* owners, std::uint32_t* proxies);
void first_person_world_gpu_shutdown();
}
#endif
