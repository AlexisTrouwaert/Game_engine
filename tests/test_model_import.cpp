#include <doctest/doctest.h>

#include <stdexcept>
#include <vector>

#include "moteur/model_import.hpp"
#include "moteur/skeleton.hpp"

// Milestone 9, part 4: models imported (levels of detail, orders, KTX2 images) and their prepared
// form (.mmdl), which the game loads instead of the glTF.

namespace {

moteur::ModelData sample_model() {
    moteur::ModelData data;
    moteur::ModelPart sphere;
    sphere.name = "sphere/mesh#0";
    sphere.node = "sphere";
    sphere.mesh = moteur::make_sphere(1.0f, 48, 24);
    sphere.material = 0;
    sphere.transform[3] = glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
    data.parts.push_back(sphere);
    moteur::ModelPart cube;
    cube.name = "cube/mesh#0";
    cube.node = "cube";
    cube.mesh = moteur::make_cube();  // 12 triangles: below lod_min_triangles
    data.parts.push_back(cube);
    moteur::ModelMaterial material;
    material.name = "stone";
    material.base_color = {0.5f, 0.4f, 0.3f, 1.0f};
    material.base_color_image = 0;
    data.materials.push_back(material);
    moteur::ModelImage image;
    image.name = "stone color";
    image.srgb = true;
    image.image.width = 16;
    image.image.height = 16;
    image.image.pixels.assign(16 * 16 * 4, 200);
    data.images.push_back(image);
    moteur::JointData root;
    root.name = "root";
    data.skeleton.joints.push_back(root);
    moteur::ClipData clip;
    clip.name = "bob";
    clip.duration = 1.0f;
    moteur::JointTrack track;
    track.translations.times = {0.0f, 1.0f};
    track.translations.values = {glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)};
    clip.tracks.push_back(track);
    data.clips.push_back(clip);
    data.files = {"model.bin"};
    return data;
}

}  // namespace

TEST_CASE("model import: levels of detail, KTX2 images, nothing else changes") {
    moteur::ModelData data = sample_model();
    const std::size_t full = data.parts[0].mesh.indices.size();
    std::vector<std::string> warnings;
    moteur::ModelImportSettings settings =
        moteur::ModelImportSettings::from_json(nlohmann::json::parse(R"({"lods": 2, "typo": 1, "lod_error": 7})"), warnings);
    CHECK(settings.lods == 2);
    CHECK(settings.lod_error == 0.02f);  // out of bounds: kept
    CHECK(warnings.size() == 2);
    moteur::import_model(data, settings, "sample");
    const moteur::MeshData& sphere = data.parts[0].mesh;
    REQUIRE(sphere.lods.size() >= 2);
    CHECK(sphere.lods[0].first_index == 0);
    CHECK(sphere.lods[0].index_count == full);
    CHECK(sphere.lods[0].error == 0.0f);
    for (std::size_t l = 1; l < sphere.lods.size(); ++l) {
        CHECK(sphere.lods[l].index_count < sphere.lods[l - 1].index_count);
        CHECK(sphere.lods[l].error >= sphere.lods[l - 1].error);
        CHECK(sphere.lods[l].first_index == sphere.lods[l - 1].first_index + sphere.lods[l - 1].index_count);
    }
    CHECK(data.parts[1].mesh.lods.size() == 1);  // the cube: too small for levels
    CHECK(data.images[0].image.pixels.empty());
    CHECK_FALSE(data.images[0].ktx2.empty());
    CHECK(data.parts[0].transform[3] == glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
    CHECK(settings.hash() != moteur::ModelImportSettings{}.hash());
}

TEST_CASE("prepared model: written and read back exactly; damaged files refused") {
    moteur::ModelData data = sample_model();
    moteur::import_model(data, {}, "sample");
    const std::vector<std::uint8_t> bytes = moteur::encode_prepared_model(data, 0x1234567890abcdefull);
    CHECK(moteur::prepared_model_stamp(bytes.data(), bytes.size()) == 0x1234567890abcdefull);
    std::uint64_t stamp = 0;
    const moteur::ModelData back = moteur::decode_prepared_model(bytes.data(), bytes.size(), "sample", &stamp);
    CHECK(stamp == 0x1234567890abcdefull);
    REQUIRE(back.parts.size() == 2);
    CHECK(back.parts[0].mesh.indices == data.parts[0].mesh.indices);
    CHECK(back.parts[0].mesh.lods.size() == data.parts[0].mesh.lods.size());
    CHECK(back.parts[0].mesh.vertices.size() == data.parts[0].mesh.vertices.size());
    CHECK(back.parts[0].mesh.vertices[5].position == data.parts[0].mesh.vertices[5].position);
    CHECK(back.parts[0].node == "sphere");
    CHECK(back.materials[0].base_color == data.materials[0].base_color);
    CHECK(back.images[0].ktx2 == data.images[0].ktx2);
    CHECK(back.images[0].srgb);
    // The clips in their playable form, the keys of glTF gone.
    CHECK(back.clips.empty());
    REQUIRE_FALSE(back.prepared_clips.empty());
    const moteur::ClipLibrary clips = moteur::ClipLibrary::load(back.skeleton, back.prepared_clips, "sample");
    CHECK(clips.names() == std::vector<std::string>{"bob"});
    CHECK(clips.clip("bob").duration() == 1.0f);
    std::vector<std::uint8_t> damaged = back.prepared_clips;
    damaged.back() ^= 0x55;
    CHECK_THROWS_WITH_AS((void)moteur::ClipLibrary::load(back.skeleton, damaged, "sample"), doctest::Contains("damaged"),
                         std::runtime_error);
    CHECK(back.files == std::vector<std::string>{"model.bin"});
    CHECK(moteur::encode_prepared_model(back, stamp) == bytes);

    // Every cut short is refused, never read out of bounds.
    for (std::size_t size = 0; size < bytes.size(); size += 97) {
        CHECK_THROWS_AS((void)moteur::decode_prepared_model(bytes.data(), size, "cut"), std::runtime_error);
    }
    std::vector<std::uint8_t> wrong = bytes;
    wrong[4] = 99;  // another version
    CHECK_THROWS_WITH_AS((void)moteur::decode_prepared_model(wrong.data(), wrong.size(), "v"), doctest::Contains("format 99"),
                         std::runtime_error);
    CHECK(moteur::prepared_model_stamp(wrong.data(), wrong.size()) == 0);
}
