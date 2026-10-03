// Skinning (milestone 5, part 5): the CPU formula (the same as skinning.hlsli) on the reference
// model, the reach of skinned parts, and how skinned draws are batched and sent.

#include <doctest/doctest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "moteur/mesh.hpp"
#include "moteur/mesh_batcher.hpp"
#include "moteur/model.hpp"
#include "moteur/skeleton.hpp"

namespace {

const std::string kAssets = MOTEUR_SOURCE_ASSETS;

bool near(const glm::vec3& value, const glm::vec3& expected, float tolerance = 1e-3f) {
    return glm::all(glm::lessThan(glm::abs(value - expected), glm::vec3(tolerance)));
}

const moteur::ModelPart& part_named(const moteur::ModelData& data, const std::string& node) {
    for (const moteur::ModelPart& part : data.parts) {
        if (part.node == node) {
            return part;
        }
    }
    throw std::runtime_error("no part " + node);
}

}  // namespace

TEST_CASE("skin_position: the reference column bends with its joints") {
    const moteur::ModelData data = moteur::load_gltf(kAssets + "models/skinned_reference.glb");
    const moteur::Skeleton skeleton = moteur::Skeleton::create(data.skeleton, "reference");
    const moteur::ClipLibrary clips = moteur::ClipLibrary::create(data.skeleton, data.clips, "reference");
    const moteur::ModelPart& column = part_named(data, "column");
    const moteur::SkinData& skin = data.skins[static_cast<std::size_t>(column.skin)];
    moteur::PoseSampler sampler;
    std::vector<glm::mat4> palette;

    // In the rest pose, the inverse bind matrices undo the joints: the vertices stay where they are.
    sampler.rest(skeleton);
    sampler.palette(skin, palette);
    REQUIRE(palette.size() == 3);
    for (std::size_t v = 0; v < column.mesh.vertices.size(); ++v) {
        CHECK(near(moteur::skin_position(palette, column.mesh.skin[v], column.mesh.vertices[v].position),
                   column.mesh.vertices[v].position));
    }

    // Half way through Bend: "upper" (at 1, 1, 0) has turned 45 degrees around +Z.
    sampler.sample(skeleton, clips.clip("Bend"), 0.5f);
    sampler.palette(skin, palette);
    const glm::vec3 upper(1.0f, 1.0f, 0.0f);
    const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const auto turned = [&](glm::vec3 p) { return upper + glm::vec3(turn * glm::vec4(p - upper, 1.0f)); };
    for (std::size_t v = 0; v < column.mesh.vertices.size(); ++v) {
        const glm::vec3 p = column.mesh.vertices[v].position;
        const glm::vec3 skinned = moteur::skin_position(palette, column.mesh.skin[v], p);
        if (p.y < 0.5f) {
            CHECK(near(skinned, p));  // root only: not moved
        } else if (p.y < 1.5f) {
            CHECK(near(skinned, 0.75f * turned(p) + 0.25f * p));  // blended: upper 0.75, root 0.25
        } else {
            CHECK(near(skinned, turned(p)));  // upper only
        }
    }
}

TEST_CASE("parse_gltf: the reach of a skinned part from its joints") {
    const moteur::ModelData data = moteur::load_gltf(kAssets + "models/skinned_reference.glb");
    // The top corners of the column, (1 +- 0.1, 2, +-0.1), follow "upper" at (1, 1, 0).
    CHECK(part_named(data, "column").skin_radius == doctest::Approx(std::sqrt(1.02f)));
    CHECK(part_named(data, "marker").skin_radius == 0.0f);  // rigid
}

