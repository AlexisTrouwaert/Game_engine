#include "moteur/msdf.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace moteur::msdf {

namespace {

constexpr std::uint8_t kRed = 1, kGreen = 2, kBlue = 4;
constexpr std::uint8_t kYellow = kRed | kGreen, kMagenta = kRed | kBlue, kCyan = kGreen | kBlue;
constexpr std::uint8_t kWhite = kRed | kGreen | kBlue, kBlack = 0;

double cross(glm::dvec2 a, glm::dvec2 b) {
    return a.x * b.y - a.y * b.x;
}

double non_zero_sign(double v) {
    return v > 0 ? 1.0 : -1.0;
}

glm::dvec2 normalize_or_zero(glm::dvec2 v) {
    const double length = glm::length(v);
    return length > 0 ? v / length : glm::dvec2(0.0);
}

using Segment = Outline::Segment;
using Contour = Outline::Contour;

glm::dvec2 point_at(const Segment& s, double t) {
    if (!s.quadratic) {
        return glm::mix(s.p0, s.p2, t);
    }
    return glm::mix(glm::mix(s.p0, s.p1, t), glm::mix(s.p1, s.p2, t), t);
}

glm::dvec2 direction_at(const Segment& s, double t) {
    if (!s.quadratic) {
        return s.p2 - s.p0;
    }
    const glm::dvec2 tangent = glm::mix(s.p1 - s.p0, s.p2 - s.p1, t);
    if (tangent.x == 0 && tangent.y == 0) {
        return s.p2 - s.p0;
    }
    return tangent;
}

// Splits a quadratic curve at t (de Casteljau).
void split_quadratic(const Segment& s, double t, Segment& first, Segment& second) {
    const glm::dvec2 a = glm::mix(s.p0, s.p1, t);
    const glm::dvec2 b = glm::mix(s.p1, s.p2, t);
    const glm::dvec2 m = glm::mix(a, b, t);
    first = {s.p0, a, m, true, s.color};
    second = {m, b, s.p2, true, s.color};
}

void split_in_thirds(const Segment& s, Segment parts[3]) {
    if (!s.quadratic) {
        const glm::dvec2 a = glm::mix(s.p0, s.p2, 1.0 / 3.0);
        const glm::dvec2 b = glm::mix(s.p0, s.p2, 2.0 / 3.0);
        parts[0] = {s.p0, (s.p0 + a) * 0.5, a, false, s.color};
        parts[1] = {a, (a + b) * 0.5, b, false, s.color};
        parts[2] = {b, (b + s.p2) * 0.5, s.p2, false, s.color};
        return;
    }
    Segment rest;
    split_quadratic(s, 1.0 / 3.0, parts[0], rest);
    split_quadratic(rest, 0.5, parts[1], parts[2]);
}

// --- Equations (closed forms, as msdfgen solves them) ----------------------------------------------

int solve_quadratic(double x[2], double a, double b, double c) {
    if (a == 0 || std::fabs(b) > 1e12 * std::fabs(a)) {
        if (b == 0) {
            return 0;
        }
        x[0] = -c / b;
        return 1;
    }
    double discriminant = b * b - 4 * a * c;
    if (discriminant > 0) {
        discriminant = std::sqrt(discriminant);
        x[0] = (-b + discriminant) / (2 * a);
        x[1] = (-b - discriminant) / (2 * a);
        return 2;
    }
    if (discriminant == 0) {
        x[0] = -b / (2 * a);
        return 1;
    }
    return 0;
}

int solve_cubic_normed(double x[3], double a, double b, double c) {
    const double a2 = a * a;
    double q = (a2 - 3 * b) / 9.0;
    const double r = (a * (2 * a2 - 9 * b) + 27 * c) / 54.0;
    const double r2 = r * r;
    const double q3 = q * q * q;
    a /= 3.0;
    if (r2 < q3) {
        double t = r / std::sqrt(q3);
        t = std::clamp(t, -1.0, 1.0);
        t = std::acos(t);
        q = -2 * std::sqrt(q);
        x[0] = q * std::cos(t / 3.0) - a;
        x[1] = q * std::cos((t + 2 * std::numbers::pi) / 3.0) - a;
        x[2] = q * std::cos((t - 2 * std::numbers::pi) / 3.0) - a;
        return 3;
    }
    const double u = (r < 0 ? 1 : -1) * std::pow(std::fabs(r) + std::sqrt(r2 - q3), 1.0 / 3.0);
    const double v = u == 0 ? 0 : q / u;
    x[0] = (u + v) - a;
    if (u == v || std::fabs(u - v) < 1e-12 * std::fabs(u + v)) {
        x[1] = -0.5 * (u + v) - a;
        return 2;
    }
    return 1;
}

int solve_cubic(double x[3], double a, double b, double c, double d) {
    if (a != 0) {
        const double bn = b / a;
        if (std::fabs(bn) < 1e6) {
            return solve_cubic_normed(x, bn, c / a, d / a);
        }
    }
    return solve_quadratic(x, b, c, d);
}

// --- Signed distances -----------------------------------------------------------------------------

// A distance and how orthogonally it meets its edge (to pick between equal distances at a corner).
struct SignedDistance {
    double distance = -std::numeric_limits<double>::max();
    double dot = 1.0;
};

bool operator<(const SignedDistance& a, const SignedDistance& b) {
    const double fa = std::fabs(a.distance);
    const double fb = std::fabs(b.distance);
    return fa < fb || (fa == fb && a.dot < b.dot);
}

SignedDistance signed_distance(const Segment& s, glm::dvec2 origin, double& param) {
    if (!s.quadratic) {
        const glm::dvec2 aq = origin - s.p0;
        const glm::dvec2 ab = s.p2 - s.p0;
        const double ab2 = glm::dot(ab, ab);
        param = ab2 > 0 ? glm::dot(aq, ab) / ab2 : 0.0;
        const glm::dvec2 eq = (param > 0.5 ? s.p2 : s.p0) - origin;
        const double endpoint_distance = glm::length(eq);
        if (param > 0 && param < 1) {
            const double ortho = cross(aq, ab) / std::sqrt(ab2);
            if (std::fabs(ortho) < endpoint_distance) {
                return {ortho, 0.0};
            }
        }
        return {non_zero_sign(cross(aq, ab)) * endpoint_distance,
                std::fabs(glm::dot(normalize_or_zero(ab), normalize_or_zero(eq)))};
    }
    const glm::dvec2 qa = s.p0 - origin;
    const glm::dvec2 ab = s.p1 - s.p0;
    const glm::dvec2 br = s.p2 - s.p1 - ab;
    const double a = glm::dot(br, br);
    const double b = 3 * glm::dot(ab, br);
    const double c = 2 * glm::dot(ab, ab) + glm::dot(qa, br);
    const double d = glm::dot(qa, ab);
    double t[3];
    const int solutions = solve_cubic(t, a, b, c, d);

    glm::dvec2 ep_dir = direction_at(s, 0);
    double min_distance = non_zero_sign(cross(ep_dir, qa)) * glm::length(qa);
    param = -glm::dot(qa, ep_dir) / glm::dot(ep_dir, ep_dir);
    {
        ep_dir = direction_at(s, 1);
        const double distance = glm::length(s.p2 - origin);
        if (distance < std::fabs(min_distance)) {
            min_distance = non_zero_sign(cross(ep_dir, s.p2 - origin)) * distance;
            param = glm::dot(origin - s.p1, ep_dir) / glm::dot(ep_dir, ep_dir);
        }
    }
    for (int i = 0; i < solutions; ++i) {
        if (t[i] > 0 && t[i] < 1) {
            const glm::dvec2 qe = qa + 2 * t[i] * ab + t[i] * t[i] * br;
            const double distance = glm::length(qe);
            if (distance <= std::fabs(min_distance)) {
                min_distance = non_zero_sign(cross(ab + t[i] * br, qe)) * distance;
                param = t[i];
            }
        }
    }
    if (param >= 0 && param <= 1) {
        return {min_distance, 0.0};
    }
    if (param < 0.5) {
        return {min_distance, std::fabs(glm::dot(normalize_or_zero(direction_at(s, 0)), normalize_or_zero(qa)))};
    }
    return {min_distance,
            std::fabs(glm::dot(normalize_or_zero(direction_at(s, 1)), normalize_or_zero(s.p2 - origin)))};
}

// Beyond the ends of an edge, the distance to the edge's tangent line (the "pseudo-distance" that
// keeps corners sharp).
void to_perpendicular(const Segment& s, SignedDistance& distance, glm::dvec2 origin, double param) {
    if (param < 0) {
        const glm::dvec2 dir = normalize_or_zero(direction_at(s, 0));
        const glm::dvec2 aq = origin - s.p0;
        if (glm::dot(aq, dir) < 0) {
            const double perpendicular = cross(aq, dir);
            if (std::fabs(perpendicular) <= std::fabs(distance.distance)) {
                distance = {perpendicular, 0.0};
            }
        }
    } else if (param > 1) {
        const glm::dvec2 dir = normalize_or_zero(direction_at(s, 1));
        const glm::dvec2 bq = origin - s.p2;
        if (glm::dot(bq, dir) > 0) {
            const double perpendicular = cross(bq, dir);
            if (std::fabs(perpendicular) <= std::fabs(distance.distance)) {
                distance = {perpendicular, 0.0};
            }
        }
    }
}

struct MultiDistance {
    double r, g, b, a;  // a: the true distance
};

double median(double a, double b, double c) {
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

// The nearest edge of each channel, and the nearest edge of all (msdfgen's multi-and-true
// distance selector).
struct Selector {
    struct Channel {
        SignedDistance distance;
        const Segment* edge = nullptr;
        double param = 0;
    };
    Channel channels[3];
    SignedDistance true_distance;

    void add(const Segment& s, glm::dvec2 origin) {
        double param = 0;
        const SignedDistance d = signed_distance(s, origin, param);
        if (d < true_distance) {
            true_distance = d;
        }
        for (int c = 0; c < 3; ++c) {
            if ((s.color & (1u << c)) != 0 && d < channels[c].distance) {
                channels[c] = {d, &s, param};
            }
        }
    }
    void merge(const Selector& other) {
        if (other.true_distance < true_distance) {
            true_distance = other.true_distance;
        }
        for (int c = 0; c < 3; ++c) {
            if (other.channels[c].distance < channels[c].distance) {
                channels[c] = other.channels[c];
            }
        }
    }
    MultiDistance distance(glm::dvec2 origin) const {
        double out[3];
        for (int c = 0; c < 3; ++c) {
            SignedDistance d = channels[c].distance;
            if (channels[c].edge != nullptr) {
                to_perpendicular(*channels[c].edge, d, origin, channels[c].param);
            }
            out[c] = d.distance;
        }
        return {out[0], out[1], out[2], true_distance.distance};
    }
};

// --- Edge coloring (msdfgen's simple method) --------------------------------------------------------

bool is_corner(glm::dvec2 a, glm::dvec2 b, double cross_threshold) {
    return glm::dot(a, b) <= 0 || std::fabs(cross(a, b)) > cross_threshold;
}

void switch_color(std::uint8_t& color, unsigned long long& seed, std::uint8_t banned = kBlack) {
    const auto combined = static_cast<std::uint8_t>(color & banned);
    if (combined == kRed || combined == kGreen || combined == kBlue) {
        color = static_cast<std::uint8_t>(combined ^ kWhite);
        return;
    }
    if (color == kBlack || color == kWhite) {
        static constexpr std::uint8_t kStart[3] = {kCyan, kMagenta, kYellow};
        color = kStart[seed % 3];
        seed /= 3;
        return;
    }
    const int shifted = color << (1 + (seed & 1));
    color = static_cast<std::uint8_t>((shifted | shifted >> 3) & kWhite);
    seed >>= 1;
}

int symmetrical_trichotomy(int position, int n) {
    // msdfgen's formula: the value is positive, the floor is the rounding it means.
    return static_cast<int>(std::floor(3 + 2.875 * position / (n - 1) - 1.4375 + 0.5)) - 3;
}

// --- Contours in texel space ------------------------------------------------------------------------

// --- The boundary of the filled shape -------------------------------------------------------------
// Variable fonts keep their overlaps (a stroke drawn over another, an "e" drawn as one contour
// crossing itself): the edges inside the shape would give its inside small distances (specks, seams).
// The edges are cut where they cross, and only the pieces with the shape on exactly one side are
// kept, turned so that the shape is on their right (distances positive inside), then chained back
// into closed contours.

struct Box {
    glm::dvec2 lo, hi;
};

Box box_of(const Segment& s) {
    return {glm::min(glm::min(s.p0, s.p1), s.p2), glm::max(glm::max(s.p0, s.p1), s.p2)};
}

void halves(const Segment& s, Segment& left, Segment& right) {
    if (s.quadratic) {
        split_quadratic(s, 0.5, left, right);
        return;
    }
    const glm::dvec2 m = (s.p0 + s.p2) * 0.5;
    left = {s.p0, (s.p0 + m) * 0.5, m, false, s.color};
    right = {m, (m + s.p2) * 0.5, s.p2, false, s.color};
}

// The part [t0, t1] of a segment.
Segment piece_of(const Segment& s, double t0, double t1) {
    if (!s.quadratic) {
        const glm::dvec2 a = glm::mix(s.p0, s.p2, t0);
        const glm::dvec2 b = glm::mix(s.p0, s.p2, t1);
        return {a, (a + b) * 0.5, b, false, s.color};
    }
    Segment first, rest, unused;
    split_quadratic(s, t1, first, unused);
    if (t0 <= 0.0) {
        return first;
    }
    split_quadratic(first, t0 / t1, unused, rest);
    return rest;
}

// Where two segments cross (parameters on each), by subdivision of their control boxes.
void intersect(const Segment& a, double a0, double a1, const Segment& b, double b0, double b1, int depth, int& budget,
               std::vector<double>& on_a, std::vector<double>& on_b) {
    if (--budget < 0) {
        return;
    }
    const Box A = box_of(a);
    const Box B = box_of(b);
    if (A.hi.x < B.lo.x || B.hi.x < A.lo.x || A.hi.y < B.lo.y || B.hi.y < A.lo.y) {
        return;
    }
    const double size = std::max(std::max(A.hi.x - A.lo.x, A.hi.y - A.lo.y), std::max(B.hi.x - B.lo.x, B.hi.y - B.lo.y));
    if (size < 1e-5 || depth >= 40) {
        on_a.push_back((a0 + a1) * 0.5);
        on_b.push_back((b0 + b1) * 0.5);
        return;
    }
    Segment al, ar, bl, br;
    halves(a, al, ar);
    halves(b, bl, br);
    const double am = (a0 + a1) * 0.5, bm = (b0 + b1) * 0.5;
    intersect(al, a0, am, bl, b0, bm, depth + 1, budget, on_a, on_b);
    intersect(al, a0, am, br, bm, b1, depth + 1, budget, on_a, on_b);
    intersect(ar, am, a1, bl, b0, bm, depth + 1, budget, on_a, on_b);
    intersect(ar, am, a1, br, bm, b1, depth + 1, budget, on_a, on_b);
}

// The non-zero winding number of the shape around p (a ray towards +x, exact on the curves).
int winding_at(const std::vector<const Segment*>& segments, glm::dvec2 p) {
    int winding = 0;
    for (const Segment* seg : segments) {
        const Segment& s = *seg;
        if (!s.quadratic) {
            const double y0 = s.p0.y, y1 = s.p2.y;
            if ((y0 <= p.y && y1 > p.y) || (y1 <= p.y && y0 > p.y)) {
                const double t = (p.y - y0) / (y1 - y0);
                if (s.p0.x + t * (s.p2.x - s.p0.x) > p.x) {
                    winding += y1 > y0 ? 1 : -1;
                }
            }
            continue;
        }
        const double a = s.p0.y - 2 * s.p1.y + s.p2.y;
        const double b = 2 * (s.p1.y - s.p0.y);
        const double c = s.p0.y - p.y;
        double t[2];
        const int roots = solve_quadratic(t, a, b, c);
        for (int r = 0; r < roots; ++r) {
            if (t[r] < 0 || t[r] >= 1) {
                continue;
            }
            const double dy = 2 * a * t[r] + b;
            if (dy == 0) {
                continue;
            }
            if (point_at(s, t[r]).x > p.x) {
                winding += dy > 0 ? 1 : -1;
            }
        }
    }
    return winding;
}

std::vector<Contour> shape_boundary(const std::vector<Contour>& contours) {
    std::vector<const Segment*> all;
    for (const Contour& c : contours) {
        for (const Segment& s : c) {
            all.push_back(&s);
        }
    }
    // Where each segment is cut.
    std::vector<std::vector<double>> cuts(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        const Box A = box_of(*all[i]);
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            const Box B = box_of(*all[j]);
            if (A.hi.x < B.lo.x || B.hi.x < A.lo.x || A.hi.y < B.lo.y || B.hi.y < A.lo.y) {
                continue;
            }
            std::vector<double> on_a, on_b;
            int budget = 20000;  // two edges lying on each other would never end: they are left alone
            intersect(*all[i], 0.0, 1.0, *all[j], 0.0, 1.0, 0, budget, on_a, on_b);
            if (budget < 0) {
                continue;
            }
            for (std::size_t k = 0; k < on_a.size(); ++k) {
                constexpr double kEnd = 1e-4;  // the joint of two neighbours is not a crossing
                if (on_a[k] > kEnd && on_a[k] < 1 - kEnd) cuts[i].push_back(on_a[k]);
                if (on_b[k] > kEnd && on_b[k] < 1 - kEnd) cuts[j].push_back(on_b[k]);
            }
        }
    }
    // The pieces with the shape on one side only, the shape on their right.
    std::vector<Segment> pieces;
    for (std::size_t i = 0; i < all.size(); ++i) {
        std::vector<double>& t = cuts[i];
        t.push_back(0.0);
        t.push_back(1.0);
        std::sort(t.begin(), t.end());
        for (std::size_t k = 0; k + 1 < t.size(); ++k) {
            if (t[k + 1] - t[k] < 1e-7) {
                continue;
            }
            const Segment piece = piece_of(*all[i], t[k], t[k + 1]);
            const double tm = (t[k] + t[k + 1]) * 0.5;
            const glm::dvec2 p = point_at(*all[i], tm) + glm::dvec2(0.0, 1.2345e-7);  // never exactly on a vertex
            const glm::dvec2 d = normalize_or_zero(direction_at(*all[i], tm));
            const glm::dvec2 right(d.y, -d.x);
            constexpr double kSide = 0.02;  // texels
            const bool filled_right = winding_at(all, p + right * kSide) != 0;
            const bool filled_left = winding_at(all, p - right * kSide) != 0;
            if (filled_right && !filled_left) {
                pieces.push_back(piece);
            } else if (filled_left && !filled_right) {
                Segment reversed = piece;
                std::swap(reversed.p0, reversed.p2);
                pieces.push_back(reversed);
            }
        }
    }
    // Chained back: each piece followed by the one starting where it ends.
    std::vector<Contour> out;
    std::vector<bool> used(pieces.size(), false);
    constexpr double kJoin = 1e-3;  // texels
    for (std::size_t first = 0; first < pieces.size(); ++first) {
        if (used[first]) {
            continue;
        }
        Contour contour;
        std::size_t current = first;
        used[current] = true;
        contour.push_back(pieces[current]);
        while (true) {
            const glm::dvec2 end = contour.back().p2;
            if (contour.size() > 1 && glm::length(end - contour.front().p0) < kJoin) {
                contour.back().p2 = contour.front().p0;  // closed
                break;
            }
            std::size_t next = pieces.size();
            double best = kJoin;
            for (std::size_t k = 0; k < pieces.size(); ++k) {
                if (!used[k]) {
                    const double gap = glm::length(pieces[k].p0 - end);
                    if (gap < best) {
                        best = gap;
                        next = k;
                    }
                }
            }
            if (next == pieces.size()) {
                break;  // left open (a piece misjudged): its distances are still right nearby
            }
            used[next] = true;
            Segment s = pieces[next];
            s.p0 = end;  // exactly joined
            if (!s.quadratic) {
                s.p1 = (s.p0 + s.p2) * 0.5;
            }
            contour.push_back(s);
        }
        out.push_back(std::move(contour));
    }
    return out;
}

// The non-zero fill of each texel center, row by row (scanlines through the flattened outline).
std::vector<bool> fill_mask(const std::vector<Contour>& contours, int width, int height) {
    struct Edge {
        glm::dvec2 a, b;
    };
    std::vector<Edge> edges;
    for (const Contour& contour : contours) {
        for (const Segment& s : contour) {
            const int steps = s.quadratic ? 16 : 1;
            glm::dvec2 a = s.p0;
            for (int k = 1; k <= steps; ++k) {
                const glm::dvec2 b = point_at(s, static_cast<double>(k) / steps);
                if (a.y != b.y) {
                    edges.push_back({a, b});
                }
                a = b;
            }
        }
    }
    std::vector<bool> filled(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), false);
    std::vector<std::pair<double, int>> crossings;
    for (int row = 0; row < height; ++row) {
        const double y = row + 0.5;
        crossings.clear();
        for (const Edge& e : edges) {
            const bool up = e.a.y < e.b.y;
            const double y0 = up ? e.a.y : e.b.y;
            const double y1 = up ? e.b.y : e.a.y;
            if (y >= y0 && y < y1) {  // half-open: a shared end point counts once
                const double t = (y - e.a.y) / (e.b.y - e.a.y);
                crossings.emplace_back(e.a.x + t * (e.b.x - e.a.x), up ? 1 : -1);
            }
        }
        std::sort(crossings.begin(), crossings.end());
        std::size_t next = 0;
        int winding = 0;
        for (int x = 0; x < width; ++x) {
            const double cx = x + 0.5;
            while (next < crossings.size() && crossings[next].first < cx) {
                winding += crossings[next].second;
                ++next;
            }
            filled[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                winding != 0;
        }
    }
    return filled;
}

// msdfgen's legacy clash detection: two neighbouring texels whose channels differ by more than a
// distance can change over one texel, in a way bilinear filtering would turn into a hole.
bool detect_clash(const float* a, const float* b, float threshold) {
    float a0 = a[0], a1 = a[1], a2 = a[2];
    float b0 = b[0], b1 = b[1], b2 = b[2];
    if (std::fabs(b0 - a0) < std::fabs(b1 - a1)) {
        std::swap(a0, a1);
        std::swap(b0, b1);
    }
    if (std::fabs(b1 - a1) < std::fabs(b2 - a2)) {
        std::swap(a1, a2);
        std::swap(b1, b2);
        if (std::fabs(b0 - a0) < std::fabs(b1 - a1)) {
            std::swap(a0, a1);
            std::swap(b0, b1);
        }
    }
    return std::fabs(b0 - a0) >= threshold && !(b0 == b1 && b0 == b2) && std::fabs(a2 - 0.5f) >= std::fabs(b2 - 0.5f);
}

}  // namespace

