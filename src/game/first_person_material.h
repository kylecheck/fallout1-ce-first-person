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

// A wall face that backs onto solid rock or void must not be see-through.
// Fills each transparent (0) pixel from the nearest opaque pixel in its
// column, passed through shade (e.g. the native intensity table), then fills
// fully transparent columns from the nearest filled column. A face with no
// opaque pixel is left unchanged.
template <typename Shade>
void first_person_fill_material_holes(std::vector<unsigned char>& pixels, int width, int height, Shade shade)
{
    if (width <= 0 || height <= 0 || static_cast<int>(pixels.size()) < width * height) return;
    std::vector<unsigned char> source = pixels;
    std::vector<unsigned char> filledColumn(width, 0);
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            if (source[y * width + x] != 0) continue;
            int best = -1;
            for (int d = 1; d < height && best < 0; d++) {
                if (y - d >= 0 && source[(y - d) * width + x] != 0) best = y - d;
                else if (y + d < height && source[(y + d) * width + x] != 0) best = y + d;
            }
            if (best < 0) break; // The whole column is transparent.
            const unsigned char shaded = shade(source[best * width + x]);
            pixels[y * width + x] = shaded != 0 ? shaded : source[best * width + x];
        }
        bool opaque = true;
        for (int y = 0; y < height && opaque; y++) opaque = pixels[y * width + x] != 0;
        filledColumn[x] = opaque ? 1 : 0;
    }
    for (int x = 0; x < width; x++) {
        if (filledColumn[x]) continue;
        int from = -1;
        for (int d = 1; d < width && from < 0; d++) {
            if (x - d >= 0 && filledColumn[x - d]) from = x - d;
            else if (x + d < width && filledColumn[x + d]) from = x + d;
        }
        if (from < 0) return; // No opaque pixel anywhere.
        for (int y = 0; y < height; y++) {
            if (pixels[y * width + x] == 0) {
                const unsigned char shaded = shade(pixels[y * width + from]);
                pixels[y * width + x] = shaded != 0 ? shaded : pixels[y * width + from];
            }
        }
    }
}

} // namespace fallout
#endif
