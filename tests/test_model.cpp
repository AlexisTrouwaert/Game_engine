#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/model.hpp"
#include "moteur/renderer.hpp"

namespace {

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

template <typename T>
std::string data_uri(const std::vector<T>& values) {
    return "data:application/octet-stream;base64," + base64(values.data(), values.size() * sizeof(T));
}

moteur::ModelData parse(const std::string& json) {
    return moteur::parse_gltf(json.data(), json.size(), "test.gltf");
}

std::string error_of(const std::string& json) {
    try {
        parse(json);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

// A triangle on the ground, counter-clockwise seen from above: its computed normal is +Y.
const std::vector<float> kTriangle = {0, 0, 0, 0, 0, 1, 1, 0, 0};

std::string triangle_gltf(const std::string& extra_node_fields = "", const std::string& nodes = "",
                          const std::string& scene_nodes = "[0]", const std::string& primitive_extra = "",
                          const std::string& top_extra = "") {
    return R"({"asset": {"version": "2.0"},
        "buffers": [{"byteLength": 36, "uri": ")" + data_uri(kTriangle) + R"("}],
        "bufferViews": [{"buffer": 0, "byteLength": 36}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                       "min": [0, 0, 0], "max": [1, 0, 1]}],
        "meshes": [{"name": "tri", "primitives": [{"attributes": {"POSITION": 0})" + primitive_extra + R"(}]}],
        "nodes": [)" + (nodes.empty() ? R"({"name": "n", "mesh": 0)" + extra_node_fields + "}" : nodes) + R"(],
        "scenes": [{"nodes": )" + scene_nodes + R"(}], "scene": 0)" + top_extra + "}";
}

// 2 x 1 pixels: red then blue.
const char* kRedBluePng = "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAADklEQVR4nGP4z8AAQv8BD/kD/YURmXYAAAAASUVORK5CYII=";

}  // namespace

TEST_CASE("parse_gltf reads a triangle and computes its missing normals and indices") {
    const moteur::ModelData model = parse(triangle_gltf());
    REQUIRE(model.parts.size() == 1);
    const moteur::ModelPart& part = model.parts[0];
    CHECK(part.name == "n/tri#0");
    REQUIRE(part.mesh.vertices.size() == 3);
    CHECK(part.mesh.indices == std::vector<std::uint32_t>{0, 1, 2});
    CHECK(part.mesh.vertices[1].position == glm::vec3(0, 0, 1));
    for (const moteur::Vertex3D& vertex : part.mesh.vertices) {
        CHECK(vertex.normal.y == doctest::Approx(1.0f));
    }
    CHECK(part.material == -1);
    CHECK(model.triangle_count() == 1);
}

TEST_CASE("parse_gltf flattens the node hierarchy into part transforms") {
    const std::string nodes = R"({"name": "parent", "translation": [1, 0, 0], "children": [1]},
                                 {"name": "child", "mesh": 0, "scale": [2, 2, 2]})";
    const moteur::ModelData model = parse(triangle_gltf("", nodes));
    REQUIRE(model.parts.size() == 1);
    CHECK(model.parts[0].name == "child/tri#0");
    const glm::vec4 moved = model.parts[0].transform * glm::vec4(0, 0, 1, 1);
    CHECK(moved.x == doctest::Approx(1.0f));
    CHECK(moved.z == doctest::Approx(2.0f));
    const moteur::Aabb box = model.bounds();
    CHECK(box.min.x == doctest::Approx(1.0f));
    CHECK(box.max.x == doctest::Approx(3.0f));
    CHECK(box.max.z == doctest::Approx(2.0f));
}

TEST_CASE("parse_gltf reads a material with its base color texture") {
    const std::string top = std::string(R"(,
        "images": [{"uri": "data:image/png;base64,)") + kRedBluePng + R"("}],
        "textures": [{"source": 0}],
        "materials": [{"name": "painted", "pbrMetallicRoughness": {
            "baseColorFactor": [0.5, 1, 1, 1], "baseColorTexture": {"index": 0},
            "metallicFactor": 0.2, "roughnessFactor": 0.7}}])";
    const moteur::ModelData model = parse(triangle_gltf("", "", "[0]", R"(, "material": 0)", top));
    REQUIRE(model.materials.size() == 1);
    const moteur::ModelMaterial& material = model.materials[0];
    CHECK(material.name == "painted");
    CHECK(material.base_color == glm::vec4(0.5f, 1.0f, 1.0f, 1.0f));
    CHECK(material.metallic == doctest::Approx(0.2f));
    CHECK(material.roughness == doctest::Approx(0.7f));
    REQUIRE(material.base_color_image == 0);
    REQUIRE(model.images.size() == 1);
    const moteur::Image& image = model.images[0].image;
    CHECK(image.width == 2);
    CHECK(image.height == 1);
    CHECK(image.pixels == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
    CHECK(model.parts[0].material == 0);
}