// --- Outline ----------------------------------------------------------------------------------------

void Outline::move_to(glm::dvec2 p) {
    close();
    start_ = pen_ = p;
    open_ = true;
    contours_.emplace_back();
}

void Outline::line_to(glm::dvec2 p) {
    if (!open_) {
        move_to(pen_);
    }
    if (p != pen_) {
        contours_.back().push_back({pen_, (pen_ + p) * 0.5, p, false, kWhite});
    }
    pen_ = p;
}

void Outline::quadratic_to(glm::dvec2 control, glm::dvec2 p) {
    if (!open_) {
        move_to(pen_);
    }
    if (p != pen_ || control != pen_) {
        if (cross(control - pen_, p - pen_) == 0 && glm::dot(control - pen_, p - control) >= 0) {
            line_to(p);  // a straight curve is a line
            return;
        }
        contours_.back().push_back({pen_, control, p, true, kWhite});
    }
    pen_ = p;
}

void Outline::cubic_to(glm::dvec2 c1, glm::dvec2 c2, glm::dvec2 p) {
    // Four pieces (de Casteljau at 1/4, 1/2, 3/4), each approximated by the quadratic curve whose
    // control point is (3 (c1 + c2) - p0 - p3) / 4.
    const glm::dvec2 p0 = pen_;
    const auto point = [&](double t) {
        const double u = 1 - t;
        return u * u * u * p0 + 3 * u * u * t * c1 + 3 * u * t * t * c2 + t * t * t * p;
    };
    const auto derivative = [&](double t) {
        const double u = 1 - t;
        return 3 * u * u * (c1 - p0) + 6 * u * t * (c2 - c1) + 3 * t * t * (p - c2);
    };
    constexpr int kPieces = 4;
    for (int i = 0; i < kPieces; ++i) {
        const double t0 = static_cast<double>(i) / kPieces;
        const double t1 = static_cast<double>(i + 1) / kPieces;
        const double h = (t1 - t0) / 3.0;
        const glm::dvec2 q0 = point(t0);
        const glm::dvec2 q3 = point(t1);
        const glm::dvec2 q1 = q0 + derivative(t0) * h;
        const glm::dvec2 q2 = q3 - derivative(t1) * h;
        quadratic_to((3.0 * (q1 + q2) - q0 - q3) / 4.0, q3);
    }
}

