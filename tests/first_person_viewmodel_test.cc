#include "game/first_person_viewmodel.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using namespace fallout;

static bool near(double a, double b, double epsilon = 1.0e-6) { return std::abs(a - b) <= epsilon; }

// A gun in profile: a barrel along the top, a grip hanging from the stock end.
static std::vector<unsigned char> gun(int width, int height, bool stockLeft)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height, 0);
    for (int y = 4; y < 12; y++) {
        for (int x = 0; x < width; x++) pixels[y * width + x] = 9;
    }
    for (int y = 12; y < height; y++) {
        for (int x = 0; x < 12; x++) pixels[y * width + (stockLeft ? x : width - 1 - x)] = 9;
    }
    return pixels;
}

int main()
{
    // Thickness: zero outside, thin at the outline, full inside.
    {
        const int width = 20;
        const int height = 20;
        std::vector<unsigned char> square(static_cast<size_t>(width) * height, 0);
        for (int y = 2; y < 18; y++) {
            for (int x = 2; x < 18; x++) square[y * width + x] = 5;
        }
        const std::vector<float> half = first_person_viewmodel_thickness(square.data(), width, height, 4.0, 6.0);
        assert(half[0] == 0.0f);
        const float edge = half[2 * width + 10];
        const float inside = half[10 * width + 10];
        assert(edge > 0.0f && edge < inside);
        assert(near(inside, 4.0, 1.0e-4));
        // It never decreases toward the middle.
        for (int x = 2; x < 10; x++) assert(half[10 * width + x] <= half[10 * width + x + 1] + 1.0e-6f);
    }

    // Lead end: guns lead with the end away from the broad stock end;
    // melee leads left as the stock art is drawn; the table overrides.
    {
        std::vector<unsigned char> left = gun(80, 30, true);
        FirstPersonViewmodelAxis axis = first_person_viewmodel_axis(left.data(), 80, 30, FIRST_PERSON_VIEWMODEL_GUN);
        assert(axis.ok && axis.axisX > 0.9);
        std::vector<unsigned char> right = gun(80, 30, false);
        axis = first_person_viewmodel_axis(right.data(), 80, 30, FIRST_PERSON_VIEWMODEL_GUN);
        assert(axis.ok && axis.axisX < -0.9);
        axis = first_person_viewmodel_axis(left.data(), 80, 30, FIRST_PERSON_VIEWMODEL_GUN, -1);
        assert(axis.axisX < -0.9);
        axis = first_person_viewmodel_axis(left.data(), 80, 30, FIRST_PERSON_VIEWMODEL_MELEE);
        assert(axis.axisX < -0.9);
        axis = first_person_viewmodel_axis(left.data(), 80, 30, FIRST_PERSON_VIEWMODEL_ITEM);
        assert(near(axis.axisX, 1.0) && near(axis.axisY, 0.0));
        assert(first_person_viewmodel_art_lead("minigun.frm") == 1);
        assert(first_person_viewmodel_art_lead("MINIGUN.FRM") == 1);
        assert(first_person_viewmodel_art_lead("pistol.frm") == 0);
        assert(first_person_viewmodel_art_lead(nullptr) == 0);
    }

    // A long tilted bar keeps its tilt as the axis; a compact L stays level.
    {
        const int width = 100;
        const int height = 40;
        std::vector<unsigned char> bar(static_cast<size_t>(width) * height, 0);
        for (int x = 0; x < width; x++) {
            const int y0 = 5 + x * 3 / 10;
            for (int y = y0; y < y0 + 4 && y < height; y++) bar[y * width + x] = 3;
        }
        FirstPersonViewmodelAxis axis = first_person_viewmodel_axis(bar.data(), width, height, FIRST_PERSON_VIEWMODEL_MELEE, 1);
        assert(axis.axisY > 0.2 && axis.axisX > 0.9);
        std::vector<unsigned char> compact = gun(28, 36, true);
        axis = first_person_viewmodel_axis(compact.data(), 28, 36, FIRST_PERSON_VIEWMODEL_GUN);
        assert(near(std::abs(axis.axisX), 1.0) && near(axis.axisY, 0.0));
    }

    // Kinds and poses.
    {
        assert(first_person_viewmodel_kind(5) == FIRST_PERSON_VIEWMODEL_GUN);
        assert(first_person_viewmodel_kind(1) == FIRST_PERSON_VIEWMODEL_MELEE);
        assert(first_person_viewmodel_kind(-1) == FIRST_PERSON_VIEWMODEL_ITEM);
        assert(first_person_viewmodel_kind(0) == FIRST_PERSON_VIEWMODEL_FIST);
        const FirstPersonViewmodelPose ready = first_person_viewmodel_pose(5, FIRST_PERSON_VIEWMODEL_READY);
        const FirstPersonViewmodelPose lowered = first_person_viewmodel_pose(5, FIRST_PERSON_VIEWMODEL_LOWERED);
        const FirstPersonViewmodelPose attack = first_person_viewmodel_pose(5, FIRST_PERSON_VIEWMODEL_ATTACK);
        assert(ready.leadZ > 0.0 && ready.x > 0.0 && ready.y < 0.0);
        assert(lowered.y < ready.y && attack.z < ready.z);
        assert(first_person_viewmodel_pose(9, FIRST_PERSON_VIEWMODEL_READY).length > ready.length);
    }

    // Rendering: the gun lands right of and below the centre, keeps only
    // palette colours (shaded through the table) and covers its bounds.
    {
        std::vector<unsigned char> art = gun(80, 30, true);
        const FirstPersonViewmodelAxis axis = first_person_viewmodel_axis(art.data(), 80, 30, FIRST_PERSON_VIEWMODEL_GUN);
        const std::vector<float> half = first_person_viewmodel_thickness(art.data(), 80, 30, 3.0, 4.5);
        int shaded = 0;
        const FirstPersonViewmodelImage image = first_person_viewmodel_render(art.data(), 80, 30, axis, half,
            first_person_viewmodel_pose(5, FIRST_PERSON_VIEWMODEL_READY), 640, 400, 448.0,
            [&](unsigned char color, int intensity) {
                shaded++;
                assert(intensity >= 0 && intensity <= 255);
                return static_cast<unsigned char>(intensity > 120 ? color : color + 1);
            });
        assert(shaded > 0);
        assert(image.width > 0 && image.height > 0);
        assert(image.left + image.width / 2 > 320 && image.top + image.height / 2 > 200);
        assert(image.left >= 0 && image.top >= 0 && image.left + image.width <= 640 && image.top + image.height <= 400);
        int opaque = 0;
        for (unsigned char pixel : image.pixels) {
            assert(pixel == 0 || pixel == 9 || pixel == 10);
            opaque += pixel != 0;
        }
        assert(opaque > image.width * image.height / 10);
        // A colour shaded to index 0 keeps its own colour instead of vanishing.
        const FirstPersonViewmodelImage dark = first_person_viewmodel_render(art.data(), 80, 30, axis, half,
            first_person_viewmodel_pose(5, FIRST_PERSON_VIEWMODEL_READY), 640, 400, 448.0,
            [](unsigned char, int) { return static_cast<unsigned char>(0); });
        int darkOpaque = 0;
        for (unsigned char pixel : dark.pixels) darkOpaque += pixel != 0;
        assert(darkOpaque == opaque);
        // Nothing to draw: empty art, or a pose behind the eye.
        std::vector<unsigned char> empty(80 * 30, 0);
        assert(!first_person_viewmodel_axis(empty.data(), 80, 30, FIRST_PERSON_VIEWMODEL_GUN).ok);
        FirstPersonViewmodelPose behind = first_person_viewmodel_pose(5, FIRST_PERSON_VIEWMODEL_READY);
        behind.z = -2.0;
        const FirstPersonViewmodelImage none = first_person_viewmodel_render(art.data(), 80, 30, axis, half, behind,
            640, 400, 448.0, [](unsigned char color, int) { return color; });
        assert(none.width == 0 && none.pixels.empty());
    }

    std::cout << "first_person_viewmodel_test: ok\n";
    return 0;
}