TEST_CASE("parse_gltf can leave the images of their own files to the caller") {
    // The file does not exist: undecoded, it is never opened.
    const std::string top = R"(,
        "images": [{"uri": "textures/wood%20planks.png"}],
        "textures": [{"source": 0}],
        "materials": [{"name": "wood", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}])";
    moteur::GltfOptions options;
    options.decode_external_images = false;
    const std::string json = triangle_gltf("", "", "[0]", R"(, "material": 0)", top);
    const moteur::ModelData model = moteur::parse_gltf(json.data(), json.size(), "test.gltf", "", options);
    REQUIRE(model.images.size() == 1);
    CHECK(model.images[0].file == "textures/wood planks.png");  // URI decoded
    CHECK(model.images[0].srgb);
    CHECK(model.images[0].image.pixels.empty());
    CHECK(model.files.empty());  // the buffer is a data URI, and the image was not read

    // Decoded, the same model fails on the missing file, and names it.
    CHECK_THROWS_WITH_AS(moteur::parse_gltf(json.data(), json.size(), "test.gltf"),
                         doctest::Contains("wood planks.png"), std::runtime_error);
}

TEST_CASE("parse_gltf reads indices, normals and texture coordinates of any type") {
    // A quad: 16-bit indices, given normals, texture coordinates as normalized 16-bit integers.
    const std::vector<float> positions = {0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0};
    const std::vector<float> normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    const std::vector<std::uint16_t> uvs = {0, 0, 0, 65535, 65535, 65535, 65535, 0};
    const std::vector<std::uint16_t> indices = {0, 1, 2, 0, 2, 3};
    const std::string json = R"({"asset": {"version": "2.0"},
        "buffers": [{"byteLength": 48, "uri": ")" + data_uri(positions) + R"("},
                    {"byteLength": 48, "uri": ")" + data_uri(normals) + R"("},
                    {"byteLength": 16, "uri": ")" + data_uri(uvs) + R"("},
                    {"byteLength": 12, "uri": ")" + data_uri(indices) + R"("}],
        "bufferViews": [{"buffer": 0, "byteLength": 48}, {"buffer": 1, "byteLength": 48},
                        {"buffer": 2, "byteLength": 16}, {"buffer": 3, "byteLength": 12}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [0, 0, 0], "max": [1, 0, 1]},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5123, "normalized": true, "count": 4, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3}]}],
        "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0})";
    const moteur::ModelData model = parse(json);
    REQUIRE(model.parts.size() == 1);
    const moteur::MeshData& mesh = model.parts[0].mesh;
    CHECK(mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    CHECK(mesh.vertices[2].uv.x == doctest::Approx(1.0f));
    CHECK(mesh.vertices[2].uv.y == doctest::Approx(1.0f));
    CHECK(mesh.vertices[3].uv.y == doctest::Approx(0.0f));
    CHECK(mesh.vertices[0].normal == glm::vec3(0, 1, 0));
    CHECK(model.parts[0].name == "node/mesh#0");  // unnamed node and mesh
}

TEST_CASE("parse_gltf reads a binary .glb") {
    // The JSON chunk, padded with spaces, then the BIN chunk holding the positions.
    std::string json = R"({"asset": {"version": "2.0"}, "buffers": [{"byteLength": 36}],
        "bufferViews": [{"buffer": 0, "byteLength": 36}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 0, 1]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
        "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0})";
    while (json.size() % 4 != 0) {
        json += ' ';
    }
    const auto u32 = [](std::vector<std::uint8_t>& out, std::uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    };
    std::vector<std::uint8_t> glb;
    const auto bin_size = static_cast<std::uint32_t>(kTriangle.size() * sizeof(float));
    u32(glb, 0x46546C67);  // "glTF"
    u32(glb, 2);
    u32(glb, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin_size));
    u32(glb, static_cast<std::uint32_t>(json.size()));
    u32(glb, 0x4E4F534A);  // "JSON"
    glb.insert(glb.end(), json.begin(), json.end());
    u32(glb, bin_size);
    u32(glb, 0x004E4942);  // "BIN"
    const auto* bin = reinterpret_cast<const std::uint8_t*>(kTriangle.data());
    glb.insert(glb.end(), bin, bin + bin_size);

    const moteur::ModelData model = moteur::parse_gltf(glb.data(), glb.size(), "test.glb");
    REQUIRE(model.parts.size() == 1);
    CHECK(model.parts[0].mesh.vertices[2].position == glm::vec3(1, 0, 0));
}

