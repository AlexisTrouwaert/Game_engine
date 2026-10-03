// Skeletons, skins and clips read from glTF (milestone 5, part 3), checked on the reference model
// written by tools/models/make_skinned_reference_model.py (its expected values were checked in
// Blender), on small files written here, and on the downloaded test characters when present.

#include <doctest/doctest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/model.hpp"
#include "moteur/skeleton.hpp"

namespace {

const std::string kAssets = MOTEUR_SOURCE_ASSETS;  // the assets/ directory of the source tree

bool near(const glm::vec3& value, const glm::vec3& expected, float tolerance = 1e-3f) {
    return glm::all(glm::lessThan(glm::abs(value - expected), glm::vec3(tolerance)));
}

// Where a joint is in model space.
glm::vec3 position(const std::vector<glm::mat4>& model, int joint) {
    return glm::vec3(model[static_cast<std::size_t>(joint)][3]);
}

moteur::ModelData load_reference(const moteur::GltfOptions& options = {}) {
    return moteur::load_gltf(kAssets + "models/skinned_reference.glb", options);
}

// Loads a downloaded test character, or returns false (with a message) when it is not there.
bool load_character(const std::string& path, moteur::ModelData& data, const moteur::GltfOptions& options = {}) {
    if (!std::filesystem::exists(kAssets + path)) {
        MESSAGE("skipped: " << path << " is missing (python tools/models/fetch_test_characters.py)");
        return false;
    }
    data = moteur::load_gltf(kAssets + path, options);
    return true;
}

std::string base64(const void* data, std::size_t size) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::string out;
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t chunk = (bytes[i] << 16) | (i + 1 < size ? bytes[i + 1] << 8 : 0) | (i + 2 < size ? bytes[i + 2] : 0);
        out += alphabet[(chunk >> 18) & 63];
        out += alphabet[(chunk >> 12) & 63];
        out += i + 1 < size ? alphabet[(chunk >> 6) & 63] : '=';
        out += i + 2 < size ? alphabet[chunk & 63] : '=';
    }
    return out;
}

}  // namespace

TEST_CASE("parse_gltf reads the skeleton parents first, with the nodes above the joints") {
    const moteur::ModelData data = load_reference();
    const moteur::SkeletonData& skeleton = data.skeleton;
    // The file lists the nodes children first; "armature" is not a joint of the skin but moves it.
    REQUIRE(skeleton.joints.size() == 4);
    CHECK(skeleton.joints[0].name == "armature");
    CHECK(skeleton.joints[1].name == "root");
    CHECK(skeleton.joints[2].name == "upper");
    CHECK(skeleton.joints[3].name == "lower");
    for (int i = 0; i < 4; ++i) {
        CHECK(skeleton.joints[static_cast<std::size_t>(i)].parent == i - 1);
    }
    CHECK(skeleton.find("upper") == 2);
    CHECK(skeleton.find("marker") == -1);  // a mesh node under a joint, not a joint

    const std::vector<glm::mat4> rest = skeleton.rest_model_matrices();
    CHECK(near(position(rest, 1), {1, 0, 0}));
    CHECK(near(position(rest, 3), {1, 2, 0}));

    // skin.joints is [upper, root, lower]: the palette keeps that order.
    REQUIRE(data.skins.size() == 1);
    CHECK(data.skins[0].joints == std::vector<int>{2, 1, 3});
    REQUIRE(data.skins[0].inverse_binds.size() == 3);
    CHECK(near(glm::vec3(data.skins[0].inverse_binds[0][3]), {-1, -1, 0}));
}

