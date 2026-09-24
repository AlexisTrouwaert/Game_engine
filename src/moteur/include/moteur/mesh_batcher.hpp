#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/material.hpp"

namespace moteur {

struct Mesh;

// One recorded draw: a mesh placed in the world with its material, and its box in the world.
struct MeshDraw {
    const Mesh* mesh = nullptr;
    glm::mat4 world{1.0f};
    Material material;
    Aabb bounds;  // the mesh's box moved by `world` (transform_box)
};

// What one instance sends to the GPU, laid out exactly as the per-instance vertex attributes of
// mesh.vert.hlsl (locations 4 to 12) and shadow.vert.hlsl (the world rows, locations 1 to 3) read it. Matrices are stored as rows
// and only the three first: the last row of an object's matrix is always (0, 0, 0, 1).
struct MeshInstance {
    glm::vec4 world[3];          // rows of the object -> world matrix
    glm::vec4 normal_matrix[3];  // rows of its inverse transpose (xyz used)
    glm::vec4 base_color;        // linear
    glm::vec4 factors;           // metallic, roughness, normal scale, occlusion strength
    glm::vec4 emissive;          // rgb
};
static_assert(sizeof(MeshInstance) == 144, "MeshInstance is uploaded as is: keep it tightly packed");

// Draws that share their mesh, their textures and their faces (one or both sides): one instanced
// draw call. The colors and factors of the material travel with each instance, so they never split
// a batch. `material` is the one of the first draw of the batch (for its textures and sides).
struct MeshBatch {
    const Mesh* mesh = nullptr;
    const Material* material = nullptr;
    std::uint32_t first_instance = 0;  // in instances()
    std::uint32_t count = 0;
};

// Turns the draws of a frame into what one pass needs: the visible ones, grouped into batches, and
// their instance data. Plain CPU code, testable without a GPU (like SpriteBatcher).
//
// Batches come in a fixed order: single-sided first, then double-sided (one pipeline change), and
// otherwise in the order in which their first draw was recorded, visible or not (so the order does
// not change as objects enter or leave the view); inside a batch, draws keep their recording order.
// The result is the same on every run: no sorting on addresses.
class MeshBatcher {
public:
    enum class Pass {
        Main,    // what the camera sees, with every material
        Shadow,  // depth from the sun: only the draws that cast shadows, grouped by mesh and sides alone
    };

    // Keeps the draws whose box meets `frustum` (and, for Pass::Shadow, that cast shadows).
    // `draws` must outlive the batches (they point into it).
    void build(const std::vector<MeshDraw>& draws, const Frustum& frustum, Pass pass);
    // The same, looking only at the draws listed in `subset` (indices into `draws`, increasing):
    // for example those inside a point light's sphere.
    void build(const std::vector<MeshDraw>& draws, const std::vector<std::uint32_t>& subset, const Frustum& frustum,
               Pass pass);

    const std::vector<MeshInstance>& instances() const { return instances_; }
    const std::vector<MeshBatch>& batches() const { return batches_; }
    std::size_t submitted() const { return submitted_; }  // draws considered (casters only, for shadows)
    std::size_t visible() const { return instances_.size(); }
    std::size_t triangles() const { return triangles_; }
    // Indices (into the draws given to build()) of the draws kept, in the order they were given.
    const std::vector<std::uint32_t>& visible_draws() const { return visible_; }

    // The instance data of one draw.
    static MeshInstance make_instance(const MeshDraw& draw);

private:
    // What a batch shares. Unused textures stay null in the shadow pass.
    struct Key {
        const void* pointers[6];  // the mesh, then the five material textures
        bool double_sided;
        bool operator==(const Key& other) const;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };

    void build(const std::vector<MeshDraw>& draws, const std::vector<std::uint32_t>* subset, const Frustum& frustum,
               Pass pass);

    std::vector<MeshInstance> instances_;
    std::vector<MeshBatch> batches_;
    std::size_t submitted_ = 0;
    std::size_t triangles_ = 0;
    // Scratch space, kept between frames to avoid allocations.
    std::vector<std::uint32_t> group_of_;   // per visible draw: its group
    std::vector<std::uint32_t> visible_;    // indices of visible draws
    std::vector<std::uint32_t> cursor_;     // per group, then per batch (see build())
    std::vector<Key> keys_;                 // per group, in order of first appearance
    std::vector<std::uint32_t> first_draw_; // per group: its first draw (for the batch's material)
    std::vector<std::uint32_t> order_;      // per group: its place among the batches
    std::unordered_map<Key, std::uint32_t, KeyHash> groups_;
};

}  // namespace moteur
