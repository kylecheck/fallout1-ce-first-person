#include "game/first_person_material.h"
#include <cassert>
#include <iostream>
using namespace fallout;
int main()
{
    // Three copies of the same vertical texture, with different source shear.
    const unsigned char solid[] = { 1,0,0, 2,1,0, 3,2,1, 0,3,2, 0,0,3 };
    auto face = first_person_rectify_columns(solid, 3, 5, false);
    assert(face.width == 3 && face.height == 3);
    const std::vector<unsigned char> expected { 1,1,1, 2,2,2, 3,3,3 };
    assert(face.pixels == expected);
    // An interior opening and a header-only column must stay transparent.
    const unsigned char opening[] = { 1,1,1, 2,0,0, 3,0,0, 4,4,0 };
    face = first_person_rectify_columns(opening, 3, 4, true);
    assert(face.pixels == std::vector<unsigned char>(opening, opening + 12));
    auto filled = first_person_rectify_columns(opening, 3, 4, false);
    for (auto pixel : filled.pixels) assert(pixel != 0);
    const unsigned char empty[] = { 0,0,0,0 };
    assert(first_person_rectify_columns(empty, 2, 2, true).pixels.empty());
    assert(first_person_rectify_columns(nullptr, 2, 2, true).pixels.empty());
    assert(first_person_material_has_opening("dv1035.frm"));
    assert(!first_person_material_has_opening("dv1000.frm"));
    {
        // Holes fill from the nearest opaque pixel in their column, shaded;
        // empty columns copy their nearest filled column; nothing stays 0.
        std::vector<unsigned char> face = {
            0, 0, 0, 0,
            5, 0, 0, 0,
            0, 0, 7, 0,
            6, 0, 0, 0,
        };
        first_person_fill_material_holes(face, 4, 4, [](unsigned char p) { return static_cast<unsigned char>(p + 100); });
        for (unsigned char p : face) assert(p != 0);
        assert(face[0] == 105 && face[4] == 5 && face[8] == 105 && face[12] == 6); // Column 0.
        assert(face[2] == 107 && face[10] == 7 && face[14] == 107);                // Column 2.
        assert(face[1] == face[0] + 100 && face[5] == 105);                        // Column 1 from column 0.
        assert(face[3] == face[2] + 100 || face[3] == face[2]);                    // Column 3 from column 2.
        std::vector<unsigned char> empty(9, 0);
        first_person_fill_material_holes(empty, 3, 3, [](unsigned char p) { return p; });
        for (unsigned char p : empty) assert(p == 0);
        // A shade that maps to 0 keeps the source colour instead.
        std::vector<unsigned char> dark = { 0, 9 };
        first_person_fill_material_holes(dark, 1, 2, [](unsigned char) { return static_cast<unsigned char>(0); });
        assert(dark[0] == 9);
    }
    // Doorways: an opening at the bottom, a quarter of the width, half the
    // height, away from the edges. Edge gaps and small holes are not.
    {
        const int w = 40;
        const int h = 40;
        std::vector<unsigned char> face(w * h, 7);
        for (int y = 12; y < h; y++) {
            for (int x = 14; x < 26; x++) face[y * w + x] = 0;
        }
        assert(first_person_material_has_doorway(face.data(), w, h));
        std::vector<unsigned char> edge(w * h, 7);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < 12; x++) edge[y * w + x] = 0;
        }
        assert(!first_person_material_has_doorway(edge.data(), w, h));
        std::vector<unsigned char> small(w * h, 7);
        for (int y = 30; y < h; y++) {
            for (int x = 14; x < 26; x++) small[y * w + x] = 0;
        }
        assert(!first_person_material_has_doorway(small.data(), w, h));
        std::vector<unsigned char> solid(w * h, 7);
        assert(!first_person_material_has_doorway(solid.data(), w, h));
    }
    std::cout << "Material shear and opening regression tests passed\n";
}