TEST_CASE("parse_gltf keeps skinned parts in place and puts rigid parts on their joint") {
    const moteur::ModelData data = load_reference();
    REQUIRE(data.parts.size() == 2);
    // In the order of the scene: the marker, under the armature, comes before the column.
    const moteur::ModelPart& column = data.parts[1];
    CHECK(column.node == "column");
    CHECK(column.skin == 0);
    CHECK(column.joint == -1);
    CHECK(column.transform == glm::mat4(1.0f));  // its node's (5, 5, 5) is ignored, as glTF says
    REQUIRE(column.mesh.skin.size() == column.mesh.vertices.size());
    for (std::size_t v = 0; v < column.mesh.vertices.size(); ++v) {
        const float y = column.mesh.vertices[v].position.y;
        const moteur::VertexSkin& skin = column.mesh.skin[v];
        if (y < 0.5f) {  // root only (palette entry 1)
            CHECK(skin == moteur::VertexSkin{{1, 0, 0, 0}, {65535, 0, 0, 0}});
        } else if (y < 1.5f) {  // upper 0.75, root 0.25
            CHECK(skin == moteur::VertexSkin{{0, 1, 0, 0}, {49151, 16384, 0, 0}});
        } else {  // upper only
            CHECK(skin == moteur::VertexSkin{{0, 0, 0, 0}, {65535, 0, 0, 0}});
        }
    }

    const moteur::ModelPart& marker = data.parts[0];
    CHECK(marker.node == "marker");
    CHECK(marker.skin == -1);
    CHECK(marker.joint == 3);  // "lower"
    CHECK(marker.mesh.skin.empty());
    CHECK(near(glm::vec3(marker.joint_offset[3]), {0, 0.25f, 0}));
    CHECK(near(glm::vec3(marker.transform[3]), {1, 2.25f, 0}));  // where the rest pose puts it
}

TEST_CASE("parse_gltf reads the clips as linear keys from 0") {
    const moteur::ModelData data = load_reference();
    REQUIRE(data.clips.size() == 5);
    const auto clip = [&data](const std::string& name) -> const moteur::ClipData& {
        for (const moteur::ClipData& c : data.clips) {
            if (c.name == name) {
                return c;
            }
        }
        FAIL("no clip " << name);
        throw std::logic_error("unreachable");
    };
    // Bend's keys are at 0.5 and 1.5 s in the file.
    const moteur::ClipData& bend = clip("Bend");
    CHECK(bend.duration == doctest::Approx(1.0f));
    REQUIRE(bend.tracks.size() == 4);
    CHECK(bend.tracks[2].rotations.times == std::vector<float>{0.0f, 1.0f});
    CHECK(bend.tracks[1].rotations.empty());  // root is not animated by Bend

    // STEP: a key 0.1 ms before each change keeps the previous value.
    const moteur::ClipData& step = clip("Step");
    const moteur::KeyTrack<glm::vec3>& steps = step.tracks[1].translations;
    REQUIRE(steps.times.size() == 5);
    CHECK(steps.times[1] == doctest::Approx(0.5f - 1e-4f));
    CHECK(steps.values[1] == glm::vec3(0.0f));
    CHECK(steps.values[2] == glm::vec3(0, 0, 1));

    // CUBICSPLINE: sampled at 60 Hz over its second.
    const moteur::ClipData& cubic = clip("Cubic");
    const moteur::KeyTrack<glm::vec3>& curve = cubic.tracks[3].translations;
    CHECK(curve.times.size() == 61);
    CHECK(curve.values[15].x == doctest::Approx(0.15625f));  // 0.25 s: 3t² - 2t³
    CHECK(curve.values[30].x == doctest::Approx(0.5f));
}

