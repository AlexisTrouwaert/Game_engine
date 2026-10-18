#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace moteur {

// Mesh optimizations done when a model is imported (milestone 9, part 4; home-made, the library
// rule: meshoptimizer would be a proposal if these fall short). Plain CPU code on index lists,
// testable without a GPU.

/// What simplify() made.
struct SimplifyResult {
    std::vector<std::uint32_t> indices;  ///< the simplified triangles, into the same vertices
    float error = 0.0f;                  ///< the largest distance error accepted, in the units of the positions
};

/// Simplifies a triangle mesh by collapsing edges onto one of their ends (Garland and Heckbert's
/// quadric error metric, half-edge collapses: no vertex is created or moved, so the simplified
/// triangles index the same vertices, attributes included). Stops at `target_index_count` indices
/// or when the next collapse would move the surface by more than `max_error` (in the units of the
/// positions). Vertices that cannot move without tearing the mesh or its attributes stay: those on
/// an open border, and those whose position is shared by another vertex (a seam of texture
/// coordinates or normals, split at export). A collapse that would flip a triangle, or make the
/// surface non-manifold, is refused. Deterministic (ties broken by vertex index).
SimplifyResult simplify(std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
                        std::size_t target_index_count, float max_error);

/// Reorders the triangles of `indices` for the GPU's post-transform vertex cache (Tom Forsyth's
/// linear-speed algorithm, a cache of 32 entries): each vertex is shaded fewer times. The set of
/// triangles is unchanged; each keeps its winding. `vertex_count`: one more than the largest index.
void optimize_vertex_cache(std::vector<std::uint32_t>& indices, std::size_t vertex_count);

/// The average number of vertices shaded per triangle for a FIFO cache of `cache_size` entries
/// (ACMR: 3 without any reuse, 0.5 to 0.7 for a well-ordered regular grid). For tests and reports.
float average_cache_miss_ratio(std::span<const std::uint32_t> indices, std::size_t vertex_count, std::size_t cache_size = 32);

/// The order in which the vertices are first used by `indices`, followed by the unused ones: vertex
/// `order[i]` becomes vertex `i`. Reordering the vertices so (and every index list with the inverse
/// mapping) makes the GPU read the vertices in sequence (vertex fetch).
std::vector<std::uint32_t> vertex_fetch_order(std::span<const std::uint32_t> indices, std::size_t vertex_count);

}  // namespace moteur
