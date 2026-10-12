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
    // Box-shaped furniture (beds, tables, stoves, lockers): a box at the
    // footprint its outline shows, each pixel on the face it depicts.
    FIRST_PERSON_SPRITE_BOX,
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

// A box read from a frame's outline, in frame pixel coordinates (x right,
// y down, both on pixel boundaries). An isometric box shows its two front
// base edges as a V along the bottom of its outline: from the left corner
// down along the North/South slope to the front corner, then up along the
// East/West slope to the right corner. The back corner of the footprint
// completes the parallelogram. The top of the outline is the top face's back
// edges, height pixels above the footprint's back edges; anything rising
// above that (a headboard, a lamp) reaches up to maxHeight.
struct FirstPersonSpriteBoxFit {
    bool ok = false;
    double leftX = 0.0;
    double frontX = 0.0;
    double rightX = 0.0;
    double frontY = 0.0;
    double height = 0.0;
    double maxHeight = 0.0;
};

// The footprint's front edges (the V) at x.
inline double first_person_sprite_box_front_y(const FirstPersonSpriteBoxFit& box, double x)
{
    const double ns = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH);
    const double ew = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_EAST_WEST);
    return x < box.frontX ? box.frontY - ns * (box.frontX - x) : box.frontY + ew * (x - box.frontX);
}

// The footprint's back edges at x: from the left corner up the East/West
// slope to the back corner, then down the North/South slope to the right.
inline double first_person_sprite_box_back_y(const FirstPersonSpriteBoxFit& box, double x)
{
    const double ns = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH);
    const double ew = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_EAST_WEST);
    const double leftY = first_person_sprite_box_front_y(box, box.leftX);
    const double rightY = first_person_sprite_box_front_y(box, box.rightX);
    const double backX = box.leftX + box.rightX - box.frontX;
    return x < backX ? leftY + ew * (x - box.leftX) : rightY - ns * (box.rightX - x);
}

// minHeight: the least top rise that makes a box; flat footprints (a rug, a
// mat) fit with a rise near zero and are tested with a negative minHeight.
inline FirstPersonSpriteBoxFit first_person_sprite_box_fit(const unsigned char* pixels, int width, int height,
    double minHeight = 4.0)
{
    FirstPersonSpriteBoxFit box;
    if (pixels == nullptr || width < 12 || height < 6) return box;
    std::vector<double> tops(width, -1.0);
    std::vector<double> bottoms(width, -1.0);
    int first = -1;
    int last = -1;
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            if (pixels[y * width + x] != 0) {
                if (tops[x] < 0.0) tops[x] = y;
                bottoms[x] = y + 1.0;
            }
        }
        if (tops[x] >= 0.0) {
            if (first < 0) first = x;
            last = x;
        }
    }
    if (first < 0 || last - first + 1 < 12) return box;
    // Columns must be contiguous: a box's outline has no gaps.
    for (int x = first; x <= last; x++) {
        if (tops[x] < 0.0) return box;
    }
    const double ns = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH);
    const double ew = first_person_sprite_axis_slope(FIRST_PERSON_SPRITE_AXIS_EAST_WEST);
    const int columns = last - first + 1;
    const int trim = columns / 33;
    // The front corner: the V the outline's bottom reaches near all three
    // corners, with no column below it (but a few stray pixels).
    double bestScore = -1.0;
    for (int frontColumn = first; frontColumn <= last + 1; frontColumn++) {
        FirstPersonSpriteBoxFit candidate;
        candidate.leftX = first;
        candidate.rightX = last + 1.0;
        candidate.frontX = frontColumn;
        std::vector<double> needed(columns);
        for (int x = first; x <= last; x++) {
            const double c = x + 0.5;
            needed[x - first] = c < candidate.frontX
                ? bottoms[x] + ns * (candidate.frontX - c)
                : bottoms[x] - ew * (c - candidate.frontX);
        }
        std::vector<double> sorted = needed;
        std::sort(sorted.begin(), sorted.end());
        candidate.frontY = sorted[columns - 1 - trim];
        // How close the outline comes to the V between two columns. Table
        // tops overhang their legs, so the side corners may touch a little
        // way in from the outline's ends.
        auto touches = [&](int from, int to) {
            double gap = 1.0e9;
            for (int x = std::max(first, from); x <= std::min(last, to); x++) {
                gap = std::min(gap, first_person_sprite_box_front_y(candidate, x + 0.5) - bottoms[x]);
            }
            return gap <= 3.0;
        };
        if (!touches(first, first + 12) || !touches(frontColumn - 4, frontColumn + 3) || !touches(last - 12, last)) {
            continue;
        }
        // The V that hugs the outline closest: least area between them.
        double gaps = 0.0;
        for (int x = first; x <= last; x++) {
            gaps += std::max(0.0, first_person_sprite_box_front_y(candidate, x + 0.5) - bottoms[x]);
        }
        const double score = 1.0e9 - gaps;
        if (score > bestScore) {
            bestScore = score;
            box = candidate;
        }
    }
    if (bestScore < 0.0) return FirstPersonSpriteBoxFit {};
    // Height: the median rise from the back edges to the outline's top; the
    // top must follow the back edges over most columns (a flat top). Raised
    // parts over a minority of columns (a headboard) reach maxHeight. Chair
    // backs and consoles' raised panels fail this and are not boxes.
    std::vector<double> rises(columns);
    for (int x = first; x <= last; x++) rises[x - first] = first_person_sprite_box_back_y(box, x + 0.5) - tops[x];
    std::vector<double> sorted = rises;
    std::sort(sorted.begin(), sorted.end());
    box.height = sorted[columns / 2];
    box.maxHeight = sorted[columns - 1];
    int following = 0;
    for (double rise : rises) following += std::abs(rise - box.height) <= 4.0;
    // Big furniture may raise up to half its top (a headboard, a hutch); a
    // small piece with that much raised is a chair, which as a box smears.
    // A small piece must also be nearly solid between its outline's top and
    // bottom: open legs (a stool, a vault chair) smear the same way.
    const int needed = columns >= 64 ? 5 : 6;
    if (columns < 64) {
        double opaque = 0.0;
        double span = 0.0;
        for (int x = first; x <= last; x++) {
            span += bottoms[x] - tops[x];
            for (int y = static_cast<int>(tops[x]); y < static_cast<int>(bottoms[x]); y++) opaque += pixels[y * width + x] != 0;
        }
        if (opaque < 0.85 * span) return FirstPersonSpriteBoxFit {};
    }
    if (box.height < minHeight || following * 10 < columns * needed) return FirstPersonSpriteBoxFit {};
    // The footprint's back corner must lie above the front corner.
    if (first_person_sprite_box_back_y(box, box.leftX + box.rightX - box.frontX) > box.frontY) {
        return FirstPersonSpriteBoxFit {};
    }
    box.ok = true;
    return box;
}

