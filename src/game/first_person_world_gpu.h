#ifndef FALLOUT_GAME_FIRST_PERSON_WORLD_GPU_H_
#define FALLOUT_GAME_FIRST_PERSON_WORLD_GPU_H_
#include <cstdint>
namespace fallout {
// Camera-space vertices use screen-pixel numerators. The shader divides by z,
// preserving perspective through near-plane clipping, including floor tiles.
struct FirstPersonGpuVertex { float x, y, z, u, v; };
bool first_person_world_gpu_begin(int width, int height, int horizon,
    unsigned char sky, unsigned char ground);
// textureKey, when non-zero, stands for the pixels' content (the same key must
// always mean the same pixels) and skips hashing them to find the texture.
void first_person_world_gpu_quad(const unsigned char* pixels, int width, int height,
    const FirstPersonGpuVertex* vertices, std::uint32_t owner, bool proxy = false, float farLimit = 128.0f,
    std::uint64_t textureKey = 0);
bool first_person_world_gpu_read(unsigned char* indexed, float* depth,
    std::uint32_t* owners, std::uint32_t* proxies);
// Partial readback: finish draws the proxies and reads the full indexed colour
// (GL row order, as first_person_world_gpu_read). read_region then reads any of
// depth, owners and proxies for a top-down rectangle into top-down rows; null
// outputs are skipped. end restores the previous GL context. On failure
// finish has already restored it and the backend is disabled.
bool first_person_world_gpu_finish(unsigned char* indexed);
bool first_person_world_gpu_read_region(int x, int y, int width, int height,
    float* depth, std::uint32_t* owners, std::uint32_t* proxies);
void first_person_world_gpu_end();
void first_person_world_gpu_shutdown();
}
#endif
