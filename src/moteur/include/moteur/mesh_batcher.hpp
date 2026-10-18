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

/// One recorded draw: a mesh placed in the world with its material, and its box in the world.
/// A skinned draw also names its palette: the skinning matrices of its pose, recorded for the frame
/// (MeshRenderer::add_palette); its vertices are first moved by them, then by `world`.
struct MeshDraw {
    const Mesh* mesh = nullptr;  ///< not owned: must stay alive until the frame is drawn
    glm::mat4 world{1.0f};       ///< object -> world
    DrawMaterial material;       ///< a copy (its textures are not owned)
    Aabb bounds;  ///< where the mesh is in the world (for a skinned one: in its pose)
    std::int32_t palette = -1;       ///< first matrix in the frame's palettes; -1: not skinned
    std::uint32_t palette_size = 0;  ///< matrices
    std::uint8_t lod = 0;            ///< its level of detail (index in Mesh::lods), chosen by the renderer
    /// What the id pass writes for its pixels (milestone 9, part 7): the selectable entity it shows,
    /// as World::pick_id_of() makes it; 0: nothing to select (it still hides what is behind it).
    std::uint32_t pick_id = 0;
};

/// What one instance sends to the GPU, laid out exactly as the per-instance vertex attributes of
/// mesh.vert.hlsl (locations 4 to 12) and shadow.vert.hlsl (the world rows, locations 1 to 3) read it. Matrices are stored as rows
/// and only the three first: the last row of an object's matrix is always (0, 0, 0, 1).
struct MeshInstance {
    glm::vec4 world[3];          ///< rows of the object -> world matrix
    glm::vec4 normal_matrix[3];  ///< rows of its inverse transpose (xyz); [0].w: the alpha cutoff; [1].w: MeshDraw::pick_id (a whole float)
    glm::vec4 base_color;        ///< linear
    glm::vec4 factors;           ///< metallic, roughness, normal scale, occlusion strength
    /// rgb: linear emitted light; w: first palette matrix of a skinned draw (see skinning.hlsli), else
    /// -1 for a material that fades (Material::fades) and 0 otherwise.
    glm::vec4 emissive;
};
static_assert(sizeof(MeshInstance) == 144, "MeshInstance is uploaded as is: keep it tightly packed");

/// Draws that share their mesh, their textures and their faces (one or both sides): one instanced
/// draw call. The colors and factors of the material travel with each instance, so they never split
/// a batch. `material` is the one of the first draw of the batch (for its textures and sides).
struct MeshBatch {
    const Mesh* mesh = nullptr;          ///< drawn by every instance of the batch
    const DrawMaterial* material = nullptr;  ///< points into the draws given to MeshBatcher::build()
    std::uint32_t first_instance = 0;  ///< in instances()
    std::uint32_t count = 0;           ///< instances, one per visible draw
    bool skinned = false;  ///< its draws have palettes (a skinned mesh drawn without one is a still mesh)
    bool masked = false;   ///< alpha-tested material (Material::alpha_cutoff > 0)
    std::uint8_t lod = 0;  ///< the level of detail of its draws (Mesh::lods)
};

/// Turns the draws of a frame into what one pass needs: the visible ones, grouped into batches, and
/// their instance data. Plain CPU code, testable without a GPU (like SpriteBatcher).
///
/// Batches come in a fixed order: single-sided first, then double-sided (one pipeline change), and
/// otherwise in the order in which their first draw was recorded, visible or not (so the order does
/// not change as objects enter or leave the view); inside a batch, draws keep their recording order.
/// The result is the same on every run: no sorting on addresses. Batches also split by skinned or
/// not, and by alpha-tested or not. Each build() replaces the previous result and reuses its memory.
class MeshBatcher {
public:
    /// What the batches are for: it decides which draws count and what splits a batch.
    enum class Pass {
        Main,    ///< what the camera sees, with every material
        Shadow,  ///< depth: only the draws that cast shadows, grouped by mesh and sides alone (and, for
                 ///< an alpha-tested material, its base color texture: its cut-out shape casts the shadow)
    };

    /// Keeps the draws whose box meets `frustum` (and, for Pass::Shadow, that cast shadows); a
    /// default Frustum keeps them all. `draws` must outlive the batches (they point into it).
    /// O(draws). Pass::Shadow fills only what the shadow shaders read in each instance (the world
    /// rows, the palette, the base color and the alpha cutoff).
    void build(const std::vector<MeshDraw>& draws, const Frustum& frustum, Pass pass);
    /// The same, looking only at the draws listed in `subset` (indices into `draws`, increasing):
    /// for example those inside a point light's sphere.
    void build(const std::vector<MeshDraw>& draws, const std::vector<std::uint32_t>& subset, const Frustum& frustum,
               Pass pass);

    /// The instances of every batch, batch after batch (MeshBatch::first_instance), ready to upload.
    const std::vector<MeshInstance>& instances() const { return instances_; }
    /// The batches of the last build(), in drawing order.
    const std::vector<MeshBatch>& batches() const { return batches_; }
    /// The draws considered by the last build(), visible or not (casters only, for shadows).
    std::size_t submitted() const { return submitted_; }
    /// The draws kept by the last build(): the number of instances.
    std::size_t visible() const { return instances_.size(); }
    /// The triangles of every instance kept.
    std::size_t triangles() const { return triangles_; }
    /// Indices (into the draws given to build()) of the draws kept, in the order they were given.
    const std::vector<std::uint32_t>& visible_draws() const { return visible_; }

    /// The instance data of one draw, as Pass::Main fills it (computes the inverse transpose of its
    /// 3 x 3 part).
    static MeshInstance make_instance(const MeshDraw& draw);

private:
    /// What a batch shares. Unused textures stay null in the shadow pass.
    struct Key {
        const void* pointers[6];  ///< the mesh, then the five material textures
        bool double_sided;
        bool skinned;
        bool masked;
        std::uint8_t lod;
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
    /// Scratch space, kept between frames to avoid allocations.
    std::vector<std::uint32_t> group_of_;   ///< per visible draw: its group
    std::vector<std::uint32_t> visible_;    ///< indices of visible draws
    std::vector<std::uint32_t> cursor_;     ///< per group, then per batch (see build())
    std::vector<Key> keys_;                 ///< per group, in order of first appearance
    std::vector<std::uint32_t> first_draw_; ///< per group: its first draw (for the batch's material)
    std::vector<std::uint32_t> order_;      ///< per group: its place among the batches
    std::unordered_map<Key, std::uint32_t, KeyHash> groups_;
};

}  // namespace moteur
