#ifndef FALLOUT_GAME_FIRST_PERSON_VIEWMODEL_H_
#define FALLOUT_GAME_FIRST_PERSON_VIEWMODEL_H_

#include <algorithm>
#include <cmath>
#include <vector>

namespace fallout {

// The held item's inventory frame turned into a solid: its outline is
// inflated (thickest inside, rounding off toward the edges), the front and
// back show the original pixels and the sides their edge pixels, all shaded
// within the native palette. Rendering produces an indexed image for the
// existing first-person sprite overlay.

// How the item is held, which decides which end leads.
enum FirstPersonViewmodelKind {
    // Guns: the muzzle leads, away from the grip.
    FIRST_PERSON_VIEWMODEL_GUN,
    // Knives, clubs, spears, hammers: the heavier working end leads.
    FIRST_PERSON_VIEWMODEL_MELEE,
    // Stimpaks, grenades, flares: held upright as drawn.
    FIRST_PERSON_VIEWMODEL_ITEM,
};

// A frame's principal axis and leading end, in frame pixels (y down).
struct FirstPersonViewmodelAxis {
    bool ok = false;
    double centerX = 0.0;
    double centerY = 0.0;
    // Unit vector along the long axis, pointing at the leading end.
    double axisX = 1.0;
    double axisY = 0.0;
    // Extent along and across the axis.
    double length = 0.0;
    double breadth = 0.0;
};

// The stock inventory art whose leading end the rules below get wrong, by
// art name: +1 when it is on the right as drawn, -1 on the left, 0 for none.
inline int first_person_viewmodel_art_lead(const char* artName)
{
    struct Lead { const char* name; int lead; };
    static const Lead leads[] = {
        // Guns whose muzzle housing is their broad end.
        { "minigun.frm", 1 },
        { "glock.frm", 1 },
        // Melee art drawn with the working end on the right.
        { "crowbar.frm", 1 },
    };
    if (artName == nullptr) return 0;
    for (const Lead& lead : leads) {
        const char* a = lead.name;
        const char* b = artName;
        while (*a != '\0' && (*a == *b || *a == (*b | 0x20))) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') return lead.lead;
    }
    return 0;
}

// The long axis from the opaque pixels' second moments, and the leading end:
// for guns the end away from the broader one (the grip or stock), for melee
// the left end (the stock art draws blades and heads there), for items the
// right end as drawn. lead (+1 right, -1 left) overrides the rule.
inline FirstPersonViewmodelAxis first_person_viewmodel_axis(const unsigned char* pixels, int width, int height,
    FirstPersonViewmodelKind kind, int lead = 0)
{
    FirstPersonViewmodelAxis axis;
    if (pixels == nullptr || width <= 0 || height <= 0) return axis;
    double count = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (pixels[y * width + x] == 0) continue;
            count += 1.0;
            sumX += x + 0.5;
            sumY += y + 0.5;
        }
    }
    if (count < 4.0) return axis;
    axis.centerX = sumX / count;
    axis.centerY = sumY / count;
    double xx = 0.0;
    double xy = 0.0;
    double yy = 0.0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (pixels[y * width + x] == 0) continue;
            const double dx = x + 0.5 - axis.centerX;
            const double dy = y + 0.5 - axis.centerY;
            xx += dx * dx;
            xy += dx * dy;
            yy += dy * dy;
        }
    }
    const double angle = 0.5 * std::atan2(2.0 * xy, xx - yy);
    axis.axisX = std::cos(angle);
    axis.axisY = std::sin(angle);
    // Items stay as drawn, and so do compact shapes (pistols, SMGs): their
    // grip pulls the moment axis diagonal across an L-shaped outline.
    bool level = kind == FIRST_PERSON_VIEWMODEL_ITEM;
    {
        const double spread = std::sqrt((xx - yy) * (xx - yy) + 4.0 * xy * xy);
        const double major = xx + yy + spread;
        const double minor = std::max(1.0e-9, xx + yy - spread);
        if (major / minor < 1.8 * 1.8) level = true;
    }
    if (level) {
        axis.axisX = 1.0;
        axis.axisY = 0.0;
    }
    if (axis.axisX < 0.0) {
        axis.axisX = -axis.axisX;
        axis.axisY = -axis.axisY;
    }
    // Across-axis unit vector pointing down the frame.
    double downX = -axis.axisY;
    double downY = axis.axisX;
    if (downY < 0.0) {
        downX = -downX;
        downY = -downY;
    }
    double minAlong = 1.0e9;
    double maxAlong = -1.0e9;
    double minAcross = 1.0e9;
    double maxAcross = -1.0e9;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (pixels[y * width + x] == 0) continue;
            const double dx = x + 0.5 - axis.centerX;
            const double dy = y + 0.5 - axis.centerY;
            const double along = dx * axis.axisX + dy * axis.axisY;
            const double across = dx * downX + dy * downY;
            minAlong = std::min(minAlong, along);
            maxAlong = std::max(maxAlong, along);
            minAcross = std::min(minAcross, across);
            maxAcross = std::max(maxAcross, across);
        }
    }
    axis.length = maxAlong - minAlong + 1.0;
    axis.breadth = maxAcross - minAcross + 1.0;
    if (kind != FIRST_PERSON_VIEWMODEL_ITEM) {
        // The breadth across the axis over each end third.
        double breadthNear[2] = { 1.0e9, -1.0e9 };
        double breadthFar[2] = { 1.0e9, -1.0e9 };
        const double third = (maxAlong - minAlong) / 3.0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (pixels[y * width + x] == 0) continue;
                const double dx = x + 0.5 - axis.centerX;
                const double dy = y + 0.5 - axis.centerY;
                const double along = dx * axis.axisX + dy * axis.axisY;
                const double across = dx * downX + dy * downY;
                const bool near = along < minAlong + third;
                const bool far = along > maxAlong - third;
                double* range = near ? breadthNear : (far ? breadthFar : nullptr);
                if (range != nullptr) {
                    range[0] = std::min(range[0], across);
                    range[1] = std::max(range[1], across);
                }
            }
        }
        const double nearBreadth = breadthNear[1] - breadthNear[0];
        const double farBreadth = breadthFar[1] - breadthFar[0];
        // The far end is the right one as drawn (the axis points right).
        bool leadFar = kind == FIRST_PERSON_VIEWMODEL_GUN ? farBreadth < nearBreadth : false;
        if (lead != 0) leadFar = lead > 0;
        if (!leadFar) {
            axis.axisX = -axis.axisX;
            axis.axisY = -axis.axisY;
        }
    }
    axis.ok = true;
    return axis;
}

