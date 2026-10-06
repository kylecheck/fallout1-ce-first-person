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
    std::cout << "Material shear and opening regression tests passed\n";
}