// Whether a frame shows a flat footprint lying on the floor: its outline is
// a box's with a top no more than 7 px above the footprint over most columns
// and nothing rising higher than 10 px. Rugs and mats are; bone heaps, trash
// piles and flat-flagged machinery are not.
inline bool first_person_sprite_flat_footprint(const unsigned char* pixels, int width, int height)
{
    const FirstPersonSpriteBoxFit fit = first_person_sprite_box_fit(pixels, width, height, -1.0e9);
    return fit.ok && fit.height <= 7.0 && fit.maxHeight <= 10.0;
}

// A face of a box: its world quad and, for each corner, the frame pixel
// coordinates it shows (u right, v down, in pixels; outside the frame is
// transparent).
struct FirstPersonSpriteFace {
    FirstPersonSpriteQuad quad;
    double u[4];
    double v[4];
};

// The faces of a fitted box for an object at a hex centre: two front faces,
// the top and, where art rises above the top, two fins along the back edges.
inline std::vector<FirstPersonSpriteFace> first_person_sprite_box_faces(const FirstPersonSpriteBoxFit& box,
    double hexX, double hexY, int width, int height, int offsetX, int offsetY)
{
    std::vector<FirstPersonSpriteFace> faces;
    if (!box.ok) return faces;
    const double left = offsetX - width / 2;
    const double top = offsetY - (height - 1);
    // Ground points of the footprint corners.
    auto ground = [&](double x, double y, double* wx, double* wy) {
        double dx;
        double dy;
        first_person_sprite_iso_to_world(x + left, y + top, &dx, &dy);
        *wx = hexX + dx;
        *wy = hexY + dy;
    };
    double lx, ly, fx, fy, rx, ry;
    ground(box.leftX, first_person_sprite_box_front_y(box, box.leftX), &lx, &ly);
    ground(box.frontX, box.frontY, &fx, &fy);
    ground(box.rightX, first_person_sprite_box_front_y(box, box.rightX), &rx, &ry);
    const double bx = lx + rx - fx;
    const double by = ly + ry - fy;
    const double topHeight = box.height / kFirstPersonWallArtPixelsPerHeight;
    const double maxHeight = box.maxHeight / kFirstPersonWallArtPixelsPerHeight;
    auto face = [&](const double (&xs)[4], const double (&ys)[4], const double (&hs)[4]) {
        FirstPersonSpriteFace f {};
        for (int i = 0; i < 4; i++) {
            f.quad.x[i] = xs[i];
            f.quad.y[i] = ys[i];
            f.quad.h[i] = hs[i];
            const double dx = xs[i] - hexX;
            const double dy = ys[i] - hexY;
            const double isoX = kFirstPersonIsoXFromWorldX * dx + kFirstPersonIsoXFromWorldY * dy;
            const double isoY = kFirstPersonIsoYFromWorldX * dx + kFirstPersonIsoYFromWorldY * dy
                - hs[i] * kFirstPersonWallArtPixelsPerHeight;
            f.u[i] = isoX - left;
            f.v[i] = isoY - top;
        }
        faces.push_back(f);
    };
    face({ lx, fx, fx, lx }, { ly, fy, fy, ly }, { topHeight, topHeight, 0.0, 0.0 });
    face({ fx, rx, rx, fx }, { fy, ry, ry, fy }, { topHeight, topHeight, 0.0, 0.0 });
    face({ lx, bx, rx, fx }, { ly, by, ry, fy }, { topHeight, topHeight, topHeight, topHeight });
    if (maxHeight > topHeight + 1.0 / kFirstPersonWallArtPixelsPerHeight) {
        // Fins sit a little in front of the back edges, off any wall behind.
        double inX = fx - bx;
        double inY = fy - by;
        const double length = std::sqrt(inX * inX + inY * inY);
        if (length > 1.0e-9) {
            inX *= 0.03 / length;
            inY *= 0.03 / length;
        }
        face({ lx + inX, bx + inX, bx + inX, lx + inX }, { ly + inY, by + inY, by + inY, ly + inY },
            { maxHeight, maxHeight, topHeight, topHeight });
        face({ bx + inX, rx + inX, rx + inX, bx + inX }, { by + inY, ry + inY, ry + inY, by + inY },
            { maxHeight, maxHeight, topHeight, topHeight });
    }
    return faces;
}

