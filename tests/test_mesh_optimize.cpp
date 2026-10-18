#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <vector>

#include "moteur/mesh.hpp"
#include "moteur/mesh_optimize.hpp"

// Milestone 9, part 4: levels of detail and index orders made at import.

namespace {

std::vector<glm::vec3> positions_of(const moteur::MeshData& mesh) {
    std::vector<glm::vec3> out;
    for (const moteur::Vertex3D& v : mesh.vertices) {
        out.push_back(v.position);
    }
    return out;
}

// A closed, smooth sphere without seams: one vertex per position (the poles shared).
moteur::MeshData seamless_sphere(int segments, int rings) {
    moteur::MeshData mesh;
    mesh.vertices.push_back({{0.0f, 1.0f, 0.0f}});
    for (int r = 1; r < rings; ++r) {
        const float phi = 3.14159265f * static_cast<float>(r) / static_cast<float>(rings);
        for (int s = 0; s < segments; ++s) {
            const float theta = 2.0f * 3.14159265f * static_cast<float>(s) / static_cast<float>(segments);
            mesh.vertices.push_back({{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)}});
        }
    }
    mesh.vertices.push_back({{0.0f, -1.0f, 0.0f}});
    const auto ring = [&](int r, int s) { return static_cast<std::uint32_t>(1 + (r - 1) * segments + (s % segments)); };
    const auto south = static_cast<std::uint32_t>(mesh.vertices.size() - 1);
    for (int s = 0; s < segments; ++s) {
        mesh.indices.insert(mesh.indices.end(), {0u, ring(1, s + 1), ring(1, s)});
        mesh.indices.insert(mesh.indices.end(), {south, ring(rings - 1, s), ring(rings - 1, s + 1)});
    }
    for (int r = 1; r < rings - 1; ++r) {
        for (int s = 0; s < segments; ++s) {
            mesh.indices.insert(mesh.indices.end(), {ring(r, s), ring(r, s + 1), ring(r + 1, s + 1)});
            mesh.indices.insert(mesh.indices.end(), {ring(r, s), ring(r + 1, s + 1), ring(r + 1, s)});
        }
    }
    return mesh;
}

// A flat square grid of n x n quads.
moteur::MeshData grid(int n) {
    moteur::MeshData mesh;
    for (int z = 0; z <= n; ++z) {
        for (int x = 0; x <= n; ++x) {
            mesh.vertices.push_back({{static_cast<float>(x), 0.0f, static_cast<float>(z)}});
        }
    }
    const auto at = [n](int x, int z) { return static_cast<std::uint32_t>(z * (n + 1) + x); };
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            mesh.indices.insert(mesh.indices.end(), {at(x, z), at(x, z + 1), at(x + 1, z + 1), at(x, z), at(x + 1, z + 1), at(x + 1, z)});
        }
    }
    return mesh;
}

}  // namespace

TEST_CASE("simplify: a sphere to a quarter of its triangles, within its error, winding kept") {
    const moteur::MeshData sphere = seamless_sphere(48, 24);
    const std::vector<glm::vec3> positions = positions_of(sphere);
    const moteur::SimplifyResult result = moteur::simplify(positions, sphere.indices, sphere.indices.size() / 4, 0.2f);
    CHECK(result.indices.size() <= sphere.indices.size() / 4 + 6);
    CHECK(result.indices.size() > sphere.indices.size() / 8);
    CHECK(result.error > 0.0f);
    CHECK(result.error <= 0.2f);
    // Every triangle still faces out (the sphere is centred on the origin).
    for (std::size_t t = 0; t < result.indices.size(); t += 3) {
        const glm::vec3 a = positions[result.indices[t]], b = positions[result.indices[t + 1]], c = positions[result.indices[t + 2]];
        CAPTURE(result.indices[t]);
        CAPTURE(result.indices[t + 1]);
        CAPTURE(result.indices[t + 2]);
        CHECK(glm::dot(glm::cross(b - a, c - a), a + b + c) > 0.0f);
    }
    // The same result every time.
    CHECK(moteur::simplify(positions, sphere.indices, sphere.indices.size() / 4, 0.2f).indices == result.indices);
    // A tight error stops early.
    const moteur::SimplifyResult tight = moteur::simplify(positions, sphere.indices, 0, 0.001f);
    CHECK(tight.indices.size() > result.indices.size());
}

TEST_CASE("simplify: borders and seams stay where they are") {
    const moteur::MeshData flat = grid(16);
    const std::vector<glm::vec3> positions = positions_of(flat);
    const moteur::SimplifyResult result = moteur::simplify(positions, flat.indices, 0, 0.01f);
    // A flat grid loses its inner vertices for free, but keeps its 64 border vertices.
    CHECK(result.indices.size() < flat.indices.size() / 4);
    std::set<std::uint32_t> used(result.indices.begin(), result.indices.end());
    for (std::uint32_t v = 0; v < positions.size(); ++v) {
        const glm::vec3 p = positions[v];
        if (p.x == 0.0f || p.z == 0.0f || p.x == 16.0f || p.z == 16.0f) {
            CHECK(used.contains(v));
        }
    }
    // The engine's sphere has a seam (doubled vertices along u = 0): it simplifies, the seam stays.
    const moteur::MeshData uv_sphere = moteur::make_sphere(1.0f, 32, 16);
    const moteur::SimplifyResult seamed = moteur::simplify(positions_of(uv_sphere), uv_sphere.indices, 0, 0.05f);
    CHECK(seamed.indices.size() < uv_sphere.indices.size());
}

TEST_CASE("vertex cache: Forsyth's order shades fewer vertices; fetch order") {
    moteur::MeshData mesh = grid(40);
    // A bad order to start with: the triangles shuffled deterministically.
    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        triangles.push_back({mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]});
    }
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        std::swap(triangles[i], triangles[(i * 7919) % triangles.size()]);
    }
    std::vector<std::uint32_t> indices;
    for (const auto& t : triangles) {
        indices.insert(indices.end(), t.begin(), t.end());
    }
    const float before = moteur::average_cache_miss_ratio(indices, mesh.vertices.size());
    std::vector<std::uint32_t> optimized = indices;
    moteur::optimize_vertex_cache(optimized, mesh.vertices.size());
    const float after = moteur::average_cache_miss_ratio(optimized, mesh.vertices.size());
    MESSAGE("ACMR " << before << " -> " << after);
    CHECK(after < 0.8f);
    CHECK(after < before);
    // The same triangles, each with its winding.
    std::multiset<std::array<std::uint32_t, 3>> a, b;
    const auto canonical = [](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        std::array<std::uint32_t, 3> t{x, y, z};
        std::rotate(t.begin(), std::min_element(t.begin(), t.end()), t.end());
        return t;
    };
    for (std::size_t t = 0; t < indices.size(); t += 3) {
        a.insert(canonical(indices[t], indices[t + 1], indices[t + 2]));
        b.insert(canonical(optimized[t], optimized[t + 1], optimized[t + 2]));
    }
    CHECK(a == b);

    const std::vector<std::uint32_t> order = moteur::vertex_fetch_order(optimized, mesh.vertices.size() + 2);
    CHECK(order.size() == mesh.vertices.size() + 2);
    CHECK(order.front() == optimized.front());
    CHECK(order[order.size() - 2] == mesh.vertices.size());  // unused ones last, in order
}