// Each opaque pixel's half thickness, in pixels: a rounded profile reaching
// maxHalf at radius pixels from the outline.
inline std::vector<float> first_person_viewmodel_thickness(const unsigned char* pixels, int width, int height,
    double maxHalf, double radius)
{
    std::vector<float> half(static_cast<size_t>(width) * height, 0.0f);
    if (pixels == nullptr || width <= 0 || height <= 0) return half;
    // Chamfer distance (3-4) to the nearest transparent pixel or the frame edge.
    const float kFar = 1.0e6f;
    std::vector<float> distance(half.size(), kFar);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (pixels[y * width + x] == 0) distance[y * width + x] = 0.0f;
        }
    }
    auto at = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0.0f;
        return distance[y * width + x];
    };
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float& d = distance[y * width + x];
            d = std::min({ d, at(x - 1, y) + 3.0f, at(x, y - 1) + 3.0f, at(x - 1, y - 1) + 4.0f, at(x + 1, y - 1) + 4.0f });
        }
    }
    for (int y = height - 1; y >= 0; y--) {
        for (int x = width - 1; x >= 0; x--) {
            float& d = distance[y * width + x];
            d = std::min({ d, at(x + 1, y) + 3.0f, at(x, y + 1) + 3.0f, at(x + 1, y + 1) + 4.0f, at(x - 1, y + 1) + 4.0f });
        }
    }
    for (size_t i = 0; i < half.size(); i++) {
        if (pixels[i] == 0) continue;
        // An edge pixel is 3 chamfer units (one pixel) from the outside; its
        // centre is half a pixel in.
        const double inside = std::max(0.5, distance[i] / 3.0 - 0.5);
        const double r = std::min(inside, radius) / radius;
        half[i] = static_cast<float>(maxHalf * std::sqrt(std::max(0.0, 1.0 - (1.0 - r) * (1.0 - r))));
    }
    return half;
}

// Placement in camera space (x right, y up, z forward, the eye at the origin):
// where the item's centre sits, the direction its leading end points, its up
// direction and how long it is in camera units.
struct FirstPersonViewmodelPose {
    double x;
    double y;
    double z;
    double leadX;
    double leadY;
    double leadZ;
    double upX;
    double upY;
    double upZ;
    double length;
};

// Stances of the held item, from the native state (see first_person.cc).
enum FirstPersonViewmodelStance {
    FIRST_PERSON_VIEWMODEL_LOWERED,
    FIRST_PERSON_VIEWMODEL_READY,
    FIRST_PERSON_VIEWMODEL_ATTACK,
    FIRST_PERSON_VIEWMODEL_RELOAD,
};

