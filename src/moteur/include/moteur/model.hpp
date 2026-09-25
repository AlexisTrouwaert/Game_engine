#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "moteur/image.hpp"
#include "moteur/material.hpp"
#include "moteur/mesh.hpp"

namespace moteur {

class Renderer;
struct Texture;
struct TextureSettings;

// The material of a model part, as glTF describes it (see Material). Images are indices in
// ModelData::images, -1 when the material has no such texture. Defaults are glTF's.
struct ModelMaterial {
    std::string name;
    glm::vec4 base_color{1.0f};   // multiplied with the texture
    float metallic = 1.0f;
    float roughness = 1.0f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    glm::vec3 emissive{0.0f};
    bool double_sided = false;
    int base_color_image = -1;
    int metallic_roughness_image = -1;
    int normal_image = -1;
    int occlusion_image = -1;
    int emissive_image = -1;
};

// One piece of a model: a mesh, where it sits in the model, and its material.
struct ModelPart {
    std::string name;             // "<node>/<mesh>#<primitive>", for errors and debuggers
    MeshData mesh;
    glm::mat4 transform{1.0f};    // part -> model, the world matrix of its glTF node
    int material = -1;            // index in ModelData::materials, -1 for the default material
};

// An image referenced by a material.
struct ModelImage {
    std::string name;
    Image image;        // empty for a KTX2 image, or a file left undecoded (GltfOptions::decode_external_images)
    std::vector<std::uint8_t> ktx2;  // a KTX2 image (KHR_texture_basisu), kept encoded: its GPU format
                                     // depends on the GPU (see decode_ktx2)
    bool srgb = false;  // a color (base color, emissive), not data (normals, roughness...)
    bool normal = false;  // a normal map
    std::string file;   // its file, relative to the model's directory; empty when inside the model
};

// A model on the CPU, as read from a glTF 2.0 file: plain data, testable without a GPU.
//
// The node hierarchy is flattened: every mesh a node uses becomes parts carrying that node's world
// matrix. glTF conventions are the engine's (right-handed, Y up, metres, counter-clockwise front
// faces), so nothing is converted.
struct ModelData {
    std::vector<ModelPart> parts;
    std::vector<ModelMaterial> materials;
    std::vector<ModelImage> images;
    // The files next to the model it was read from (buffers, and the images it decoded), relative
    // to its directory: what to watch for a reload.
    std::vector<std::string> files;

    // The box around every part, in model space.
    Aabb bounds() const;
    std::size_t triangle_count() const;
};

struct GltfOptions {
    // false: images in their own files are not decoded (ModelImage::image stays empty), so that
    // the caller can load them itself, through a cache shared by every model.
    bool decode_external_images = true;
    // false: the plain images of KHR_texture_basisu textures are used instead of their KTX2
    // version (to compare the two).
    bool prefer_ktx2 = true;
};

// Reads a .gltf (with its .bin and images next to it) or a .glb file. Only what the engine uses is
// read: triangle meshes (positions; normals, computed if missing; first texture coordinates),
// the node hierarchy of the default scene, and base color factors and textures.
// Throws std::runtime_error naming the file if it cannot be read or is not valid glTF.
ModelData load_gltf(const std::string& path, const GltfOptions& gltf_options = {});

// The same from memory. `name` appears in error messages; `base_directory` (with a trailing
// separator, or empty) is where external buffers and images are looked for.
ModelData parse_gltf(const void* data, std::size_t size, const std::string& name,
                     const std::string& base_directory = "", const GltfOptions& gltf_options = {});

// A model on the GPU: its parts' meshes, materials and textures. Moves, does not copy.
struct Model {
    struct Part {
        Mesh mesh;
        glm::mat4 transform{1.0f};
        int material = -1;
    };
    std::vector<Part> parts;
    std::vector<Material> materials;  // their textures point into `textures`
    std::vector<std::shared_ptr<Texture>> textures;
    Aabb bounds;
    std::size_t triangle_count = 0;
    std::size_t gpu_bytes = 0;  // meshes, and the textures the model created itself

    // Gives the texture of an image in its own file (ModelImage::file not empty), created with
    // `settings`; the textures of an asset cache, for instance, shared between models.
    using TextureSource = std::function<std::shared_ptr<Texture>(const ModelImage& image,
                                                                 const TextureSettings& settings)>;

    // Uploads everything and waits for the GPU (loading time, not inside a frame). Without
    // `texture_source`, every image must have been decoded.
    static Model create(Renderer& renderer, const ModelData& data, const std::string& name,
                        const TextureSource& texture_source = {});
    static Model load(Renderer& renderer, const std::string& path);

    // Takes the content of `fresh` (the same model, loaded again) without moving anything a user
    // may point to: each part's Mesh and each texture the model created stay the same objects,
    // with the new buffers and pixels. Textures shared with a cache must be the same ones. Throws
    // std::runtime_error, leaving this model unchanged, if the two differ in structure (number of
    // parts, materials or textures, or another shared texture): the scene must then be reloaded.
    void replace_in_place(Model&& fresh);
};

}  // namespace moteur
