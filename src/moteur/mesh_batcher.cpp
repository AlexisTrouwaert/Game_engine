#include "moteur/mesh_batcher.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>

#include <functional>

#include "moteur/mesh.hpp"

namespace moteur {

bool MeshBatcher::Key::operator==(const Key& other) const {
    for (int i = 0; i < 6; ++i) {
        if (pointers[i] != other.pointers[i]) {
            return false;
        }
    }
    return double_sided == other.double_sided && skinned == other.skinned && masked == other.masked && lod == other.lod;
}

std::size_t MeshBatcher::KeyHash::operator()(const Key& key) const {
    std::size_t hash = (key.double_sided ? 1u : 0u) + (key.skinned ? 2u : 0u) + (key.masked ? 4u : 0u) +
                       (static_cast<std::size_t>(key.lod) << 3);
    for (const void* pointer : key.pointers) {
        hash = hash * 1099511628211ull ^ std::hash<const void*>{}(pointer);
    }
    return hash;
}

glm::uvec4 MeshBatcher::pack_effects(const MaterialEffects& effects, float emissive_pulse, glm::vec2 uv_scroll) {
    const glm::vec4 flash = glm::clamp(glm::vec4(glm::vec3(effects.flash) * 0.25f, effects.flash.a), 0.0f, 1.0f);
    // The edge's glow: its color over its brightest channel, and that brightness / 16.
    const float peak = std::max({effects.dissolve_edge.r, effects.dissolve_edge.g, effects.dissolve_edge.b, 1e-6f});
    const glm::vec4 edge = glm::clamp(glm::vec4(effects.dissolve_edge / peak, peak / 16.0f), 0.0f, 1.0f);
    return {glm::packUnorm4x8(flash), glm::packUnorm4x8(edge), glm::packHalf2x16(glm::vec2(effects.dissolve, emissive_pulse)),
            glm::packHalf2x16(uv_scroll)};
}

MeshInstance MeshBatcher::make_instance(const MeshDraw& draw) {
    // GLM stores columns: row r of a matrix m is (m[0][r], m[1][r], m[2][r], m[3][r]). Normals only
    // need the 3 x 3 part, whose inverse is much cheaper than the full one.
    const glm::mat3 normal = glm::inverseTranspose(glm::mat3(draw.world));
    const DrawMaterial& material = draw.material;
    MeshInstance instance = {};
    for (int r = 0; r < 3; ++r) {
        instance.world[r] = glm::vec4(draw.world[0][r], draw.world[1][r], draw.world[2][r], draw.world[3][r]);
        instance.normal_matrix[r] = glm::vec4(normal[0][r], normal[1][r], normal[2][r], 0.0f);
    }
    instance.normal_matrix[0].w = material.alpha_cutoff;
    instance.normal_matrix[1].w = static_cast<float>(draw.pick_id);  // exact below 2^24
    instance.base_color = material.base_color;
    instance.factors = glm::vec4(material.metallic, material.roughness, material.normal_scale, material.occlusion_strength);
    // w: the palette of a skinned instance, or -1 for a mesh that fades before the hero (cutout).
    instance.emissive = glm::vec4(material.emissive, draw.palette >= 0 ? static_cast<float>(draw.palette)
                                                     : (material.fades && !material.blend ? -1.0f : 0.0f));
    instance.effects = pack_effects(draw.effects, material.emissive_pulse, material.uv_scroll);
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
        if (draw.material.blend || (pass == Pass::Shadow && !draw.material.casts_shadow)) {
            continue;  // transparent: build_transparent()
        }
        ++submitted_;
        const DrawMaterial& material = draw.material;
        // A dissolving object's shadow is cut out like an alpha-tested one's (by its noise).
        const bool masked = material.alpha_cutoff > 0.0f || (pass == Pass::Shadow && draw.effects.dissolve > 0.0f);
        const std::uint8_t lod = draw.lod < draw.mesh->lods.size() ? draw.lod : std::uint8_t{0};
        Key key = {{draw.mesh, nullptr, nullptr, nullptr, nullptr, nullptr}, material.double_sided, draw.palette >= 0, masked, lod};
        if (pass == Pass::Main) {
            key.pointers[1] = material.base_color_texture;
            key.pointers[2] = material.metallic_roughness_texture;
            key.pointers[3] = material.normal_texture;
            key.pointers[4] = material.occlusion_texture;
            key.pointers[5] = material.emissive_texture;
        } else if (masked) {
            key.pointers[1] = material.base_color_texture;  // its shape, cut out of the shadow
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
        batch.skinned = keys_[g].skinned;
        batch.masked = keys_[g].masked;
        batch.lod = keys_[g].lod;
    }
    cursor_.assign(batch_count, 0);  // now: where the next instance of each batch goes
    std::uint32_t offset = 0;
    for (std::size_t b = 0; b < batch_count; ++b) {
        batches_[b].first_instance = offset;
        cursor_[b] = offset;
        offset += batches_[b].count;
        const std::uint32_t indices = batches_[b].mesh->lods.empty() ? batches_[b].mesh->index_count
                                                                     : batches_[b].mesh->lods[batches_[b].lod].index_count;
        triangles_ += static_cast<std::size_t>(batches_[b].count) * (indices / 3);
    }

    // 4. The instances, each at the next free place of its batch: recording order within a batch.
    // The shadow pass only reads the world matrix (and the palette; for an alpha-tested material, its
    // alpha factor and cutoff): the rest is not computed.
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
            instance.emissive = glm::vec4(0.0f, 0.0f, 0.0f, draw.palette >= 0 ? static_cast<float>(draw.palette) : 0.0f);
            instance.base_color = draw.material.base_color;
            instance.normal_matrix[0].w = draw.material.alpha_cutoff;
            instance.effects = pack_effects(draw.effects, 0.0f, glm::vec2(0.0f));
        }
    }
}