// Native weapon animation codes (art.h WEAPON_ANIMATION_*), -1 for items.
inline FirstPersonViewmodelKind first_person_viewmodel_kind(int animationCode)
{
    switch (animationCode) {
    case 1: // knife
    case 2: // club
    case 3: // hammer
    case 4: // spear
        return FIRST_PERSON_VIEWMODEL_MELEE;
    case 5: // pistol
    case 6: // SMG
    case 7: // shotgun
    case 8: // laser rifle
    case 9: // minigun
    case 10: // launcher
        return FIRST_PERSON_VIEWMODEL_GUN;
    default:
        return FIRST_PERSON_VIEWMODEL_ITEM;
    }
}

// Where the item is held for a stance: guns low and right of centre with the
// muzzle pointing into the view and a little toward its centre, as in Doom
// and Quake; melee weapons angled up from the lower right; items held up in
// front, turned a little. Length in camera units grows with the weapon class.
inline FirstPersonViewmodelPose first_person_viewmodel_pose(int animationCode, FirstPersonViewmodelStance stance)
{
    const FirstPersonViewmodelKind kind = first_person_viewmodel_kind(animationCode);
    FirstPersonViewmodelPose pose {};
    if (kind == FIRST_PERSON_VIEWMODEL_GUN) {
        double length = 0.30;
        if (animationCode == 6) length = 0.33;
        if (animationCode == 7 || animationCode == 8) length = 0.60;
        if (animationCode == 9 || animationCode == 10) length = 0.68;
        pose = { 0.24, -0.21, 0.72, -0.42, 0.05, 1.0, 0.05, 1.0, 0.0, length };
    } else if (kind == FIRST_PERSON_VIEWMODEL_MELEE) {
        // Raised from the lower right, tipped forward, with its flat side
        // (a blade, a hammer's face) turned to the viewer.
        double length = 0.30;
        if (animationCode == 3 || animationCode == 4) length = 0.60;
        if (animationCode == 2) length = 0.42;
        pose = { 0.24, -0.22, 0.66, -0.25, 0.85, 0.45, -1.0, 0.0, 0.0, length };
    } else {
        pose = { 0.17, -0.17, 0.60, 0.92, 0.18, -0.25, 0.0, 1.0, 0.0, 0.15 };
    }
    switch (stance) {
    case FIRST_PERSON_VIEWMODEL_LOWERED:
        pose.y -= 0.10;
        pose.x += 0.04;
        pose.leadY -= 0.22;
        break;
    case FIRST_PERSON_VIEWMODEL_ATTACK:
        // Kick: back toward the eye and the lead tipped up.
        pose.z -= 0.05;
        pose.y += 0.015;
        pose.leadY += 0.12;
        break;
    case FIRST_PERSON_VIEWMODEL_RELOAD:
        pose.y -= 0.14;
        pose.leadY -= 0.10;
        pose.upX += 0.45;
        break;
    case FIRST_PERSON_VIEWMODEL_READY:
    default:
        break;
    }
    return pose;
}

