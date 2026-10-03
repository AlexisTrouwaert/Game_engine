#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/gpu_resource.hpp"

namespace moteur {

class Renderer;

// One vertex of a 3D mesh, as the GPU reads it: 48 bytes. A skinned mesh adds a VertexSkin per
// vertex, in a buffer of its own (milestone 5).
struct Vertex3D {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    // For normal maps: xyz points along increasing u, w (+1 or -1) says which way the bitangent
    // goes: bitangent = cross(normal, tangent.xyz) * w, pointing up in the texture image (the
    // "green up" convention of glTF normal maps).
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

// How a vertex of a skinned mesh follows the skeleton: up to four joints (indices in its skin's
// palette, see SkinData) and their weights, in 65535ths, which add up to exactly 65535. Unused
// entries have a weight of 0.
struct VertexSkin {
    glm::u8vec4 joints{0};
    glm::u16vec4 weights{65535, 0, 0, 0};

    bool operator==(const VertexSkin&) const = default;
};
static_assert(sizeof(VertexSkin) == 12, "VertexSkin is uploaded as is: joints at 0, weights at 4");

// A mesh on the CPU: plain data that can be built, tested and inspected without a GPU.
//
// World conventions (see milestone 3, part 2): right-handed, Y up, 1 unit = 1 metre. Triangles are
// counter-clockwise when seen from the side their normal points to (the glTF convention), so
// back-face culling removes the inside of closed shapes.
struct MeshData {
    std::vector<Vertex3D> vertices;
    std::vector<std::uint32_t> indices;  // three per triangle
    std::vector<VertexSkin> skin;        // one per vertex for a skinned mesh, else empty

    std::size_t triangle_count() const { return indices.size() / 3; }
    Aabb bounds() const;
};

// Fills every vertex's tangent from positions, normals and texture coordinates, with MikkTSpace
// (the algorithm Blender and most tools bake normal maps with, so the maps match). Vertices shared
// by triangles that disagree keep the tangent of the last one: meshes exported from Blender are
// already split along texture seams, where that matters.
void compute_tangents(MeshData& mesh);

// Primitives, centred on the origin, for tests and placeholders. Each face has its own vertices,
// so edges stay sharp (normals are not averaged across faces).
MeshData make_cube(glm::vec3 size = glm::vec3(1.0f));
// A flat rectangle on the ground (y = 0), facing up, `size.x` along X and `size.y` along Z.
MeshData make_plane(glm::vec2 size = glm::vec2(1.0f));
// A UV sphere: `segments` around the vertical axis (at least 3), `rings` from pole to pole (at least 2).
MeshData make_sphere(float radius = 0.5f, int segments = 32, int rings = 16);

// A mesh on the GPU: its vertex and index buffers, uploaded once. Moves, does not copy.
struct Mesh {
    GpuBuffer vertices;
    GpuBuffer indices;       // 32-bit
    GpuBuffer skin;          // one VertexSkin per vertex, for a skinned mesh (MeshData::skin)
    std::uint32_t index_count = 0;
    Aabb bounds;
    std::size_t gpu_bytes = 0;  // vertices, indices and skin

    bool skinned() const { return static_cast<bool>(skin); }

    // Uploads `data` and waits for the GPU (loading time, not inside a frame). `name` labels the
    // buffers for graphics debuggers. Throws std::invalid_argument for an empty mesh or a skin that
    // does not have one entry per vertex, and std::runtime_error if the GPU refuses the buffers.
    static Mesh create(Renderer& renderer, const MeshData& data, const char* name = nullptr);
};

}  // namespace moteur