void MeshBatcher::build_transparent(const std::vector<MeshDraw>& draws, const Frustum& frustum, glm::vec3 eye) {
    instances_.clear();
    batches_.clear();
    submitted_ = 0;
    triangles_ = 0;
    visible_.clear();
    sorted_.clear();
    for (std::size_t i = 0; i < draws.size(); ++i) {
        const MeshDraw& draw = draws[i];
        if (!draw.material.blend) {
            continue;
        }
        ++submitted_;
        if (!frustum.intersects(draw.bounds)) {
            continue;
        }
        const glm::vec3 offset = (draw.bounds.empty() ? glm::vec3(draw.world[3]) : draw.bounds.center()) - eye;
        sorted_.emplace_back(glm::dot(offset, offset), static_cast<std::uint32_t>(i));
    }
    // The farthest first; equal distances keep the recording order (a stable sort): the same on
    // every run.
    std::stable_sort(sorted_.begin(), sorted_.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    Key last = {};
    for (const auto& entry : sorted_) {
        const std::uint32_t index = entry.second;
        const MeshDraw& draw = draws[index];
        const DrawMaterial& material = draw.material;
        const std::uint8_t lod = draw.lod < draw.mesh->lods.size() ? draw.lod : std::uint8_t{0};
        const Key key = {{draw.mesh, material.base_color_texture, material.metallic_roughness_texture, material.normal_texture,
                          material.occlusion_texture, material.emissive_texture},
                         material.double_sided, draw.palette >= 0, material.alpha_cutoff > 0.0f, lod};
        if (batches_.empty() || !(key == last)) {
            MeshBatch batch;
            batch.mesh = draw.mesh;
            batch.material = &draw.material;
            batch.first_instance = static_cast<std::uint32_t>(instances_.size());
            batch.skinned = key.skinned;
            batch.masked = key.masked;
            batch.lod = lod;
            batches_.push_back(batch);
            last = key;
        }
        ++batches_.back().count;
        instances_.push_back(make_instance(draw));
        visible_.push_back(index);
        const std::uint32_t indices = draw.mesh->lods.empty() ? draw.mesh->index_count : draw.mesh->lods[lod].index_count;
        triangles_ += indices / 3;
    }
}

}  // namespace moteur
