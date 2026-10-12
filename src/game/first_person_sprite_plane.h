#ifndef FALLOUT_GAME_FIRST_PERSON_SPRITE_PLANE_H_
#define FALLOUT_GAME_FIRST_PERSON_SPRITE_PLANE_H_

#include <algorithm>
#include <cmath>
#include <vector>

namespace fallout {

// Where a scenery or item sprite is placed in the first-person world. The
// original isometric art is kept; only the surface it is drawn on changes.
enum FirstPersonSpritePlacement {
    // Camera-facing, as critters are drawn.
    FIRST_PERSON_SPRITE_UPRIGHT,
    // Turned toward the viewer and leaned back by the isometric camera angle
    // the art was drawn from, pivoting about its middle.
    FIRST_PERSON_SPRITE_LEANING,
    // Native flat objects (rugs, flat beds, bones): laid on the floor exactly
    // where the isometric view draws them.
    FIRST_PERSON_SPRITE_FLOOR,
    // Art painted along a wall (windows, vents, wall machinery): on that
    // wall's plane, unsheared as the wall art is.
    FIRST_PERSON_SPRITE_WALL,
};

// A sprite's surface: world corners for texture (0,0), (1,0), (1,1), (0,1),
// that is top-left, top-right, bottom-right, bottom-left of the frame. x and
// y are renderer world coordinates, h the height above the floor. The four
// corners always form a parallelogram, so the texture maps affinely.
struct FirstPersonSpriteQuad {
    double x[4];
    double y[4];
    double h[4];
};

// Native isometric screen pixels per world unit along each world axis
// (first_person_iso_to_world is the inverse), and the wall art's pixels per
// unit of height (wall faces are 95-103 px for the 1.65-unit structural walls).
constexpr double kFirstPersonIsoXFromWorldX = 27.712812921102035;
constexpr double kFirstPersonIsoXFromWorldY = 16.0;
constexpr double kFirstPersonIsoYFromWorldX = -6.928203230275509;
constexpr double kFirstPersonIsoYFromWorldY = 12.0;
constexpr double kFirstPersonWallArtPixelsPerHeight = 60.0;
// The native camera looks down by asin(13.86 / 32): ground depth is drawn
// at 0.433 of its width.
constexpr double kFirstPersonIsoLean = 0.44783277;

inline void first_person_sprite_iso_to_world(double isoX, double isoY, double* worldX, double* worldY)
{
    constexpr double kDeterminant = kFirstPersonIsoXFromWorldX * kFirstPersonIsoYFromWorldY
        - kFirstPersonIsoXFromWorldY * kFirstPersonIsoYFromWorldX;
    *worldX = (kFirstPersonIsoYFromWorldY * isoX - kFirstPersonIsoXFromWorldY * isoY) / kDeterminant;
    *worldY = (-kFirstPersonIsoYFromWorldX * isoX + kFirstPersonIsoXFromWorldX * isoY) / kDeterminant;
}

// The isometric pixel offset of a frame corner from its hex centre, as native
// object drawing places it: bottom centre at the hex centre plus the art's
// offsets and the object's shift. corner is 0..3 in FirstPersonSpriteQuad order.
inline void first_person_sprite_corner_iso(int corner, int width, int height, int offsetX, int offsetY,
    double* isoX, double* isoY)
{
    const double left = offsetX - width / 2;
    const double top = offsetY - (height - 1);
    *isoX = left + ((corner == 1 || corner == 2) ? width : 0);
    *isoY = top + ((corner >= 2) ? height : 0);
}

// A flat object's frame on the floor at the height lift.
inline FirstPersonSpriteQuad first_person_sprite_floor_quad(double hexX, double hexY,
    int width, int height, int offsetX, int offsetY, double lift)
{
    FirstPersonSpriteQuad quad {};
    for (int corner = 0; corner < 4; corner++) {
        double isoX;
        double isoY;
        first_person_sprite_corner_iso(corner, width, height, offsetX, offsetY, &isoX, &isoY);
        double dx;
        double dy;
        first_person_sprite_iso_to_world(isoX, isoY, &dx, &dy);
        quad.x[corner] = hexX + dx;
        quad.y[corner] = hexY + dy;
        quad.h[corner] = lift;
    }
    return quad;
}

// Wall axes: the structural lattice's North/South pieces run along world y,
// East/West pieces along world x.
enum FirstPersonSpriteWallAxis {
    FIRST_PERSON_SPRITE_AXIS_NONE,
    FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH,
    FIRST_PERSON_SPRITE_AXIS_EAST_WEST,
};

// The isometric slope (pixels down per pixel right) of a wall axis.
inline double first_person_sprite_axis_slope(FirstPersonSpriteWallAxis axis)
{
    return axis == FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH
        ? kFirstPersonIsoYFromWorldY / kFirstPersonIsoXFromWorldY
        : kFirstPersonIsoYFromWorldX / kFirstPersonIsoXFromWorldX;
}

// Whether a frame's opaque shape is a band along one wall axis: the top and
// the bottom of its opaque columns both follow that axis's slope (as wall art
// does), with little deviation. Free-standing furniture fails on its outline.
inline FirstPersonSpriteWallAxis first_person_sprite_wall_axis(const unsigned char* pixels, int width, int height)
{
    if (pixels == nullptr || width <= 0 || height <= 0) return FIRST_PERSON_SPRITE_AXIS_NONE;
    std::vector<double> xs;
    std::vector<double> tops;
    std::vector<double> bottoms;
    for (int x = 0; x < width; x++) {
        int top = -1;
        int bottom = -1;
        for (int y = 0; y < height; y++) {
            if (pixels[y * width + x] != 0) {
                if (top < 0) top = y;
                bottom = y;
            }
        }
        if (top >= 0) {
            xs.push_back(x);
            tops.push_back(top);
            bottoms.push_back(bottom);
        }
    }
    constexpr size_t kMinColumns = 6;
    if (xs.size() < kMinColumns) return FIRST_PERSON_SPRITE_AXIS_NONE;
    // Opaque columns must be contiguous: gaps mean separate parts (legs).
    if (xs.back() - xs.front() + 1 != static_cast<double>(xs.size())) return FIRST_PERSON_SPRITE_AXIS_NONE;
    auto fits = [&](const std::vector<double>& ys, double slope) {
        // Offset for the fixed slope (the median, so a ragged end column does
        // not move it), then the RMS deviation of the best 90% of columns.
        std::vector<double> offsets(xs.size());
        for (size_t i = 0; i < xs.size(); i++) offsets[i] = ys[i] - slope * xs[i];
        std::vector<double> sorted = offsets;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const double offset = sorted[sorted.size() / 2];
        std::vector<double> squares(xs.size());
        for (size_t i = 0; i < xs.size(); i++) squares[i] = (offsets[i] - offset) * (offsets[i] - offset);
        std::sort(squares.begin(), squares.end());
        const size_t kept = xs.size() - xs.size() / 10;
        double sum = 0.0;
        for (size_t i = 0; i < kept; i++) sum += squares[i];
        return std::sqrt(sum / static_cast<double>(kept)) <= 1.5;
    };
    for (FirstPersonSpriteWallAxis axis : { FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH, FIRST_PERSON_SPRITE_AXIS_EAST_WEST }) {
        const double slope = first_person_sprite_axis_slope(axis);
        if (fits(tops, slope) && fits(bottoms, slope)) return axis;
    }
    return FIRST_PERSON_SPRITE_AXIS_NONE;
}

// A wall-painted frame on the vertical plane along axis through (lineX, lineY).
// Columns keep their place along the wall and each pixel's height comes from
// the isometric rise between the wall line and the pixel, as for wall art.
inline FirstPersonSpriteQuad first_person_sprite_wall_quad(double hexX, double hexY,
    FirstPersonSpriteWallAxis axis, double lineX, double lineY,
    int width, int height, int offsetX, int offsetY)
{
    const double axisX = axis == FIRST_PERSON_SPRITE_AXIS_EAST_WEST ? 1.0 : 0.0;
    const double axisY = axis == FIRST_PERSON_SPRITE_AXIS_EAST_WEST ? 0.0 : 1.0;
    const double isoAlongX = kFirstPersonIsoXFromWorldX * axisX + kFirstPersonIsoXFromWorldY * axisY;
    const double isoAlongY = kFirstPersonIsoYFromWorldX * axisX + kFirstPersonIsoYFromWorldY * axisY;
    // The line point's isometric offset from the hex centre.
    const double lineDX = lineX - hexX;
    const double lineDY = lineY - hexY;
    const double lineIsoX = kFirstPersonIsoXFromWorldX * lineDX + kFirstPersonIsoXFromWorldY * lineDY;
    const double lineIsoY = kFirstPersonIsoYFromWorldX * lineDX + kFirstPersonIsoYFromWorldY * lineDY;
    FirstPersonSpriteQuad quad {};
    for (int corner = 0; corner < 4; corner++) {
        double isoX;
        double isoY;
        first_person_sprite_corner_iso(corner, width, height, offsetX, offsetY, &isoX, &isoY);
        const double along = (isoX - lineIsoX) / isoAlongX;
        const double rise = lineIsoY + along * isoAlongY - isoY;
        quad.x[corner] = lineX + along * axisX;
        quad.y[corner] = lineY + along * axisY;
        quad.h[corner] = rise / kFirstPersonWallArtPixelsPerHeight;
    }
    return quad;
}

// A free-standing frame of worldWidth x worldHeight turned toward the viewer
// and leaned back by the isometric angle, pivoting about the middle of its
// visible height so the front of the object comes forward and the back goes
// away, each by at most maxShift. dropRows/height of the frame (empty rows
// under the art) hangs below the floor so the lowest visible pixel rests on it.
inline FirstPersonSpriteQuad first_person_sprite_leaning_quad(double anchorX, double anchorY,
    double viewerX, double viewerY, double worldWidth, double worldHeight, double dropFraction,
    double lean, double maxShift)
{
    double forwardX = anchorX - viewerX;
    double forwardY = anchorY - viewerY;
    const double length = std::sqrt(forwardX * forwardX + forwardY * forwardY);
    if (length > 1.0e-9) {
        forwardX /= length;
        forwardY /= length;
    } else {
        forwardX = 1.0;
        forwardY = 0.0;
    }
    // Screen-right as seen by the viewer (matches the camera's right vector).
    const double rightX = -forwardY;
    const double rightY = forwardX;
    const double below = worldHeight * dropFraction;
    const double visible = worldHeight - below;
    double sine = std::sin(lean);
    if (visible * 0.5 * sine > maxShift && visible > 0.0) sine = maxShift / (visible * 0.5);
    const double cosine = std::sqrt(std::max(0.0, 1.0 - sine * sine));
    FirstPersonSpriteQuad quad {};
    // s runs along the leaning card from the floor contact (s = 0) upward.
    const double sTop = visible;
    const double sBottom = -below;
    for (int corner = 0; corner < 4; corner++) {
        const double s = corner < 2 ? sTop : sBottom;
        const double side = (corner == 1 || corner == 2) ? 0.5 : -0.5;
        const double back = (s - visible * 0.5) * sine;
        quad.x[corner] = anchorX + rightX * worldWidth * side + forwardX * back;
        quad.y[corner] = anchorY + rightY * worldWidth * side + forwardY * back;
        quad.h[corner] = s * cosine;
    }
    return quad;
}

// The point at texture (u, v) on a quad.
inline void first_person_sprite_quad_point(const FirstPersonSpriteQuad& quad, double u, double v,
    double* x, double* y, double* h)
{
    *x = quad.x[0] + (quad.x[1] - quad.x[0]) * u + (quad.x[3] - quad.x[0]) * v;
    *y = quad.y[0] + (quad.y[1] - quad.y[0]) * u + (quad.y[3] - quad.y[0]) * v;
    *h = quad.h[0] + (quad.h[1] - quad.h[0]) * u + (quad.h[3] - quad.h[0]) * v;
}

// Ray/parallelogram intersection in camera space (Moller-Trumbore): the ray
// from the eye along (dx, dy, 1), the parallelogram at c0 with edges e1 (u)
// and e2 (v). On a hit, *t is the camera depth and u, v lie in [0, 1).
inline bool first_person_sprite_ray_hit(const double dx, const double dy,
    const double c0[3], const double e1[3], const double e2[3], double* t, double* u, double* v)
{
    const double d[3] = { dx, dy, 1.0 };
    const double p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
    const double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::abs(det) < 1.0e-12) return false;
    const double inverse = 1.0 / det;
    const double s[3] = { -c0[0], -c0[1], -c0[2] };
    *u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inverse;
    if (*u < 0.0 || *u >= 1.0) return false;
    const double q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
    *v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inverse;
    if (*v < 0.0 || *v >= 1.0) return false;
    *t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inverse;
    return *t > 0.0;
}

} // namespace fallout
#endif
