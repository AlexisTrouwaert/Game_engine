#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/gpu_resource.hpp"

namespace moteur {

class Renderer;

// One vertex of a 3D mesh, as the GPU reads it: 48 bytes. Skinning attributes (animation) will be
// added by milestone 5.
struct Vertex3D {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    // For normal maps: xyz points along increasing u, w (+1 or -1) says which way the bitangent
    // goes: bitangent = cross(normal, tangent.xyz) * w, pointing up in the texture image (the
    // "green up" convention of glTF normal maps).
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

// A mesh on the CPU: plain data that can be built, tested and inspected without a GPU.
//
// World conventions (see milestone 3, part 2): right-handed, Y up, 1 unit = 1 metre. Triangles are
// counter-clockwise when seen from the side their normal points to (the glTF convention), so
// back-face culling removes the inside of closed shapes.
struct MeshData {
    std::vector<Vertex3D> vertices;
    std::vector<std::uint32_t> indices;  // three per triangle

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
    std::uint32_t index_count = 0;
    Aabb bounds;

    // Uploads `data` and waits for the GPU (loading time, not inside a frame). `name` labels the
    // buffers for graphics debuggers. Throws std::invalid_argument for an empty mesh, and
    // std::runtime_error if the GPU refuses the buffers.
    static Mesh create(Renderer& renderer, const MeshData& data, const char* name = nullptr);
};

}  // namespace moteur
