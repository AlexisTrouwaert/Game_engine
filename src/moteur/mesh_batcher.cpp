#include "moteur/mesh_batcher.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <functional>

#include "moteur/mesh.hpp"

namespace moteur {

bool MeshBatcher::Key::operator==(const Key& other) const {
    for (int i = 0; i < 6; ++i) {
        if (pointers[i] != other.pointers[i]) {
            return false;
        }
    }
    return double_sided == other.double_sided;
}

std::size_t MeshBatcher::KeyHash::operator()(const Key& key) const {
    std::size_t hash = key.double_sided ? 1u : 0u;
    for (const void* pointer : key.pointers) {
        hash = hash * 1099511628211ull ^ std::hash<const void*>{}(pointer);
    }
    return hash;
}

MeshInstance MeshBatcher::make_instance(const MeshDraw& draw) {
    // GLM stores columns: row r of a matrix m is (m[0][r], m[1][r], m[2][r], m[3][r]). Normals only
    // need the 3 x 3 part, whose inverse is much cheaper than the full one.
    const glm::mat3 normal = glm::inverseTranspose(glm::mat3(draw.world));
    const Material& material = draw.material;
    MeshInstance instance = {};
    for (int r = 0; r < 3; ++r) {
        instance.world[r] = glm::vec4(draw.world[0][r], draw.world[1][r], draw.world[2][r], draw.world[3][r]);
        instance.normal_matrix[r] = glm::vec4(normal[0][r], normal[1][r], normal[2][r], 0.0f);
    }
    instance.base_color = material.base_color;
    instance.factors = glm::vec4(material.metallic, material.roughness, material.normal_scale, material.occlusion_strength);
    instance.emissive = glm::vec4(material.emissive, 0.0f);
    return instance;
}

void MeshBatcher::build(const std::vector<MeshDraw>& draws, const Frustum& frustum, Pass pass) {
    build(draws, nullptr, frustum, pass);
}

void MeshBatcher::build(const std::vector<MeshDraw>& draws, const std::vector<std::uint32_t>& subset,
                        const Frustum& frustum, Pass pass) {
    build(draws, &subset, frustum, pass);
}

void MeshBatcher::build(const std::vector<MeshDraw>& draws, const std::vector<std::uint32_t>* subset,
                        const Frustum& frustum, Pass pass) {
    instances_.clear();
    batches_.clear();
    submitted_ = 0;
    triangles_ = 0;
    visible_.clear();
    group_of_.clear();
    keys_.clear();
    first_draw_.clear();
    groups_.clear();

    constexpr std::uint32_t kNoGroup = ~0u;
    constexpr std::size_t kScanLimit = 16;
    std::uint32_t last_group = kNoGroup;

    // 1. The group of every draw, groups numbered by first appearance, then culling. Groups are
    // numbered before culling so that their order does not depend on what is visible: where two
    // objects meet at exactly the same depth, the one drawn first wins, and that must not change
    // (flicker) when the camera moves. A group left without any visible draw gets no batch.
    const std::size_t count = subset != nullptr ? subset->size() : draws.size();
    for (std::size_t k = 0; k < count; ++k) {
        const std::size_t i = subset != nullptr ? (*subset)[k] : k;
        const MeshDraw& draw = draws[i];
        if (pass == Pass::Shadow && !draw.material.casts_shadow) {
            continue;
        }
        ++submitted_;
        const Material& material = draw.material;
        Key key = {{draw.mesh, nullptr, nullptr, nullptr, nullptr, nullptr}, material.double_sided};
        if (pass == Pass::Main) {
            key.pointers[1] = material.base_color_texture;
            key.pointers[2] = material.metallic_roughness_texture;
            key.pointers[3] = material.normal_texture;
            key.pointers[4] = material.occlusion_texture;
            key.pointers[5] = material.emissive_texture;
        }
        // Finding the group is the costly part for many objects: first the group of the previous
        // draw (decor is often recorded mesh after mesh), then a plain scan while there are few
        // groups, and the hash table beyond.
        std::uint32_t group = kNoGroup;
        if (last_group != kNoGroup && keys_[last_group] == key) {
            group = last_group;
        } else if (keys_.size() <= kScanLimit) {
            for (std::uint32_t g = 0; g < keys_.size(); ++g) {
                if (keys_[g] == key) {
                    group = g;
                    break;
                }
            }
        } else if (const auto found = groups_.find(key); found != groups_.end()) {
            group = found->second;
        }
        if (group == kNoGroup) {
            group = static_cast<std::uint32_t>(keys_.size());
            keys_.push_back(key);
            first_draw_.push_back(static_cast<std::uint32_t>(i));
            if (keys_.size() == kScanLimit + 1) {  // from now on, the table is used: fill it
                for (std::uint32_t g = 0; g < keys_.size(); ++g) {
                    groups_.emplace(keys_[g], g);
                }
            } else if (keys_.size() > kScanLimit + 1) {
                groups_.emplace(key, group);
            }
        }
        last_group = group;
        if (!frustum.intersects(draw.bounds)) {
            continue;
        }
        visible_.push_back(static_cast<std::uint32_t>(i));
        group_of_.push_back(group);
    }

    // 2. Batch order: single-sided groups, then double-sided ones, each in order of first appearance;
    // groups without a visible draw are left out.
    const std::size_t group_count = keys_.size();
    cursor_.assign(group_count, 0);  // here: visible draws per group
    for (const std::uint32_t group : group_of_) {
        ++cursor_[group];
    }
    constexpr std::uint32_t kNoBatch = ~0u;
    order_.assign(group_count, kNoBatch);
    std::uint32_t next = 0;
    for (const bool double_sided : {false, true}) {
        for (std::size_t g = 0; g < group_count; ++g) {
            if (keys_[g].double_sided == double_sided && cursor_[g] > 0) {
                order_[g] = next++;
            }
        }
    }

    // 3. Sizes, then where each batch starts in the instance array.
    const std::size_t batch_count = next;
    batches_.resize(batch_count);
    for (std::size_t g = 0; g < group_count; ++g) {
        if (order_[g] == kNoBatch) {
            continue;
        }
        MeshBatch& batch = batches_[order_[g]];
        const MeshDraw& first = draws[first_draw_[g]];
        batch.mesh = first.mesh;
        batch.material = &first.material;
        batch.count = cursor_[g];
    }
    cursor_.assign(batch_count, 0);  // now: where the next instance of each batch goes
    std::uint32_t offset = 0;
    for (std::size_t b = 0; b < batch_count; ++b) {
        batches_[b].first_instance = offset;
        cursor_[b] = offset;
        offset += batches_[b].count;
        triangles_ += static_cast<std::size_t>(batches_[b].count) * (batches_[b].mesh->index_count / 3);
    }

    // 4. The instances, each at the next free place of its batch: recording order within a batch.
    // The shadow pass only reads the world matrix: the rest is not computed.
    instances_.resize(visible_.size());
    for (std::size_t v = 0; v < visible_.size(); ++v) {
        const std::uint32_t batch = order_[group_of_[v]];
        const MeshDraw& draw = draws[visible_[v]];
        MeshInstance& instance = instances_[cursor_[batch]++];
        if (pass == Pass::Main) {
            instance = make_instance(draw);
        } else {
            for (int r = 0; r < 3; ++r) {
                instance.world[r] = glm::vec4(draw.world[0][r], draw.world[1][r], draw.world[2][r], draw.world[3][r]);
            }
        }
    }
}

}  // namespace moteur
