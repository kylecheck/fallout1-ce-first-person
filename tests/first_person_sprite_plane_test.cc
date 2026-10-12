#include "game/first_person_sprite_plane.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using namespace fallout;

static bool near(double a, double b, double epsilon = 1.0e-6) { return std::abs(a - b) <= epsilon; }

// World offset to isometric pixels, the forward map of the renderer's inverse.
static void worldToIso(double dx, double dy, double* isoX, double* isoY)
{
    *isoX = kFirstPersonIsoXFromWorldX * dx + kFirstPersonIsoXFromWorldY * dy;
    *isoY = kFirstPersonIsoYFromWorldX * dx + kFirstPersonIsoYFromWorldY * dy;
}

// Draws a band along a slope: every column from x0 to x1 is opaque from the
// line top + slope * x down to thickness rows below it.
static std::vector<unsigned char> band(int width, int height, double slope, double top, int thickness)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height, 0);
    for (int x = 0; x < width; x++) {
        const int start = static_cast<int>(std::lround(top + slope * x));
        for (int y = start; y < start + thickness; y++) {
            if (y >= 0 && y < height) pixels[y * width + x] = 7;
        }
    }
    return pixels;
}

int main()
{
    // The inverse isometric map undoes the forward one.
    {
        double isoX;
        double isoY;
        worldToIso(1.25, -0.5, &isoX, &isoY);
        double x;
        double y;
        first_person_sprite_iso_to_world(isoX, isoY, &x, &y);
        assert(near(x, 1.25) && near(y, -0.5));
        // One hex row down is 16 px right and 12 px down; one column left
        // (an even column) is 32 px left.
        first_person_sprite_iso_to_world(16.0, 12.0, &x, &y);
        assert(near(x, 0.0) && near(y, 1.0));
        first_person_sprite_iso_to_world(-32.0, 0.0, &x, &y);
        assert(near(x, -0.8660254037844386) && near(y, -0.5));
    }

    // A floor decal puts each frame corner where native drawing shows it.
    {
        const FirstPersonSpriteQuad quad = first_person_sprite_floor_quad(10.0, 20.0, 40, 21, 3, -2, 0.01);
        for (int corner = 0; corner < 4; corner++) {
            double isoX;
            double isoY;
            worldToIso(quad.x[corner] - 10.0, quad.y[corner] - 20.0, &isoX, &isoY);
            double expectX;
            double expectY;
            first_person_sprite_corner_iso(corner, 40, 21, 3, -2, &expectX, &expectY);
            assert(near(isoX, expectX, 1.0e-6) && near(isoY, expectY, 1.0e-6));
            assert(near(quad.h[corner], 0.01));
        }
        // Bottom centre lands on the hex centre plus offsets, top left is
        // up and to the left of it.
        double isoX;
        double isoY;
        first_person_sprite_corner_iso(0, 40, 21, 3, -2, &isoX, &isoY);
        assert(near(isoX, 3 - 20) && near(isoY, -2 - 20));
        first_person_sprite_corner_iso(2, 40, 21, 3, -2, &isoX, &isoY);
        assert(near(isoX, 3 + 20) && near(isoY, -2 + 1));
    }

    // Wall bands are recognised by their slope; other shapes are not.
    {
        const double ns = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH);
        const double ew = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_EAST_WEST);
        assert(near(ns, 0.75) && near(ew, -0.25));
        std::vector<unsigned char> window = band(32, 91, ns, 2.0, 60);
        assert(first_person_sprite_wall_axis(window.data(), 32, 91) == FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH);
        std::vector<unsigned char> vent = band(56, 43, ew, 16.0, 20);
        assert(first_person_sprite_wall_axis(vent.data(), 56, 43) == FIRST_PERSON_SPRITE_AXIS_EAST_WEST);
        // A ragged end column (a lone pixel at the corner) does not hide it.
        std::vector<unsigned char> panel = band(60, 91, ew, 16.0, 74);
        for (int y = 0; y < 91; y++) panel[y * 60] = 0;
        panel[90 * 60] = 9;
        assert(first_person_sprite_wall_axis(panel.data(), 60, 91) == FIRST_PERSON_SPRITE_AXIS_EAST_WEST);
        // A diamond (a footprint seen from above) is not a wall band.
        std::vector<unsigned char> diamond(60 * 30, 0);
        for (int x = 0; x < 60; x++) {
            const int half = x < 30 ? x / 2 : (59 - x) / 2;
            for (int y = 15 - half; y <= 15 + half; y++) diamond[y * 60 + x] = 3;
        }
        assert(first_person_sprite_wall_axis(diamond.data(), 60, 30) == FIRST_PERSON_SPRITE_AXIS_NONE);
        // Two legs with a gap between them are not one band.
        std::vector<unsigned char> legs(20 * 30, 0);
        for (int y = 0; y < 30; y++) {
            for (int x : { 0, 1, 2, 3, 16, 17, 18, 19 }) legs[y * 20 + x] = 5;
        }
        assert(first_person_sprite_wall_axis(legs.data(), 20, 30) == FIRST_PERSON_SPRITE_AXIS_NONE);
        // Too narrow to judge.
        std::vector<unsigned char> thin = band(4, 8, ns, 0.0, 4);
        assert(first_person_sprite_wall_axis(thin.data(), 4, 8) == FIRST_PERSON_SPRITE_AXIS_NONE);
        assert(first_person_sprite_wall_axis(nullptr, 4, 8) == FIRST_PERSON_SPRITE_AXIS_NONE);
    }

    // Top-edge axis: judged on the top alone, so a notched bottom is fine.
    {
        const double ew = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_EAST_WEST);
        const double ns = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH);
        std::vector<unsigned char> door = band(52, 79, ew, 14.0, 60);
        for (int y = 60; y < 79; y++) {
            for (int x = 0; x < 8; x++) door[y * 52 + x] = 0;
        }
        assert(first_person_sprite_top_axis(door.data(), 52, 79) == FIRST_PERSON_SPRITE_AXIS_EAST_WEST);
        std::vector<unsigned char> side = band(30, 84, ns, 0.0, 60);
        assert(first_person_sprite_top_axis(side.data(), 30, 84) == FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH);
        std::vector<unsigned char> dome(40 * 40, 0);
        for (int x = 0; x < 40; x++) {
            const int top = static_cast<int>(std::abs(x - 20) * std::abs(x - 20) / 12);
            for (int y = top; y < 40; y++) dome[y * 40 + x] = 2;
        }
        assert(first_person_sprite_top_axis(dome.data(), 40, 40) == FIRST_PERSON_SPRITE_AXIS_NONE);
    }

    // A wall quad: a pixel on the wall line's isometric image at floor level
    // gets height 0; a pixel 60 px above it is one unit up; corners stay on
    // the wall line.
    {
        // A North/South wall line at x = hexX + 0.3 (y free).
        const double hexX = -50.0;
        const double hexY = 30.0;
        const FirstPersonSpriteQuad quad = first_person_sprite_wall_quad(hexX, hexY,
            FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH, hexX + 0.3, hexY, 32, 91, 0, 0);
        for (int corner = 0; corner < 4; corner++) {
            assert(near(quad.x[corner], hexX + 0.3));
            // Projecting the corner back gives the frame corner.
            double isoX;
            double isoY;
            worldToIso(quad.x[corner] - hexX, quad.y[corner] - hexY, &isoX, &isoY);
            isoY -= quad.h[corner] * kFirstPersonWallArtPixelsPerHeight;
            double expectX;
            double expectY;
            first_person_sprite_corner_iso(corner, 32, 91, 0, 0, &expectX, &expectY);
            assert(near(isoX, expectX, 1.0e-6) && near(isoY, expectY, 1.0e-6));
        }
        // Top corners are above bottom corners by the frame height.
        assert(near(quad.h[0] - quad.h[3], 91.0 / kFirstPersonWallArtPixelsPerHeight));
        // East/West: corners keep y on the line.
        const FirstPersonSpriteQuad ewQuad = first_person_sprite_wall_quad(hexX, hexY,
            FIRST_PERSON_SPRITE_AXIS_EAST_WEST, hexX, hexY - 0.2, 56, 43, 0, 0);
        for (int corner = 0; corner < 4; corner++) assert(near(ewQuad.y[corner], hexY - 0.2));
        // It spans the art's width along the wall at 27.7 px per unit.
        assert(near(ewQuad.x[1] - ewQuad.x[0], 56.0 / kFirstPersonIsoXFromWorldX));
    }

    // A leaning quad faces the viewer, leans away by the isometric angle and
    // keeps the visible bottom on the floor, the middle at the anchor.
    {
        const FirstPersonSpriteQuad quad = first_person_sprite_leaning_quad(0.0, 5.0, 0.0, 0.0,
            1.0, 1.0, 0.0, kFirstPersonIsoLean, 1.0);
        // Right is toward -x when looking along +y (right = (-forwardY, forwardX)).
        assert(near(quad.x[1] - quad.x[0], -1.0));
        assert(near(quad.h[3], 0.0) && near(quad.h[2], 0.0));
        assert(near(quad.h[0], std::cos(kFirstPersonIsoLean)));
        // Top is farther than bottom, the middle is at the anchor.
        assert(quad.y[0] > quad.y[3]);
        assert(near((quad.y[0] + quad.y[3]) * 0.5, 5.0));
        assert(near(quad.y[0] - quad.y[3], std::sin(kFirstPersonIsoLean)));
        // The shift is capped.
        const FirstPersonSpriteQuad capped = first_person_sprite_leaning_quad(0.0, 5.0, 0.0, 0.0,
            1.0, 2.0, 0.0, kFirstPersonIsoLean, 0.2);
        assert(near(capped.y[0] - 5.0, 0.2) && near(5.0 - capped.y[3], 0.2));
        // Empty rows under the art hang below the floor.
        const FirstPersonSpriteQuad dropped = first_person_sprite_leaning_quad(0.0, 5.0, 0.0, 0.0,
            1.0, 1.0, 0.25, 0.0, 1.0);
        assert(near(dropped.h[3], -0.25) && near(dropped.h[0], 0.75));
    }

    // Pushing a quad off a wall: a North/South wall along x = 1 from y = 0
    // to 2, the object on the x < 1 side.
    {
        FirstPersonSpriteQuad quad {};
        const double xs[4] = { 0.5, 1.2, 1.2, 0.5 };
        for (int i = 0; i < 4; i++) {
            quad.x[i] = xs[i];
            quad.y[i] = 1.0;
        }
        const double moved = first_person_sprite_push_off_wall(quad, 1.0, 0.0, 1.0, 2.0, 0.0, 1.0, 0.05, 0.3);
        assert(near(moved, 0.25));
        assert(near(quad.x[1], 0.95) && near(quad.x[0], 0.25));
        // Already clear: nothing moves.
        assert(near(first_person_sprite_push_off_wall(quad, 1.0, 0.0, 1.0, 2.0, 0.0, 1.0, 0.05, 0.3), 0.0));
        // Beside the wall's end, out of reach: nothing moves.
        FirstPersonSpriteQuad past {};
        for (int i = 0; i < 4; i++) {
            past.x[i] = xs[i];
            past.y[i] = 3.0;
        }
        assert(near(first_person_sprite_push_off_wall(past, 1.0, 0.0, 1.0, 2.0, 0.0, 1.0, 0.05, 0.3), 0.0));
        // The side comes from the object, whichever way the segment runs.
        FirstPersonSpriteQuad other {};
        const double otherXs[4] = { 1.5, 0.8, 0.8, 1.5 };
        for (int i = 0; i < 4; i++) {
            other.x[i] = otherXs[i];
            other.y[i] = 1.0;
        }
        assert(near(first_person_sprite_push_off_wall(other, 1.0, 2.0, 1.0, 0.0, 2.0, 1.0, 0.05, 0.3), 0.25));
        assert(near(other.x[1], 1.05));
    }

    // Ray hits: a quad 4 units ahead spanning x -1..1, height -1..1 around
    // the eye's line, camera space (x right, y up, z forward).
    {
        const double c0[3] = { -1.0, 1.0, 4.0 };
        const double e1[3] = { 2.0, 0.0, 0.0 };
        const double e2[3] = { 0.0, -2.0, 0.0 };
        double t;
        double u;
        double v;
        assert(first_person_sprite_ray_hit(0.0, 0.0, c0, e1, e2, &t, &u, &v));
        assert(near(t, 4.0) && near(u, 0.5) && near(v, 0.5));
        // Toward the top-left quarter.
        assert(first_person_sprite_ray_hit(-0.125, 0.125, c0, e1, e2, &t, &u, &v));
        assert(near(u, 0.25) && near(v, 0.25) && near(t, 4.0));
        // Outside.
        assert(!first_person_sprite_ray_hit(0.3, 0.0, c0, e1, e2, &t, &u, &v));
        // Behind the eye.
        const double behind[3] = { -1.0, 1.0, -4.0 };
        assert(!first_person_sprite_ray_hit(0.0, 0.0, behind, e1, e2, &t, &u, &v));
        // A floor plane 0.74 below the eye, 2..6 ahead.
        const double f0[3] = { -1.0, -0.74, 6.0 };
        const double f1[3] = { 2.0, 0.0, 0.0 };
        const double f2[3] = { 0.0, 0.0, -4.0 };
        assert(first_person_sprite_ray_hit(0.0, -0.74 / 3.0, f0, f1, f2, &t, &u, &v));
        assert(near(t, 3.0) && near(v, 0.75));
    }

    // Box outlines: a solid box, a table on corner legs, and shapes that are
    // not boxes.
    {
        FirstPersonSpriteBoxFit truth;
        truth.leftX = 0.0;
        truth.frontX = 40.0;
        truth.rightX = 100.0;
        truth.frontY = 70.0;
        const int width = 100;
        const int height = 72;
        auto silhouette = [&](double rise, int legRows) {
            std::vector<unsigned char> pixels(static_cast<size_t>(width) * height, 0);
            for (int x = 0; x < width; x++) {
                const double c = x + 0.5;
                const double front = first_person_sprite_box_front_y(truth, c);
                const double back = first_person_sprite_box_back_y(truth, c) - rise;
                const bool leg = legRows > 0 && (x < 3 || std::abs(c - truth.frontX) < 2.0 || x >= width - 3);
                const double bottom = legRows > 0 && !leg ? front - legRows : front;
                for (int y = 0; y < height; y++) {
                    if (y + 0.5 >= back && y + 0.5 < bottom) pixels[y * width + x] = 4;
                }
            }
            return pixels;
        };
        std::vector<unsigned char> solid = silhouette(30.0, 0);
        FirstPersonSpriteBoxFit fit = first_person_sprite_box_fit(solid.data(), width, height);
        assert(fit.ok);
        assert(near(fit.leftX, 0.0) && near(fit.rightX, 100.0));
        assert(std::abs(fit.frontX - 40.0) <= 1.0);
        assert(std::abs(fit.frontY - 70.0) <= 1.0);
        assert(std::abs(fit.height - 30.0) <= 1.5);
        assert(std::abs(fit.maxHeight - 30.0) <= 1.5);
        // A table: the top and its front edge, legs only at the corners.
        std::vector<unsigned char> table = silhouette(30.0, 18);
        fit = first_person_sprite_box_fit(table.data(), width, height);
        assert(fit.ok);
        assert(std::abs(fit.frontX - 40.0) <= 1.5 && std::abs(fit.frontY - 70.0) <= 1.0);
        // A headboard rising above part of the top reaches maxHeight.
        std::vector<unsigned char> bed = silhouette(20.0, 0);
        for (int x = 70; x < 100; x++) {
            const double back = first_person_sprite_box_back_y(truth, x + 0.5);
            for (int y = 0; y < height; y++) {
                if (y + 0.5 >= back - 40.0 && y + 0.5 < back) bed[y * width + x] = 4;
            }
        }
        fit = first_person_sprite_box_fit(bed.data(), width, height);
        assert(fit.ok && std::abs(fit.height - 20.0) <= 1.5 && std::abs(fit.maxHeight - 40.0) <= 1.5);
        // Flat footprints: the box outline with almost no rise.
        std::vector<unsigned char> rug = silhouette(3.0, 0);
        assert(first_person_sprite_flat_footprint(rug.data(), width, height));
        assert(!first_person_sprite_flat_footprint(solid.data(), width, height));
        // A blob that stands on one point is not a box.
        std::vector<unsigned char> blob(static_cast<size_t>(width) * height, 0);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                const double dx = (x - 50.0) / 50.0;
                const double dy = (y - 36.0) / 36.0;
                if (dx * dx + dy * dy < 1.0) blob[y * width + x] = 2;
            }
        }
        assert(!first_person_sprite_box_fit(blob.data(), width, height).ok);
        // Separate parts are not a box.
        std::vector<unsigned char> parts = solid;
        for (int y = 0; y < height; y++) parts[y * width + 50] = 0;
        assert(!first_person_sprite_box_fit(parts.data(), width, height).ok);
        assert(!first_person_sprite_box_fit(nullptr, width, height).ok);

        // Faces: front faces stand on the V, the top is level at the height,
        // and each face corner shows the frame pixel it projects to.
        fit = first_person_sprite_box_fit(solid.data(), width, height);
        const std::vector<FirstPersonSpriteFace> faces = first_person_sprite_box_faces(fit, 10.0, 20.0,
            width, height, 0, 0);
        assert(faces.size() == 3);
        const double topHeight = fit.height / kFirstPersonWallArtPixelsPerHeight;
        for (const FirstPersonSpriteFace& face : faces) {
            for (int i = 0; i < 4; i++) {
                assert(near(face.quad.h[i], 0.0) || near(face.quad.h[i], topHeight));
                // Back through the forward projection to the frame.
                double isoX;
                double isoY;
                worldToIso(face.quad.x[i] - 10.0, face.quad.y[i] - 20.0, &isoX, &isoY);
                isoY -= face.quad.h[i] * kFirstPersonWallArtPixelsPerHeight;
                assert(near(face.u[i], isoX - (0 - width / 2), 1.0e-6));
                assert(near(face.v[i], isoY - (0 - (height - 1)), 1.0e-6));
            }
        }
        // Front-left face bottom corners are the left and front corners.
        assert(near(faces[0].u[3], fit.leftX, 1.0e-6) && near(faces[0].v[3], first_person_sprite_box_front_y(fit, fit.leftX), 1.0e-6));
        assert(near(faces[0].u[2], fit.frontX, 1.0e-6) && near(faces[0].v[2], fit.frontY, 1.0e-6));
        // The top's back corner shows the outline's top at the back corner.
        const double backX = fit.leftX + fit.rightX - fit.frontX;
        assert(near(faces[2].u[1], backX, 1.0e-6));
        assert(near(faces[2].v[1], first_person_sprite_box_back_y(fit, backX) - fit.height, 1.0e-6));
        // The front-left face runs along world y (North/South), the front-right
        // along world x (East/West).
        assert(near(faces[0].quad.x[0], faces[0].quad.x[1], 1.0e-6));
        assert(near(faces[1].quad.y[0], faces[1].quad.y[1], 1.0e-6));
        // With a headboard, two fins rise from the top to maxHeight.
        fit = first_person_sprite_box_fit(bed.data(), width, height);
        assert(first_person_sprite_box_faces(fit, 0.0, 0.0, width, height, 0, 0).size() == 5);
    }

    std::cout << "first_person_sprite_plane_test: ok\n";
    return 0;
}