// The output: an indexed image covering the item's screen bounds, positioned
// at (left, top) in a view of the given size.
struct FirstPersonViewmodelImage {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

// Renders the solid. focal is in screen pixels; the view centre is
// (viewWidth / 2, viewHeight / 2). shade(color, intensity) maps a palette
// colour through the native intensity table (128 leaves it unchanged).
template <typename Shade>
FirstPersonViewmodelImage first_person_viewmodel_render(const unsigned char* pixels, int width, int height,
    const FirstPersonViewmodelAxis& axis, const std::vector<float>& half, const FirstPersonViewmodelPose& pose,
    int viewWidth, int viewHeight, double focal, Shade shade)
{
    FirstPersonViewmodelImage image;
    if (!axis.ok || pixels == nullptr || width <= 0 || height <= 0 || axis.length <= 0.0) return image;
    // Camera-space basis: lead, up (made perpendicular to lead) and side.
    double lx = pose.leadX, ly = pose.leadY, lz = pose.leadZ;
    double ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    if (ll < 1.0e-9) return image;
    lx /= ll;
    ly /= ll;
    lz /= ll;
    double ux = pose.upX, uy = pose.upY, uz = pose.upZ;
    const double ud = ux * lx + uy * ly + uz * lz;
    ux -= ud * lx;
    uy -= ud * ly;
    uz -= ud * lz;
    double ul = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (ul < 1.0e-9) return image;
    ux /= ul;
    uy /= ul;
    uz /= ul;
    // side = lead x up: the frame's front faces the viewer when the item is
    // to the right of centre with its lead pointing away.
    const double sx = ly * uz - lz * uy;
    const double sy = lz * ux - lx * uz;
    const double sz = lx * uy - ly * ux;
    // length is the item's largest extent, along or across its axis (an
    // upright bottle is longer across the level axis than along it).
    const double scale = pose.length / std::max(axis.length, axis.breadth);
    // Frame "up" (toward smaller y, across the axis).
    double frameUpX = axis.axisY;
    double frameUpY = -axis.axisX;
    if (frameUpY > 0.0) {
        frameUpX = -frameUpX;
        frameUpY = -frameUpY;
    }
    // Frame point (fx, fy) at depth dz (pixels, + toward the front face) to camera.
    auto toCamera = [&](double fx, double fy, double dz, double* c) {
        const double dx = fx - axis.centerX;
        const double dy = fy - axis.centerY;
        const double along = (dx * axis.axisX + dy * axis.axisY) * scale;
        const double upward = (dx * frameUpX + dy * frameUpY) * scale;
        const double out = dz * scale;
        c[0] = pose.x + lx * along + ux * upward + sx * out;
        c[1] = pose.y + ly * along + uy * upward + sy * out;
        c[2] = pose.z + lz * along + uz * upward + sz * out;
    };
    // The art's frame axes may be mirrored by the lead flip: the front face
    // is the one whose normal faces the eye, whichever sign that takes.
    struct Quad { double c[4][3]; unsigned char color; };
    std::vector<Quad> quads;
    quads.reserve(static_cast<size_t>(width) * height);
    // Light from the upper left, toward the viewer (camera space, unit).
    const double lightX = -0.40, lightY = 0.62, lightZ = -0.67;
    auto addQuad = [&](const double (&corners)[4][3], unsigned char color, double nx, double ny, double nz,
                       double base) {
        const double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        double lambert = nl > 1.0e-9 ? (nx * lightX + ny * lightY + nz * lightZ) / nl : 0.0;
        // Faces turned away from the eye light from the back the same way.
        if (lambert < 0.0) lambert = -lambert * 0.35;
        const int intensity = std::clamp(static_cast<int>(std::lround(128.0 * (base + 0.35 * lambert))), 0, 255);
        Quad quad;
        std::copy(&corners[0][0], &corners[0][0] + 12, &quad.c[0][0]);
        // Index 0 is transparent: a colour shaded to it keeps its own.
        const unsigned char shaded = shade(color, intensity);
        quad.color = shaded != 0 ? shaded : color;
        quads.push_back(quad);
    };
    auto halfAt = [&](int x, int y) -> double {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0.0;
        return half[y * width + x];
    };
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const unsigned char color = pixels[y * width + x];
            if (color == 0) continue;
            const double t = halfAt(x, y);
            // Heightfield normal for the face shading, in camera space.
            const double gx = (halfAt(x + 1, y) - halfAt(x - 1, y)) * 0.5;
            const double gy = (halfAt(x, y + 1) - halfAt(x, y - 1)) * 0.5;
            for (int face = 0; face < 2; face++) {
                const double z = face == 0 ? t : -t;
                double corners[4][3];
                toCamera(x, y, z, corners[0]);
                toCamera(x + 1.0, y, z, corners[1]);
                toCamera(x + 1.0, y + 1.0, z, corners[2]);
                toCamera(x, y + 1.0, z, corners[3]);
                // Normal in frame terms: (-gx, -gy, +-1), mapped like a direction.
                double a[3];
                double b[3];
                toCamera(axis.centerX - gx, axis.centerY - gy, face == 0 ? 1.0 : -1.0, a);
                toCamera(axis.centerX, axis.centerY, 0.0, b);
                addQuad(corners, color, a[0] - b[0], a[1] - b[1], a[2] - b[2], 0.88);
            }
            // Side walls toward thinner neighbours (or the outside).
            const int nxs[4] = { -1, 1, 0, 0 };
            const int nys[4] = { 0, 0, -1, 1 };
            for (int n = 0; n < 4; n++) {
                const int x2 = x + nxs[n];
                const int y2 = y + nys[n];
                const bool outside = x2 < 0 || y2 < 0 || x2 >= width || y2 >= height || pixels[y2 * width + x2] == 0;
                const double t2 = outside ? 0.0 : halfAt(x2, y2);
                if (t2 >= t) continue;
                // The shared edge.
                double ex0, ey0, ex1, ey1;
                if (n == 0) { ex0 = x; ey0 = y; ex1 = x; ey1 = y + 1.0; }
                else if (n == 1) { ex0 = x + 1.0; ey0 = y; ex1 = x + 1.0; ey1 = y + 1.0; }
                else if (n == 2) { ex0 = x; ey0 = y; ex1 = x + 1.0; ey1 = y; }
                else { ex0 = x; ey0 = y + 1.0; ex1 = x + 1.0; ey1 = y + 1.0; }
                double a[3];
                double b[3];
                toCamera(axis.centerX + nxs[n], axis.centerY + nys[n], 0.0, a);
                toCamera(axis.centerX, axis.centerY, 0.0, b);
                const double wnx = a[0] - b[0], wny = a[1] - b[1], wnz = a[2] - b[2];
                for (int side = 0; side < 2; side++) {
                    const double z0 = side == 0 ? t2 : -t;
                    const double z1 = side == 0 ? t : -t2;
                    if (z1 - z0 < 1.0e-6) continue;
                    double corners[4][3];
                    toCamera(ex0, ey0, z1, corners[0]);
                    toCamera(ex1, ey1, z1, corners[1]);
                    toCamera(ex1, ey1, z0, corners[2]);
                    toCamera(ex0, ey0, z0, corners[3]);
                    addQuad(corners, color, wnx, wny, wnz, 0.62);
                }
            }
        }
    }
    // Project and find the screen bounds.
    const double cx = viewWidth / 2.0;
    const double cy = viewHeight / 2.0;
    constexpr double kNear = 0.05;
    double minX = 1.0e9, minY = 1.0e9, maxX = -1.0e9, maxY = -1.0e9;
    for (const Quad& quad : quads) {
        for (const auto& c : quad.c) {
            if (c[2] < kNear) continue;
            const double px = cx + c[0] * focal / c[2];
            const double py = cy - c[1] * focal / c[2];
            minX = std::min(minX, px);
            maxX = std::max(maxX, px);
            minY = std::min(minY, py);
            maxY = std::max(maxY, py);
        }
    }
    if (maxX < minX) return image;
    image.left = std::max(0, static_cast<int>(std::floor(minX)));
    image.top = std::max(0, static_cast<int>(std::floor(minY)));
    const int right = std::min(viewWidth - 1, static_cast<int>(std::ceil(maxX)));
    const int bottom = std::min(viewHeight - 1, static_cast<int>(std::ceil(maxY)));
    if (right < image.left || bottom < image.top) return image;
    image.width = right - image.left + 1;
    image.height = bottom - image.top + 1;
    image.pixels.assign(static_cast<size_t>(image.width) * image.height, 0);
    std::vector<float> depth(image.pixels.size(), 0.0f); // 1/z, larger is nearer
    auto triangle = [&](const double* a, const double* b, const double* c, unsigned char color) {
        if (a[2] < kNear || b[2] < kNear || c[2] < kNear) return;
        const double ax = cx + a[0] * focal / a[2] - image.left, ay = cy - a[1] * focal / a[2] - image.top;
        const double bx = cx + b[0] * focal / b[2] - image.left, by = cy - b[1] * focal / b[2] - image.top;
        const double qx = cx + c[0] * focal / c[2] - image.left, qy = cy - c[1] * focal / c[2] - image.top;
        const double area = (bx - ax) * (qy - ay) - (by - ay) * (qx - ax);
        if (std::abs(area) < 1.0e-12) return;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ ax, bx, qx }))));
        const int x1 = std::min(image.width - 1, static_cast<int>(std::ceil(std::max({ ax, bx, qx }))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ ay, by, qy }))));
        const int y1 = std::min(image.height - 1, static_cast<int>(std::ceil(std::max({ ay, by, qy }))));
        const double ia = 1.0 / a[2], ib = 1.0 / b[2], ic = 1.0 / c[2];
        for (int y = y0; y <= y1; y++) {
            const double py = y + 0.5;
            for (int x = x0; x <= x1; x++) {
                const double px = x + 0.5;
                double w0 = ((bx - px) * (qy - py) - (by - py) * (qx - px)) / area;
                double w1 = ((qx - px) * (ay - py) - (qy - py) * (ax - px)) / area;
                double w2 = 1.0 - w0 - w1;
                if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;
                const float inverse = static_cast<float>(w0 * ia + w1 * ib + w2 * ic);
                const size_t i = static_cast<size_t>(y) * image.width + x;
                if (inverse > depth[i]) {
                    depth[i] = inverse;
                    image.pixels[i] = color;
                }
            }
        }
    };
    for (const Quad& quad : quads) {
        triangle(quad.c[0], quad.c[1], quad.c[2], quad.color);
        triangle(quad.c[0], quad.c[2], quad.c[3], quad.color);
    }
    return image;
}

} // namespace fallout
#endif
