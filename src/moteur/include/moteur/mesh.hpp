#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/geometry_pool.hpp"
#include "moteur/gpu_resource.hpp"

namespace moteur {

class Renderer;

/// One vertex of a 3D mesh, as the GPU reads it: 48 bytes. A skinned mesh adds a VertexSkin per
/// vertex, in a buffer of its own (milestone 5).
struct Vertex3D {
    glm::vec3 position{0.0f};            ///< in the mesh's own space, in metres
    glm::vec3 normal{0.0f, 1.0f, 0.0f};  ///< unit length, pointing out of the front face
    glm::vec2 uv{0.0f};                  ///< texture coordinates, glTF convention: v = 0 at the top of the image
    /// For normal maps: xyz points along increasing u, w (+1 or -1) says which way the bitangent
    /// goes: bitangent = cross(normal, tangent.xyz) * w, pointing up in the texture image (the
    /// "green up" convention of glTF normal maps).
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

/// How a vertex of a skinned mesh follows the skeleton: up to four joints (indices in its skin's
/// palette, see SkinData) and their weights, in 65535ths, which add up to exactly 65535. Unused
/// entries have a weight of 0.
struct VertexSkin {
    glm::u8vec4 joints{0};                 ///< four indices into the skin's palette (0 to 255)
    glm::u16vec4 weights{65535, 0, 0, 0};  ///< the weight of each joint, in 65535ths; default: all on the first

    /// Same joints and same weights, member by member.
    bool operator==(const VertexSkin&) const = default;
};
static_assert(sizeof(VertexSkin) == 12, "VertexSkin is uploaded as is: joints at 0, weights at 4");

/// A mesh on the CPU: plain data that can be built, tested and inspected without a GPU.
///
/// World conventions (see milestone 3, part 2): right-handed, Y up, 1 unit = 1 metre. Triangles are
/// counter-clockwise when seen from the side their normal points to (the glTF convention), so
/// back-face culling removes the inside of closed shapes.
struct MeshData {
    std::vector<Vertex3D> vertices;      ///< indexed by `indices`
    std::vector<std::uint32_t> indices;  ///< three per triangle
    std::vector<VertexSkin> skin;        ///< one per vertex for a skinned mesh, else empty

    /// `indices.size() / 3`.
    std::size_t triangle_count() const { return indices.size() / 3; }
    /// The box around every vertex position (all of them, used by an index or not); an empty box
    /// without vertices. O(vertices).
    Aabb bounds() const;
};

/// Fills every vertex's tangent from positions, normals and texture coordinates, with MikkTSpace
/// (the algorithm Blender and most tools bake normal maps with, so the maps match). Vertices shared
/// by triangles that disagree keep the tangent of the last one: meshes exported from Blender are
/// already split along texture seams, where that matters. Does nothing without a whole triangle.
void compute_tangents(MeshData& mesh);

/// A box, centred on the origin, `size` metres along X, Y and Z: 24 vertices, 12 triangles, tangents
/// computed. Primitives like this one are for tests and placeholders. Each face has its own
/// vertices, so edges stay sharp (normals are not averaged across faces); each face maps the whole
/// texture.
MeshData make_cube(glm::vec3 size = glm::vec3(1.0f));
/// A flat rectangle on the ground (y = 0), centred on the origin, facing up, `size.x` metres along
/// X and `size.y` along Z; the whole texture once, tangents computed.
MeshData make_plane(glm::vec2 size = glm::vec2(1.0f));
/// A UV sphere centred on the origin, `radius` in metres: `segments` around the vertical axis
/// (raised to 3 if fewer), `rings` from pole to pole (raised to 2). u goes once around, v from the
/// top pole (0) to the bottom one (1); the seam's vertices are doubled. Tangents computed.
MeshData make_sphere(float radius = 0.5f, int segments = 32, int rings = 16);

/// A mesh on the GPU: its vertices and indices, uploaded once, in ranges of the renderer's
/// geometry pools (see GeometryPool). Moves, does not copy. Like every GPU resource, it must be
/// destroyed before the Renderer that created it.
struct Mesh {
    GpuSlice vertices;       ///< Vertex3D, one after the other
    GpuSlice indices;        ///< 32-bit
    GpuSlice skin;           ///< one VertexSkin per vertex, for a skinned mesh (MeshData::skin)
    std::uint32_t index_count = 0;  ///< three per triangle
    Aabb bounds;             ///< of the vertices, in the mesh's own space (MeshData::bounds())
    std::size_t gpu_bytes = 0;  ///< vertices, indices and skin

    /// Whether it has a skin (MeshData::skin was not empty): drawn with a palette, see
    /// MeshRenderer::add_palette().
    bool skinned() const { return static_cast<bool>(skin); }

    /// Uploads `data` with the renderer's pending uploads (sent before the next frame); neither
    /// submits nor waits. `name` is only used in error messages (the pools' buffers are shared, so
    /// not named per mesh): every message starts with "Mesh 'name'", or "Mesh (unnamed)" for a null
    /// or empty `name`. Throws std::invalid_argument for an empty mesh or a skin that does not
    /// have one entry per vertex (before anything is staged), and std::runtime_error if the GPU
    /// refuses the buffers. Main thread (the renderer's).
    static Mesh create(Renderer& renderer, const MeshData& data, const char* name = nullptr);
};

}  // namespace moteur