void Outline::close() {
    if (!open_) {
        return;
    }
    if (pen_ != start_) {
        line_to(start_);
    }
    open_ = false;
    if (contours_.back().empty()) {
        contours_.pop_back();
    }
}

bool Outline::empty() const {
    for (const Contour& c : contours_) {
        if (!c.empty()) {
            return false;
        }
    }
    return true;
}

void Outline::bounds(glm::dvec2& min, glm::dvec2& max) const {
    min = glm::dvec2(std::numeric_limits<double>::max());
    max = glm::dvec2(std::numeric_limits<double>::lowest());
    bool any = false;
    for (const Contour& c : contours_) {
        for (const Segment& s : c) {
            for (const glm::dvec2& p : {s.p0, s.p1, s.p2}) {
                min = glm::min(min, p);
                max = glm::max(max, p);
                any = true;
            }
        }
    }
    if (!any) {
        min = max = glm::dvec2(0.0);
    }
}

// --- Coloring -------------------------------------------------------------------------------------

void color_edges(Outline& outline, double angle) {
    const double cross_threshold = std::sin(angle);
    unsigned long long seed = 0;
    std::uint8_t color = kCyan;  // msdfgen's initial color for seed 0
    std::vector<int> corners;
    for (Contour& contour : outline.contours()) {
        if (contour.empty()) {
            continue;
        }
        corners.clear();
        glm::dvec2 previous = direction_at(contour.back(), 1);
        for (std::size_t i = 0; i < contour.size(); ++i) {
            if (is_corner(normalize_or_zero(previous), normalize_or_zero(direction_at(contour[i], 0)), cross_threshold)) {
                corners.push_back(static_cast<int>(i));
            }
            previous = direction_at(contour[i], 1);
        }

        if (corners.empty()) {  // smooth contour
            switch_color(color, seed);
            for (Segment& s : contour) {
                s.color = color;
            }
        } else if (corners.size() == 1) {  // "teardrop": three colors around it
            std::uint8_t colors[3];
            switch_color(color, seed);
            colors[0] = color;
            colors[1] = kWhite;
            switch_color(color, seed);
            colors[2] = color;
            const int corner = corners[0];
            const int m = static_cast<int>(contour.size());
            if (m >= 3) {
                for (int i = 0; i < m; ++i) {
                    contour[static_cast<std::size_t>((corner + i) % m)].color =
                        colors[1 + symmetrical_trichotomy(i, m)];
                }
            } else {
                // Fewer than three edges for three colors: split them.
                Segment parts[7];
                int count = 0;
                Segment thirds[3];
                split_in_thirds(contour[0], thirds);
                for (int k = 0; k < 3; ++k) parts[(corner == 0 ? 0 : 3) + k] = thirds[k];
                if (m >= 2) {
                    split_in_thirds(contour[1], thirds);
                    for (int k = 0; k < 3; ++k) parts[(corner == 0 ? 3 : 0) + k] = thirds[k];
                    parts[0].color = parts[1].color = colors[0];
                    parts[2].color = parts[3].color = colors[1];
                    parts[4].color = parts[5].color = colors[2];
                    count = 6;
                } else {
                    parts[0].color = colors[0];
                    parts[1].color = colors[1];
                    parts[2].color = colors[2];
                    count = 3;
                }
                contour.assign(parts, parts + count);
            }
        } else {  // several corners: a color change at each
            const int corner_count = static_cast<int>(corners.size());
            int spline = 0;
            const int start = corners[0];
            const int m = static_cast<int>(contour.size());
            switch_color(color, seed);
            const std::uint8_t initial = color;
            for (int i = 0; i < m; ++i) {
                const int index = (start + i) % m;
                if (spline + 1 < corner_count && corners[static_cast<std::size_t>(spline) + 1] == index) {
                    ++spline;
                    switch_color(color, seed, static_cast<std::uint8_t>(spline == corner_count - 1 ? initial : kBlack));
                }
                contour[static_cast<std::size_t>(index)].color = color;
            }
        }
    }
}