TEST_CASE("MeshBatcher: skinned draws get batches of their own, and their palette") {
    moteur::Mesh mesh;  // without GPU buffers: the batcher only reads the box and the index count
    mesh.bounds.add(glm::vec3(-0.5f));
    mesh.bounds.add(glm::vec3(0.5f));
    mesh.index_count = 36;
    std::vector<moteur::MeshDraw> draws(3);
    for (std::size_t i = 0; i < draws.size(); ++i) {
        draws[i].mesh = &mesh;
        draws[i].world = glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
        draws[i].bounds = moteur::transform_box(mesh.bounds, draws[i].world);
    }
    draws[1].palette = 41;  // the same mesh, in a pose
    draws[1].palette_size = 41;
    draws[2].palette = 82;
    draws[2].palette_size = 41;

    for (const auto pass : {moteur::MeshBatcher::Pass::Main, moteur::MeshBatcher::Pass::Shadow}) {
        moteur::MeshBatcher batcher;
        batcher.build(draws, moteur::Frustum{}, pass);
        REQUIRE(batcher.batches().size() == 2);  // still, then skinned: first appearance
        CHECK_FALSE(batcher.batches()[0].skinned);
        CHECK(batcher.batches()[1].skinned);
        CHECK(batcher.batches()[1].count == 2);
        const auto& instances = batcher.instances();
        CHECK(instances[0].emissive.w == 0.0f);
        CHECK(instances[1].emissive.w == 41.0f);  // where each instance's palette starts
        CHECK(instances[2].emissive.w == 82.0f);
    }
}

TEST_CASE("The knight's skinned parts in a pose, compared with Blender") {
    const std::string path = kAssets + "models/characters/kaykit_adventurers/Knight.glb";
    if (!std::filesystem::exists(path)) {
        MESSAGE("skipped: Knight.glb is missing (python tools/models/fetch_test_characters.py)");
        return;
    }
    const moteur::ModelData data = moteur::load_gltf(path);
    const moteur::Skeleton skeleton = moteur::Skeleton::create(data.skeleton, "Knight");
    const moteur::ClipLibrary clips = moteur::ClipLibrary::create(data.skeleton, data.clips, "Knight");
    moteur::PoseSampler sampler;
    std::vector<glm::mat4> palette;
    const auto skinned_box = [&](const std::string& clip, float seconds) {
        const moteur::SkeletalClip& played = clips.clip(clip);
        sampler.sample(skeleton, played, seconds / played.duration());
        moteur::Aabb box;
        for (const moteur::ModelPart& part : data.parts) {
            if (part.skin < 0) {
                continue;
            }
            sampler.palette(data.skins[static_cast<std::size_t>(part.skin)], palette);
            for (std::size_t v = 0; v < part.mesh.vertices.size(); ++v) {
                box.add(moteur::skin_position(palette, part.mesh.skin[v], part.mesh.vertices[v].position));
            }
        }
        return box;
    };
    // The box of the six skinned parts (body, head, arms, legs; no helmet nor weapons) at a few
    // times, as Blender 5.2 measures them (evaluated meshes of the imported .glb, in glTF axes, metres).
    struct Case {
        const char* clip;
        float seconds;
        glm::vec3 min;
        glm::vec3 max;
    };
    const Case cases[] = {
        {"Idle", 0.5f, {-0.6508f, -0.0010f, -0.4851f}, {0.6685f, 2.2771f, 0.5316f}},
        {"1H_Melee_Attack_Chop", 0.4f, {-0.6401f, -0.0005f, -0.7304f}, {0.6811f, 2.2977f, 0.4381f}},
        {"Running_A", 0.3f, {-0.6945f, 0.0834f, -0.4105f}, {0.7029f, 2.2898f, 1.0160f}},
    };
    for (const Case& c : cases) {
        const moteur::Aabb box = skinned_box(c.clip, c.seconds);
        MESSAGE(std::string(c.clip) << " at " << c.seconds << " s: min (" << box.min.x << ", " << box.min.y << ", " << box.min.z
                       << "), max (" << box.max.x << ", " << box.max.y << ", " << box.max.z << ")");
        CHECK(near(box.min, c.min, 2e-3f));
        CHECK(near(box.max, c.max, 2e-3f));
    }
}