// The wall axis a frame's top (or bottom) edge runs along: the edge of its
// opaque columns follows one wall slope within 2 px over the best three
// quarters of its columns (a side return may take the rest), over at least
// 12 columns. Door panels have notched or uneven
// bottoms, and a rolling gear door a round top over a straight rail, so one
// edge is judged at a time.
inline FirstPersonSpriteWallAxis first_person_sprite_edge_axis(const unsigned char* pixels, int width, int height,
    bool top)
{
    if (pixels == nullptr || width < 12 || height <= 0) return FIRST_PERSON_SPRITE_AXIS_NONE;
    std::vector<double> xs;
    std::vector<double> tops;
    for (int x = 0; x < width; x++) {
        for (int i = 0; i < height; i++) {
            const int y = top ? i : height - 1 - i;
            if (pixels[y * width + x] != 0) {
                xs.push_back(x);
                tops.push_back(y);
                break;
            }
        }
    }
    if (xs.size() < 12) return FIRST_PERSON_SPRITE_AXIS_NONE;
    FirstPersonSpriteWallAxis best = FIRST_PERSON_SPRITE_AXIS_NONE;
    double bestError = 2.0;
    for (FirstPersonSpriteWallAxis axis : { FIRST_PERSON_SPRITE_AXIS_NORTH_SOUTH, FIRST_PERSON_SPRITE_AXIS_EAST_WEST }) {
        const double slope = first_person_sprite_axis_slope(axis);
        std::vector<double> offsets(xs.size());
        for (size_t i = 0; i < xs.size(); i++) offsets[i] = tops[i] - slope * xs[i];
        std::vector<double> sorted = offsets;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const double offset = sorted[sorted.size() / 2];
        std::vector<double> squares(xs.size());
        for (size_t i = 0; i < xs.size(); i++) squares[i] = (offsets[i] - offset) * (offsets[i] - offset);
        std::sort(squares.begin(), squares.end());
        const size_t kept = xs.size() - xs.size() / 4;
        double sum = 0.0;
        for (size_t i = 0; i < kept; i++) sum += squares[i];
        const double error = std::sqrt(sum / static_cast<double>(kept));
        if (error <= bestError) {
            bestError = error;
            best = axis;
        }
    }
    return best;
}

inline FirstPersonSpriteWallAxis first_person_sprite_top_axis(const unsigned char* pixels, int width, int height)
{
    return first_person_sprite_edge_axis(pixels, width, height, true);
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

// Moves a quad straight off a wall segment (a to b) to the side point
// (sideX, sideY) is on, so that no corner beside the segment (along it,
// within reach of its ends) is nearer than margin to the wall's line. Returns
// the distance moved (0 when nothing was behind or on the wall).
inline double first_person_sprite_push_off_wall(FirstPersonSpriteQuad& quad, double ax, double ay,
    double bx, double by, double sideX, double sideY, double margin, double reach)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0e-9) return 0.0;
    const double ux = dx / length;
    const double uy = dy / length;
    double nx = -uy;
    double ny = ux;
    if ((sideX - ax) * nx + (sideY - ay) * ny < 0.0) {
        nx = -nx;
        ny = -ny;
    }
    double push = 0.0;
    for (int corner = 0; corner < 4; corner++) {
        const double px = quad.x[corner] - ax;
        const double py = quad.y[corner] - ay;
        const double along = px * ux + py * uy;
        if (along < -reach || along > length + reach) continue;
        push = std::max(push, margin - (px * nx + py * ny));
    }
    if (push <= 0.0) return 0.0;
    for (int corner = 0; corner < 4; corner++) {
        quad.x[corner] += nx * push;
        quad.y[corner] += ny * push;
    }
    return push;
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