TEST_CASE("Skeleton and ClipLibrary play the reference clips as Blender does") {
    const moteur::ModelData data = load_reference();
    const moteur::Skeleton skeleton = moteur::Skeleton::create(data.skeleton, "reference");
    CHECK(skeleton.joint_count() == 4);
    const moteur::ClipLibrary clips = moteur::ClipLibrary::create(data.skeleton, data.clips, "reference");
    CHECK(clips.names() == std::vector<std::string>{"Bend", "Cubic", "Flip", "Scale", "Step"});
    CHECK(clips.mismatch(skeleton).empty());
    CHECK(clips.bytes() > 0);
    CHECK_THROWS_WITH_AS(clips.clip("Run"), "Clips 'reference': no clip 'Run'", std::runtime_error);

    // The values of make_skinned_reference_model.py, also found in Blender 5.2: where "lower" is.
    std::vector<glm::mat4> pose;
    const auto lower_at = [&](const std::string& name, float seconds) {
        moteur::sample_model_pose(skeleton, clips.clip(name), seconds, pose);
        REQUIRE(pose.size() == 4);
        return position(pose, 3);
    };
    const float h = std::sqrt(0.5f);
    CHECK(near(lower_at("Bend", 0.0f), {1, 2, 0}));
    CHECK(near(lower_at("Bend", 0.5f), {1 - h, 1 + h, 0}));
    CHECK(near(lower_at("Bend", 1.0f), {0, 1, 0}));
    CHECK(near(lower_at("Bend", 7.0f), {0, 1, 0}));  // clamped to the end
    CHECK(near(lower_at("Step", 0.25f), {1, 2, 0}));
    CHECK(near(lower_at("Step", 0.75f), {1, 2, 1}));
    CHECK(near(lower_at("Cubic", 0.25f), {1.15625f, 2, 0}));
    CHECK(near(lower_at("Cubic", 0.5f), {1.5f, 2, 0}));
    CHECK(near(lower_at("Flip", 0.5f), {1, 2, 0}));  // the short way: the root does not turn
    CHECK(near(lower_at("Scale", 0.5f), {1, 2.5f, 0}));
}

TEST_CASE("parse_gltf without meshes reads the skeleton and the clips only") {
    moteur::GltfOptions options;
    options.meshes = false;
    const moteur::ModelData data = load_reference(options);
    CHECK(data.parts.empty());
    CHECK(data.materials.empty());
    CHECK(data.skeleton.joints.size() == 4);
    CHECK(data.clips.size() == 5);
}

TEST_CASE("parse_gltf keeps the four strongest influences and normalizes them") {
    // Three vertices, skinned on six joints with JOINTS_0/WEIGHTS_0 and JOINTS_1/WEIGHTS_1.
    const float positions[] = {0, 0, 0, 0, 0, 1, 1, 0, 0};
    const std::uint8_t joints0[] = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
    const float weights0[] = {0.125f, 0.25f, 0.125f, 0, 0.2f, 0.2f, 0.2f, 0.2f, 0, 0, 0, 0};
    const std::uint8_t joints1[] = {4, 5, 0, 0, 4, 5, 0, 0, 4, 5, 0, 0};
    const float weights1[] = {0.5f, 0, 0, 0, 0.1f, 0.1f, 0, 0, 0, 0, 0, 0};
    std::vector<std::uint8_t> buffer;
    const auto append = [&buffer](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        buffer.insert(buffer.end(), bytes, bytes + size);
    };
    append(positions, sizeof(positions));  // 0: 36 bytes
    append(joints0, sizeof(joints0));      // 36: 12
    append(weights0, sizeof(weights0));    // 48: 48
    append(joints1, sizeof(joints1));      // 96: 12
    append(weights1, sizeof(weights1));    // 108: 48
    const std::string json = R"({"asset": {"version": "2.0"},
        "buffers": [{"byteLength": 156, "uri": "data:application/octet-stream;base64,)" + base64(buffer.data(), buffer.size()) + R"("}],
        "bufferViews": [{"buffer": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 12},
                        {"buffer": 0, "byteOffset": 48, "byteLength": 48}, {"buffer": 0, "byteOffset": 96, "byteLength": 12},
                        {"buffer": 0, "byteOffset": 108, "byteLength": 48}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 0, 1]},
                      {"bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4"},
                      {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"},
                      {"bufferView": 3, "componentType": 5121, "count": 3, "type": "VEC4"},
                      {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2, "JOINTS_1": 3, "WEIGHTS_1": 4}}]}],
        "skins": [{"joints": [1, 2, 3, 4, 5, 6]}],
        "nodes": [{"name": "body", "mesh": 0, "skin": 0}, {"name": "j0"}, {"name": "j1"}, {"name": "j2"},
                  {"name": "j3"}, {"name": "j4"}, {"name": "j5"}],
        "scenes": [{"nodes": [0, 1, 2, 3, 4, 5, 6]}], "scene": 0})";
    const moteur::ModelData data = moteur::parse_gltf(json.data(), json.size(), "weights.gltf");
    CHECK(data.skeleton.joints.size() == 6);  // the six joints; the mesh node is not one
    REQUIRE(data.parts.size() == 1);
    const std::vector<moteur::VertexSkin>& skin = data.parts[0].mesh.skin;
    REQUIRE(skin.size() == 3);
    // j4 0.5, j1 0.25, then j0 and j2 at 0.125 (equal weights: the smaller joint first). The
    // quarters round to 65536 in all: the strongest gives one back.
    CHECK(skin[0] == moteur::VertexSkin{{4, 1, 0, 2}, {32767, 16384, 8192, 8192}});
    // Six influences: the four of 0.2 kept, 0.25 each once normalized.
    CHECK(skin[1] == moteur::VertexSkin{{0, 1, 2, 3}, {16383, 16384, 16384, 16384}});
    // No weight at all: the first joint of the palette.
    CHECK(skin[2] == moteur::VertexSkin{{0, 0, 0, 0}, {65535, 0, 0, 0}});
}