// --- Generation -----------------------------------------------------------------------------------

void generate(Outline& outline, const Placement& transform, int width, int height, std::uint8_t* rgba) {
    const std::size_t texels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (width <= 0 || height <= 0) {
        return;
    }

    // The outline in texel space (y up, texel centers at half units), reduced to the boundary of
    // the shape, then colored.
    std::vector<Contour> source;
    for (const Contour& c : outline.contours()) {
        if (c.empty()) {
            continue;
        }
        Contour& contour = source.emplace_back();
        for (const Segment& s : c) {
            const auto map = [&](glm::dvec2 p) { return (p + transform.translate) * transform.scale; };
            contour.push_back({map(s.p0), map(s.p1), map(s.p2), s.quadratic, kWhite});
        }
    }
    Outline boundary;
    boundary.contours() = shape_boundary(source);
    color_edges(boundary);
    const std::vector<Contour>& contours = boundary.contours();
    if (contours.empty()) {
        std::fill(rgba, rgba + texels * 4, std::uint8_t{0});
        return;
    }

    // The edges near each cell of 8 x 8 texels: farther ones only matter beyond the range (they
    // saturate), and the scanline pass gives those texels their sign.
    constexpr int kCell = 8;
    const int cells_x = (width + kCell - 1) / kCell;
    const int cells_y = (height + kCell - 1) / kCell;
    const double reach = transform.range;  // generous: pseudo-distances reach past the edges' ends
    std::vector<std::vector<const Segment*>> cells(static_cast<std::size_t>(cells_x) * static_cast<std::size_t>(cells_y));
    for (const Contour& contour : contours) {
        for (const Segment& seg : contour) {
            const glm::dvec2 lo = glm::min(glm::min(seg.p0, seg.p1), seg.p2) - reach;
            const glm::dvec2 hi = glm::max(glm::max(seg.p0, seg.p1), seg.p2) + reach;
            const int x0 = std::max(0, static_cast<int>(std::floor(lo.x / kCell)));
            const int y0 = std::max(0, static_cast<int>(std::floor(lo.y / kCell)));
            const int x1 = std::min(cells_x - 1, static_cast<int>(std::floor(hi.x / kCell)));
            const int y1 = std::min(cells_y - 1, static_cast<int>(std::floor(hi.y / kCell)));
            for (int cy = y0; cy <= y1; ++cy) {
                for (int cx = x0; cx <= x1; ++cx) {
                    cells[static_cast<std::size_t>(cy) * static_cast<std::size_t>(cells_x) + static_cast<std::size_t>(cx)]
                        .push_back(&seg);
                }
            }
        }
    }

    // Distances, in range units: 0.5 on the edge. Stored rows bottom to top while computing.
    std::vector<float> field(texels * 4);
    const double inv_range = 1.0 / transform.range;
    for (int row = 0; row < height; ++row) {
        for (int x = 0; x < width; ++x) {
            const glm::dvec2 p(x + 0.5, row + 0.5);
            Selector selector;
            for (const Segment* seg : cells[static_cast<std::size_t>(row / kCell) * static_cast<std::size_t>(cells_x) +
                                            static_cast<std::size_t>(x / kCell)]) {
                selector.add(*seg, p);
            }
            const MultiDistance d = selector.distance(p);
            float* out = &field[(static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
            const auto normalized = [&](double v) {
                return static_cast<float>(std::clamp(v * inv_range + 0.5, -1.0, 2.0));
            };
            out[0] = normalized(d.r);
            out[1] = normalized(d.g);
            out[2] = normalized(d.b);
            out[3] = normalized(d.a);
        }
    }

    // The sign from the fill: a texel inside the shape whose median says outside (or the reverse)
    // is flipped. Settles what the combiner could not see (contours beyond the range, overlaps).
    const std::vector<bool> filled = fill_mask(contours, width, height);
    for (std::size_t i = 0; i < texels; ++i) {
        float* t = &field[i * 4];
        const float m = static_cast<float>(median(t[0], t[1], t[2]));
        const bool inside = m > 0.5f;
        const bool near_edge = std::fabs(m - 0.5f) * transform.range < 0.1;
        if (inside != filled[i] && !near_edge) {
            t[0] = 1.0f - t[0];
            t[1] = 1.0f - t[1];
            t[2] = 1.0f - t[2];
        }
        const bool true_inside = t[3] > 0.5f;
        if (true_inside != filled[i] && std::fabs(t[3] - 0.5f) * transform.range >= 0.1) {
            t[3] = 1.0f - t[3];
        }
    }

    // Clashing texels get the median in all three channels.
    const auto threshold = static_cast<float>(1.001 / transform.range);
    std::vector<std::size_t> clashes;
    for (int row = 0; row < height; ++row) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const float* t = &field[i * 4];
            if ((x > 0 && detect_clash(t, &field[(i - 1) * 4], threshold)) ||
                (x < width - 1 && detect_clash(t, &field[(i + 1) * 4], threshold)) ||
                (row > 0 && detect_clash(t, &field[(i - static_cast<std::size_t>(width)) * 4], threshold)) ||
                (row < height - 1 && detect_clash(t, &field[(i + static_cast<std::size_t>(width)) * 4], threshold))) {
                clashes.push_back(i);
            }
        }
    }
    for (const std::size_t i : clashes) {
        float* t = &field[i * 4];
        const auto m = static_cast<float>(median(t[0], t[1], t[2]));
        t[0] = t[1] = t[2] = m;
    }

    for (int row = 0; row < height; ++row) {
        const std::size_t out_row = static_cast<std::size_t>(height - 1 - row);
        for (int x = 0; x < width; ++x) {
            const float* t = &field[(static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
            std::uint8_t* out = rgba + (out_row * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
            for (int c = 0; c < 4; ++c) {
                out[c] = static_cast<std::uint8_t>(std::lround(std::clamp(t[c], 0.0f, 1.0f) * 255.0f));
            }
        }
    }
}

}  // namespace moteur::msdf
