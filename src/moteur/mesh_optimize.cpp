#include "moteur/mesh_optimize.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <iterator>
#include <map>
#include <queue>
#include <tuple>
#include <utility>

namespace moteur {

// --- Simplification ------------------------------------------------------------------------------

namespace {

// A symmetric 4 x 4 quadric (10 numbers), in double: the sum of the squared distances to planes.
struct Quadric {
    double a2 = 0, ab = 0, ac = 0, ad = 0, b2 = 0, bc = 0, bd = 0, c2 = 0, cd = 0, d2 = 0;

    static Quadric plane(const glm::dvec3& n, double d, double weight) {
        Quadric q;
        q.a2 = n.x * n.x * weight;
        q.ab = n.x * n.y * weight;
        q.ac = n.x * n.z * weight;
        q.ad = n.x * d * weight;
        q.b2 = n.y * n.y * weight;
        q.bc = n.y * n.z * weight;
        q.bd = n.y * d * weight;
        q.c2 = n.z * n.z * weight;
        q.cd = n.z * d * weight;
        q.d2 = d * d * weight;
        return q;
    }
    void add(const Quadric& o) {
        a2 += o.a2; ab += o.ab; ac += o.ac; ad += o.ad; b2 += o.b2;
        bc += o.bc; bd += o.bd; c2 += o.c2; cd += o.cd; d2 += o.d2;
    }
    double error(const glm::dvec3& p) const {
        const double e = a2 * p.x * p.x + 2 * ab * p.x * p.y + 2 * ac * p.x * p.z + 2 * ad * p.x + b2 * p.y * p.y +
                         2 * bc * p.y * p.z + 2 * bd * p.y + c2 * p.z * p.z + 2 * cd * p.z + d2;
        return std::max(e, 0.0);
    }
};

struct Collapse {
    double cost;
    std::uint32_t from;
    std::uint32_t to;
    std::uint32_t version;  // of `from` when queued: a later change of it makes this one stale
    bool operator>(const Collapse& o) const {
        return std::tie(cost, from, to) > std::tie(o.cost, o.from, o.to);
    }
};

glm::dvec3 triangle_normal(const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) {
    return glm::cross(b - a, c - a);
}

}  // namespace

SimplifyResult simplify(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                        std::size_t target_index_count, float max_error) {
    SimplifyResult result;
    const std::size_t vertex_count = positions.size();
    const std::size_t triangle_count = indices.size() / 3;
    std::vector<std::array<std::uint32_t, 3>> triangles(triangle_count);
    for (std::size_t t = 0; t < triangle_count; ++t) {
        triangles[t] = {indices[t * 3], indices[t * 3 + 1], indices[t * 3 + 2]};
    }
    std::vector<bool> alive(triangle_count, true);
    std::size_t alive_count = triangle_count;
    if (triangle_count == 0 || target_index_count >= indices.size()) {
        result.indices.assign(indices.begin(), indices.end());
        return result;
    }

    // Positions shared by several vertices (seams) and open borders lock their vertices.
    std::vector<bool> locked(vertex_count, false);
    {
        std::map<std::tuple<float, float, float>, std::uint32_t> first;
        for (std::uint32_t v = 0; v < vertex_count; ++v) {
            const glm::vec3& p = positions[v];
            const auto [it, added] = first.emplace(std::make_tuple(p.x, p.y, p.z), v);
            if (!added) {
                locked[v] = true;
                locked[it->second] = true;
            }
        }
        std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
        for (const auto& t : triangles) {
            for (int e = 0; e < 3; ++e) {
                const std::uint32_t a = t[static_cast<std::size_t>(e)];
                const std::uint32_t b = t[static_cast<std::size_t>((e + 1) % 3)];
                ++edges[{std::min(a, b), std::max(a, b)}];
            }
        }
        for (const auto& [edge, uses] : edges) {
            if (uses != 2) {  // an open border, or a non-manifold edge: left as it is
                locked[edge.first] = true;
                locked[edge.second] = true;
            }
        }
    }

    // The triangles around each vertex, and each vertex's quadric.
    std::vector<std::vector<std::uint32_t>> around(vertex_count);
    std::vector<Quadric> quadrics(vertex_count);
    std::vector<glm::dvec3> original(triangle_count, glm::dvec3(0.0));  // each triangle's first unit normal
    for (std::uint32_t t = 0; t < triangle_count; ++t) {
        const auto& tri = triangles[t];
        const glm::dvec3 a(positions[tri[0]]), b(positions[tri[1]]), c(positions[tri[2]]);
        const glm::dvec3 n = triangle_normal(a, b, c);
        const double length = glm::length(n);
        if (length > 0.0) {
            original[t] = n / length;
            const glm::dvec3 unit = n / length;
            const Quadric q = Quadric::plane(unit, -glm::dot(unit, a), length * 0.5);  // weighted by area
            for (const std::uint32_t v : tri) {
                quadrics[v].add(q);
            }
        }
        for (const std::uint32_t v : tri) {
            around[v].push_back(t);
        }
    }

    std::vector<std::uint32_t> version(vertex_count, 0);
    std::vector<bool> removed(vertex_count, false);
    std::priority_queue<Collapse, std::vector<Collapse>, std::greater<>> queue;
    const auto neighbours = [&](std::uint32_t v) {
        std::vector<std::uint32_t> out;
        for (const std::uint32_t t : around[v]) {
            if (!alive[t]) {
                continue;
            }
            for (const std::uint32_t w : triangles[t]) {
                if (w != v) {
                    out.push_back(w);
                }
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    };
    const auto push = [&](std::uint32_t from) {
        if (locked[from] || removed[from]) {
            return;
        }
        for (const std::uint32_t to : neighbours(from)) {
            Quadric q = quadrics[from];
            q.add(quadrics[to]);
            queue.push({q.error(glm::dvec3(positions[to])), from, to, version[from]});
        }
    };
    for (std::uint32_t v = 0; v < vertex_count; ++v) {
        push(v);
    }

    const double limit = static_cast<double>(max_error) * static_cast<double>(max_error);
    double worst = 0.0;
    const std::size_t target_triangles = target_index_count / 3;
    while (alive_count > target_triangles && !queue.empty()) {
        const Collapse c = queue.top();
        queue.pop();
        if (c.cost > limit) {
            break;
        }
        if (removed[c.from] || removed[c.to] || c.version != version[c.from]) {
            continue;
        }
        // Still an edge? The link condition: the vertices both share must be exactly the third
        // vertices of the triangles on the edge (otherwise the collapse pinches the surface).
        std::vector<std::uint32_t> on_edge;
        for (const std::uint32_t t : around[c.from]) {
            if (alive[t] && std::find(triangles[t].begin(), triangles[t].end(), c.to) != triangles[t].end()) {
                on_edge.push_back(t);
            }
        }
        if (on_edge.empty()) {
            continue;
        }
        std::vector<std::uint32_t> opposite;
        for (const std::uint32_t t : on_edge) {
            for (const std::uint32_t w : triangles[t]) {
                if (w != c.from && w != c.to) {
                    opposite.push_back(w);
                }
            }
        }
        std::sort(opposite.begin(), opposite.end());
        const std::vector<std::uint32_t> from_ring = neighbours(c.from);
        const std::vector<std::uint32_t> to_ring = neighbours(c.to);
        std::vector<std::uint32_t> common;
        std::set_intersection(from_ring.begin(), from_ring.end(), to_ring.begin(), to_ring.end(), std::back_inserter(common));
        if (common != opposite) {
            continue;
        }
        // No triangle may flip or collapse to nothing.
        bool flips = false;
        const glm::dvec3 target(positions[c.to]);
        for (const std::uint32_t t : around[c.from]) {
            if (!alive[t] || std::find(on_edge.begin(), on_edge.end(), t) != on_edge.end()) {
                continue;
            }
            std::array<glm::dvec3, 3> before, after;
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t w = triangles[t][static_cast<std::size_t>(k)];
                before[static_cast<std::size_t>(k)] = glm::dvec3(positions[w]);
                after[static_cast<std::size_t>(k)] = w == c.from ? target : glm::dvec3(positions[w]);
            }
            const glm::dvec3 n0 = triangle_normal(before[0], before[1], before[2]);
            const glm::dvec3 n1 = triangle_normal(after[0], after[1], after[2]);
            const double l0 = glm::length(n0), l1 = glm::length(n1);
            // Nor turn away from where it faced at first: the drift of many small collapses adds up.
            if (l1 <= 1e-12 * std::max(l0, 1e-30) || glm::dot(n0, n1) < 0.25 * l0 * l1 ||
                glm::dot(original[t], n1) < 0.5 * l1) {
                flips = true;
                break;
            }
            // Nor become a sliver (worse than it was): its quality, 1 for an equilateral triangle,
            // 2 sqrt(3) x area / sum of its squared edges.
            const auto quality = [](const std::array<glm::dvec3, 3>& p, double doubled_area) {
                const double edges = glm::dot(p[1] - p[0], p[1] - p[0]) + glm::dot(p[2] - p[1], p[2] - p[1]) +
                                     glm::dot(p[0] - p[2], p[0] - p[2]);
                return edges > 0.0 ? 3.4641016151377544 * doubled_area / edges : 0.0;
            };
            const double q1 = quality(after, l1);
            if (q1 < 0.08 && q1 < quality(before, l0)) {
                flips = true;
                break;
            }
        }
        if (flips) {
            continue;
        }
        // Collapse.
        for (const std::uint32_t t : on_edge) {
            alive[t] = false;
            --alive_count;
        }
        for (const std::uint32_t t : around[c.from]) {
            if (!alive[t]) {
                continue;
            }
            for (std::uint32_t& w : triangles[t]) {
                if (w == c.from) {
                    w = c.to;
                }
            }
            around[c.to].push_back(t);
        }
        removed[c.from] = true;
        quadrics[c.to].add(quadrics[c.from]);
        worst = std::max(worst, c.cost);
        // The costs around the target changed.
        ++version[c.to];
        push(c.to);
        for (const std::uint32_t w : neighbours(c.to)) {
            ++version[w];
            push(w);
        }
    }

    result.indices.reserve(alive_count * 3);
    for (std::size_t t = 0; t < triangle_count; ++t) {
        if (alive[t]) {
            result.indices.insert(result.indices.end(), triangles[t].begin(), triangles[t].end());
        }
    }
    result.error = static_cast<float>(std::sqrt(worst));
    return result;
}

// --- Vertex cache (Forsyth) ----------------------------------------------------------------------

namespace {

constexpr std::size_t kCacheSize = 32;

float cache_score(int position) {
    // Tom Forsyth, "Linear-Speed Vertex Cache Optimisation" (2006): the three most recent vertices
    // score the same (a triangle just drawn), then a power falloff.
    if (position < 0) {
        return 0.0f;
    }
    if (position < 3) {
        return 0.75f;
    }
    const float scaler = 1.0f / static_cast<float>(kCacheSize - 3);
    return std::pow(1.0f - static_cast<float>(position - 3) * scaler, 1.5f);
}

float valence_score(std::size_t remaining) {
    return remaining == 0 ? 0.0f : 2.0f * std::pow(static_cast<float>(remaining), -0.5f);
}

}  // namespace

void optimize_vertex_cache(std::vector<std::uint32_t>& indices, std::size_t vertex_count) {
    const std::size_t triangle_count = indices.size() / 3;
    if (triangle_count < 2) {
        return;
    }
    std::vector<std::vector<std::uint32_t>> triangles_of(vertex_count);
    for (std::uint32_t t = 0; t < triangle_count; ++t) {
        for (int k = 0; k < 3; ++k) {
            triangles_of[indices[t * 3 + static_cast<std::size_t>(k)]].push_back(t);
        }
    }
    std::vector<std::size_t> remaining(vertex_count);
    std::vector<int> cache_position(vertex_count, -1);
    std::vector<float> vertex_score(vertex_count);
    for (std::size_t v = 0; v < vertex_count; ++v) {
        remaining[v] = triangles_of[v].size();
        vertex_score[v] = valence_score(remaining[v]);
    }
    std::vector<float> triangle_score(triangle_count);
    std::vector<bool> drawn(triangle_count, false);
    for (std::size_t t = 0; t < triangle_count; ++t) {
        triangle_score[t] = vertex_score[indices[t * 3]] + vertex_score[indices[t * 3 + 1]] + vertex_score[indices[t * 3 + 2]];
    }
    std::vector<std::uint32_t> out;
    out.reserve(indices.size());
    std::deque<std::uint32_t> cache;
    std::size_t scan = 0;  // for the next best triangle when the cache gives none
    for (std::size_t done = 0; done < triangle_count; ++done) {
        // The best triangle among those of the vertices in the cache; else the next undrawn one.
        std::int64_t best = -1;
        float best_score = -1.0f;
        for (const std::uint32_t v : cache) {
            for (const std::uint32_t t : triangles_of[v]) {
                if (!drawn[t] && (triangle_score[t] > best_score ||
                                  (triangle_score[t] == best_score && static_cast<std::int64_t>(t) < best))) {
                    best_score = triangle_score[t];
                    best = t;
                }
            }
        }
        if (best < 0) {
            while (drawn[scan]) {
                ++scan;
            }
            best = static_cast<std::int64_t>(scan);
        }
        const auto t = static_cast<std::size_t>(best);
        drawn[t] = true;
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t v = indices[t * 3 + static_cast<std::size_t>(k)];
            out.push_back(v);
            --remaining[v];
            auto& list = triangles_of[v];
            list.erase(std::find(list.begin(), list.end(), static_cast<std::uint32_t>(t)));
            const auto found = std::find(cache.begin(), cache.end(), v);
            if (found != cache.end()) {
                cache.erase(found);
            }
            cache.push_front(v);
        }
        std::vector<std::uint32_t> touched(cache.begin(), cache.end());
        while (cache.size() > kCacheSize) {
            touched.push_back(cache.back());
            cache_position[cache.back()] = -1;
            cache.pop_back();
        }
        for (std::size_t i = 0; i < cache.size(); ++i) {
            cache_position[cache[i]] = static_cast<int>(i);
        }
        // New scores for the vertices that moved, and their triangles.
        for (const std::uint32_t v : touched) {
            const int position = std::find(cache.begin(), cache.end(), v) != cache.end() ? cache_position[v] : -1;
            vertex_score[v] = cache_score(position) + valence_score(remaining[v]);
        }
        for (const std::uint32_t v : touched) {
            for (const std::uint32_t other : triangles_of[v]) {
                triangle_score[other] = vertex_score[indices[other * 3]] + vertex_score[indices[other * 3 + 1]] +
                                        vertex_score[indices[other * 3 + 2]];
            }
        }
    }
    indices = std::move(out);
}

float average_cache_miss_ratio(std::span<const std::uint32_t> indices, std::size_t vertex_count, std::size_t cache_size) {
    if (indices.size() < 3) {
        return 0.0f;
    }
    std::vector<std::size_t> stamp(vertex_count, 0);  // the time it entered the FIFO, 0: not in it
    std::size_t time = 0;
    std::size_t misses = 0;
    for (const std::uint32_t v : indices) {
        if (stamp[v] == 0 || time - stamp[v] >= cache_size) {
            ++misses;
            ++time;
            stamp[v] = time;
        }
    }
    const std::size_t triangles = indices.size() / 3;  // whole triangles only
    return static_cast<float>(misses) / static_cast<float>(triangles);
}

std::vector<std::uint32_t> vertex_fetch_order(std::span<const std::uint32_t> indices, std::size_t vertex_count) {
    std::vector<std::uint32_t> order;
    order.reserve(vertex_count);
    std::vector<bool> seen(vertex_count, false);
    for (const std::uint32_t v : indices) {
        if (!seen[v]) {
            seen[v] = true;
            order.push_back(v);
        }
    }
    for (std::uint32_t v = 0; v < vertex_count; ++v) {
        if (!seen[v]) {
            order.push_back(v);
        }
    }
    return order;
}

}  // namespace moteur