TEST_CASE("SkeletonData::mismatch names the first difference") {
    const moteur::SkeletonData reference = load_reference().skeleton;
    CHECK(reference.mismatch(reference).empty());
    moteur::SkeletonData renamed = reference;
    renamed.joints[2].name = "elbow";
    CHECK(reference.mismatch(renamed) == "joint 2 is 'upper' instead of 'elbow'");
    moteur::SkeletonData moved = reference;
    moved.joints[3].parent = 1;
    CHECK(reference.mismatch(moved) == "joint 'lower' has the parent 'upper' instead of 'root'");
    moteur::SkeletonData shorter = reference;
    shorter.joints.pop_back();
    CHECK(reference.mismatch(shorter) == "4 joints instead of 3");
}

TEST_CASE("ClipLibrary::replace_in_place keeps the clips where they are") {
    const moteur::ModelData data = load_reference();
    moteur::ClipLibrary clips = moteur::ClipLibrary::create(data.skeleton, data.clips, "reference");
    const moteur::SkeletalClip* bend = &clips.clip("Bend");
    clips.replace_in_place(moteur::ClipLibrary::create(data.skeleton, data.clips, "reference"));
    CHECK(&clips.clip("Bend") == bend);
    CHECK(bend->duration() == doctest::Approx(1.0f));

    std::vector<moteur::ClipData> fewer = data.clips;
    fewer.pop_back();
    CHECK_THROWS_AS(clips.replace_in_place(moteur::ClipLibrary::create(data.skeleton, fewer, "reference")), std::runtime_error);
    CHECK(clips.size() == 5);  // unchanged

    moteur::Skeleton skeleton = moteur::Skeleton::create(data.skeleton, "reference");
    moteur::SkeletonData other = data.skeleton;
    other.joints[3].name = "tip";
    CHECK_THROWS_AS(skeleton.replace_in_place(moteur::Skeleton::create(other, "reference")), std::runtime_error);
    CHECK(skeleton.data().joints[3].name == "lower");
}