TEST_CASE("parse_gltf reports problems with the file name") {
    CHECK(error_of("{ not json").find("test.gltf") != std::string::npos);
    CHECK(error_of(R"({"asset": {"version": "1.0"}})").find("test.gltf") != std::string::npos);
    // Only lines: nothing to draw.
    CHECK(error_of(triangle_gltf("", "", "[0]", R"(, "mode": 1)")).find("no triangle mesh") != std::string::npos);
    // A compression the engine cannot read.
    CHECK(error_of(triangle_gltf("", "", "[0]", "", R"(, "extensionsUsed": ["KHR_draco_mesh_compression"],
        "extensionsRequired": ["KHR_draco_mesh_compression"])"))
              .find("KHR_draco_mesh_compression") != std::string::npos);
    CHECK_THROWS_AS(moteur::load_gltf("does/not/exist.glb"), std::runtime_error);
}

namespace {

// A model as Model::create would build it, without a GPU: `owned` textures made by the model
// (embedded images), then `shared` ones (from a cache), each material pointing to one of them.
moteur::Model fake_model(std::uint32_t index_count, int width, const std::vector<std::shared_ptr<moteur::Texture>>& shared) {
    moteur::Model model;
    auto owned = std::make_shared<moteur::Texture>();
    owned->width = width;
    model.textures.push_back(owned);
    model.textures.insert(model.textures.end(), shared.begin(), shared.end());
    moteur::Material material;
    material.base_color_texture = owned.get();
    material.normal_texture = shared.empty() ? nullptr : shared[0].get();
    material.roughness = static_cast<float>(width) / 100.0f;
    model.materials.push_back(material);
    moteur::Model::Part part;
    part.mesh.index_count = index_count;
    part.material = 0;
    model.parts.push_back(std::move(part));
    model.triangle_count = index_count / 3;
    return model;
}

}  // namespace

TEST_CASE("Model::replace_in_place keeps every object users point to") {
    const std::vector<std::shared_ptr<moteur::Texture>> shared = {std::make_shared<moteur::Texture>()};
    moteur::Model model = fake_model(3, 10, shared);
    const moteur::Mesh* mesh = &model.parts[0].mesh;
    const moteur::Texture* owned = model.textures[0].get();

    model.replace_in_place(fake_model(6, 20, shared));

    CHECK(&model.parts[0].mesh == mesh);
    CHECK(model.parts[0].mesh.index_count == 6);
    CHECK(model.textures[0].get() == owned);
    CHECK(owned->width == 20);                                // new pixels, same texture
    CHECK(model.materials[0].base_color_texture == owned);    // not the fresh model's texture
    CHECK(model.materials[0].normal_texture == shared[0].get());
    CHECK(model.materials[0].roughness == doctest::Approx(0.2f));
    CHECK(model.triangle_count == 2);
}

TEST_CASE("Model::replace_in_place refuses another structure and changes nothing") {
    const std::vector<std::shared_ptr<moteur::Texture>> shared = {std::make_shared<moteur::Texture>()};
    moteur::Model model = fake_model(3, 10, shared);

    moteur::Model more_parts = fake_model(6, 20, shared);
    more_parts.parts.emplace_back();
    CHECK_THROWS_WITH_AS(model.replace_in_place(std::move(more_parts)), doctest::Contains("structure"), std::runtime_error);

    // Another cached texture: the old one may be freed while a scene still points to it.
    const std::vector<std::shared_ptr<moteur::Texture>> other = {std::make_shared<moteur::Texture>()};
    CHECK_THROWS_WITH_AS(model.replace_in_place(fake_model(6, 20, other)), doctest::Contains("texture"), std::runtime_error);

    CHECK(model.parts.size() == 1);
    CHECK(model.parts[0].mesh.index_count == 3);
    CHECK(model.textures[0]->width == 10);
    CHECK((model.textures[1] == shared[0]));
}
