#ifndef FALLOUT_GAME_FIRST_PERSON_GPU_H_
#define FALLOUT_GAME_FIRST_PERSON_GPU_H_

namespace fallout {

// GPU composition pass executed after Fallout's software framebuffer has been
// uploaded to SDL, but before SDL_RenderPresent. This is the migration point
// for first-person rendering that should no longer live in the CPU rasterizer.
void first_person_gpu_begin_frame();
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
    int destinationHeight,
    bool flipHorizontal = false);
void first_person_gpu_present();

} // namespace fallout

#endif /* FALLOUT_GAME_FIRST_PERSON_GPU_H_ */