TEST_CASE("The KayKit characters share one skeleton and their clips") {
    moteur::ModelData knight;
    moteur::ModelData warrior;
    const auto start = std::chrono::steady_clock::now();
    if (!load_character("models/characters/kaykit_adventurers/Knight.glb", knight)) {
        return;
    }
    const auto loaded = std::chrono::steady_clock::now();
    moteur::GltfOptions clips_only;
    clips_only.meshes = false;
    if (!load_character("models/characters/kaykit_skeletons/Skeleton_Warrior.glb", warrior, clips_only)) {
        return;
    }
    // "Rig" (the armature node) above the 41 joints of the skin.
    REQUIRE(knight.skeleton.joints.size() == 42);
    CHECK(knight.skeleton.joints[0].name == "Rig");
    CHECK(knight.skeleton.mismatch(warrior.skeleton).empty());
    CHECK(knight.clips.size() == 76);
    CHECK(warrior.clips.size() == 95);

    // Weapons, shields, helmet and cape are rigid parts on joints.
    const auto part = [&knight](const std::string& node) -> const moteur::ModelPart& {
        for (const moteur::ModelPart& p : knight.parts) {
            if (p.node == node) {
                return p;
            }
        }
        FAIL("no part " << node);
        throw std::logic_error("unreachable");
    };
    CHECK(part("1H_Sword").joint == knight.skeleton.find("handslot.r"));
    CHECK(part("Round_Shield").joint == knight.skeleton.find("handslot.l"));
    CHECK(part("Knight_Helmet").joint == knight.skeleton.find("head"));
    CHECK(part("Knight_Body").skin == 0);

    const auto read = std::chrono::steady_clock::now();
    const moteur::Skeleton skeleton = moteur::Skeleton::create(knight.skeleton, "Knight");
    const auto built = std::chrono::steady_clock::now();
    const moteur::ClipLibrary clips = moteur::ClipLibrary::create(warrior.skeleton, warrior.clips, "Skeleton_Warrior");
    const auto done = std::chrono::steady_clock::now();
    CHECK(clips.mismatch(skeleton).empty());  // the warrior's clips play on the knight
    const moteur::SkeletalClip& idle = clips.clip("Idle");
    CHECK(idle.duration() == doctest::Approx(1.0667f).epsilon(1e-3));
    std::vector<glm::mat4> pose;
    moteur::sample_model_pose(skeleton, idle, 0.5f, pose);
    const glm::vec3 head = position(pose, knight.skeleton.find("head"));
    CHECK(head.y > 1.0f);
    CHECK(head.y < 1.5f);  // the rest pose puts it at 1.23 m

    const auto ms = [](auto from, auto to) { return std::chrono::duration<double, std::milli>(to - from).count(); };
    MESSAGE("Knight.glb read in " << ms(start, loaded) << " ms, Skeleton_Warrior.glb (clips only) in " << ms(loaded, read)
                                  << " ms; skeleton built in " << ms(read, built)
                                  << " ms; 95 clips built in " << ms(built, done) << " ms, " << clips.bytes() / 1024 << " KB");
}

TEST_CASE("The Khronos test models load") {
    moteur::ModelData data;
    if (load_character("models/characters/khronos/SimpleSkin.gltf", data)) {
        CHECK(data.skeleton.joints.size() == 2);
        CHECK(data.clips.size() == 1);
    }
    if (load_character("models/characters/khronos/Fox.glb", data)) {  // no normals: computed
        CHECK(data.skins.size() == 1);
        CHECK(data.skins[0].joints.size() == 24);
        CHECK(data.clips.size() == 3);
        CHECK(data.parts[0].mesh.skin.size() == data.parts[0].mesh.vertices.size());
        const moteur::Skeleton skeleton = moteur::Skeleton::create(data.skeleton, "Fox");
        const moteur::ClipLibrary clips = moteur::ClipLibrary::create(data.skeleton, data.clips, "Fox");
        CHECK(clips.names() == std::vector<std::string>{"Run", "Survey", "Walk"});
    }
    if (load_character("models/characters/khronos/RiggedFigure.glb", data)) {  // a clip without a name
        CHECK(data.skins[0].joints.size() == 19);
        REQUIRE(data.clips.size() == 1);
        CHECK(data.clips[0].name == "animation 0");
    }
    if (load_character("models/characters/khronos/InterpolationTest.glb", data)) {
        // No skin: the animated cubes are the skeleton, and each cube a rigid part on its own node.
        CHECK(data.skins.empty());
        CHECK(data.clips.size() == 9);
        for (const moteur::ModelPart& part : data.parts) {
            if (part.node.rfind("Cube", 0) == 0) {
                CHECK(part.joint >= 0);
            }
        }
        const moteur::ClipLibrary clips = moteur::ClipLibrary::create(data.skeleton, data.clips, "InterpolationTest");
        CHECK(clips.size() == 9);
    }
}
