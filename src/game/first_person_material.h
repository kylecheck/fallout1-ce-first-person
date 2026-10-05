#ifndef FALLOUT_GAME_FIRST_PERSON_MATERIAL_H_
#define FALLOUT_GAME_FIRST_PERSON_MATERIAL_H_

#include <algorithm>
#include <cstring>
#include <vector>

namespace fallout {

inline bool first_person_material_has_opening(const char* name)
{
    const char* names[] = { "dv1010.frm", "dv1011.frm", "dv1012.frm", "dv1013.frm",
        "dv1035.frm", "dv1036.frm", "dv1043.frm", "dv1044.frm",
        "velvdr03.frm", "velvdr04.frm" };
    for (const char* candidate : names) {
        if (std::strcmp(name, candidate) == 0) {
            return true;
        }
    }
    return false;
}

struct FirstPersonRectifiedMaterial {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

// Remove the vertical isometric shear from a single wall face. Keep source
// columns fixed; shifting whole rows destroys the shape of posts and windows.
inline FirstPersonRectifiedMaterial first_person_rectify_columns(
    const unsigned char* source, int width, int height, bool preserveAlpha)
{
    FirstPersonRectifiedMaterial result;
    if (source == nullptr || width <= 0 || height <= 0) {
        return result;
    }
    std::vector<int> tops(width, height), bottoms(width, -1);
    int left = width, right = -1, faceHeight = 0;
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            if (source[y * width + x] != 0) {
                tops[x] = std::min(tops[x], y);
                bottoms[x] = y;
            }
        }
        if (bottoms[x] >= tops[x]) {
            left = std::min(left, x);
            right = x;
            faceHeight = std::max(faceHeight, bottoms[x] - tops[x] + 1);
        }
    }
    if (right < left) {
        return result;
    }
    result.width = right - left + 1;
    result.height = faceHeight;
    result.pixels.resize(static_cast<size_t>(result.width) * result.height, 0);
    for (int x = left; x <= right; x++) {
        if (bottoms[x] < tops[x]) {
            continue;
        }
        for (int y = 0; y < faceHeight; y++) {
            int sy = tops[x] + y;
            if (preserveAlpha && sy > bottoms[x]) {
                continue;
            }
            sy = std::min(sy, bottoms[x]);
            unsigned char pixel = source[sy * width + x];
            if (!preserveAlpha && pixel == 0) {
                // Solid faces repair only within their own source column.
                for (int radius = 1; radius <= bottoms[x] - tops[x] && pixel == 0; radius++) {
                    if (sy - radius >= tops[x]) {
                        pixel = source[(sy - radius) * width + x];
                    }
                    if (pixel == 0 && sy + radius <= bottoms[x]) {
                        pixel = source[(sy + radius) * width + x];
                    }
                }
            }
            result.pixels[y * result.width + x - left] = pixel;
        }
    }
    return result;
}

} // namespace fallout
#endif
