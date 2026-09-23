#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "moteur/image.hpp"
#include "moteur/material.hpp"
#include "moteur/mesh.hpp"

namespace moteur {

class Renderer;
struct Texture;

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

// A decoded image referenced by a material.
struct ModelImage {
    std::string name;
    Image image;
    bool srgb = false;  // a color (base color, emissive), not data (normals, roughness...)
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

    // The box around every part, in model space.
    Aabb bounds() const;
    std::size_t triangle_count() const;
};

// Reads a .gltf (with its .bin and images next to it) or a .glb file. Only what the engine uses is
// read: triangle meshes (positions; normals, computed if missing; first texture coordinates),
// the node hierarchy of the default scene, and base color factors and textures.
// Throws std::runtime_error naming the file if it cannot be read or is not valid glTF.
ModelData load_gltf(const std::string& path);

// The same from memory. `name` appears in error messages; `base_directory` (with a trailing
// separator, or empty) is where external buffers and images are looked for.
ModelData parse_gltf(const void* data, std::size_t size, const std::string& name,
                     const std::string& base_directory = "");

// A model on the GPU: its parts' meshes, materials and textures. Moves, does not copy.
struct Model {
    struct Part {
        Mesh mesh;
        glm::mat4 transform{1.0f};
        int material = -1;
    };
    std::vector<Part> parts;
    std::vector<Material> materials;  // their textures point into `textures`
    std::vector<std::unique_ptr<Texture>> textures;
    Aabb bounds;
    std::size_t triangle_count = 0;

    // Uploads everything and waits for the GPU (loading time, not inside a frame).
    static Model create(Renderer& renderer, const ModelData& data, const std::string& name);
    static Model load(Renderer& renderer, const std::string& path);
};

}  // namespace moteur
