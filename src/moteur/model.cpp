#include "moteur/model.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/type_ptr.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>

#include "moteur/renderer.hpp"

namespace moteur {

namespace {

// cgltf reads external buffers through these, so they go through SDL_LoadFile, which handles
// UTF-8 paths on Windows (cgltf's own fopen does not).
cgltf_result read_file(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size,
                       void** data) {
    std::size_t loaded_size = 0;
    void* loaded = SDL_LoadFile(path, &loaded_size);
    if (loaded == nullptr) {
        return cgltf_result_file_not_found;
    }
    *size = loaded_size;
    *data = loaded;
    return cgltf_result_success;
}

void release_file(const cgltf_memory_options*, const cgltf_file_options*, void* data) {
    SDL_free(data);
}

const char* describe(cgltf_result result) {
    switch (result) {
        case cgltf_result_data_too_short: return "the data is truncated";
        case cgltf_result_unknown_format: return "this is not glTF";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "a file it refers to is missing";
        case cgltf_result_io_error: return "a file it refers to cannot be read";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "glTF 1.0 is not supported";
        default: return "unknown error";
    }
}

struct CgltfDeleter {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};

// Reads every element of an accessor as floats (normalized integers become [0, 1] or [-1, 1]).
std::vector<float> read_floats(const cgltf_accessor* accessor, std::size_t components) {
    if (cgltf_num_components(accessor->type) != components) {
        throw std::runtime_error("an attribute has " + std::to_string(cgltf_num_components(accessor->type)) +
                                 " components instead of " + std::to_string(components));
    }
    std::vector<float> values(accessor->count * components);
    if (cgltf_accessor_unpack_floats(accessor, values.data(), values.size()) != values.size()) {
        throw std::runtime_error("an attribute cannot be read (compressed or out of its buffer)");
    }
    return values;
}

// Smooth normals, area-weighted, for meshes exported without them.
void compute_normals(MeshData& mesh) {
    for (Vertex3D& vertex : mesh.vertices) {
        vertex.normal = glm::vec3(0.0f);
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Vertex3D& a = mesh.vertices[mesh.indices[i]];
        Vertex3D& b = mesh.vertices[mesh.indices[i + 1]];
        Vertex3D& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 face = glm::cross(b.position - a.position, c.position - a.position);  // length = 2 x area
        a.normal += face;
        b.normal += face;
        c.normal += face;
    }
    for (Vertex3D& vertex : mesh.vertices) {
        const float length = glm::length(vertex.normal);
        vertex.normal = length > 0.0f ? vertex.normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

MeshData read_primitive(const cgltf_primitive& primitive) {
    const cgltf_accessor* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
    if (positions == nullptr) {
        throw std::runtime_error("a primitive has no POSITION attribute");
    }
    MeshData mesh;
    const std::vector<float> xyz = read_floats(positions, 3);
    mesh.vertices.resize(positions->count);
    for (std::size_t i = 0; i < positions->count; ++i) {
        mesh.vertices[i].position = {xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]};
    }
    if (const cgltf_accessor* uvs = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0)) {
        const std::vector<float> uv = read_floats(uvs, 2);
        for (std::size_t i = 0; i < mesh.vertices.size() && i < uvs->count; ++i) {
            mesh.vertices[i].uv = {uv[i * 2], uv[i * 2 + 1]};
        }
    }

    if (primitive.indices != nullptr) {
        mesh.indices.resize(primitive.indices->count);
        for (std::size_t i = 0; i < primitive.indices->count; ++i) {
            const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
            if (index >= mesh.vertices.size()) {
                throw std::runtime_error("an index points past the vertices");
            }
            mesh.indices[i] = static_cast<std::uint32_t>(index);
        }
    } else {
        mesh.indices.resize(mesh.vertices.size());
        for (std::size_t i = 0; i < mesh.indices.size(); ++i) {
            mesh.indices[i] = static_cast<std::uint32_t>(i);
        }
    }
    mesh.indices.resize(mesh.indices.size() / 3 * 3);  // a trailing partial triangle means nothing

    if (const cgltf_accessor* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0)) {
        const std::vector<float> n = read_floats(normals, 3);
        for (std::size_t i = 0; i < mesh.vertices.size() && i < normals->count; ++i) {
            mesh.vertices[i].normal = glm::normalize(glm::vec3(n[i * 3], n[i * 3 + 1], n[i * 3 + 2]));
        }
    } else {
        compute_normals(mesh);
    }

    // Tangents as exported (Blender computes them with MikkTSpace too), else computed the same way,
    // as glTF asks.
    if (const cgltf_accessor* tangents = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0)) {
        const std::vector<float> t = read_floats(tangents, 4);
        for (std::size_t i = 0; i < mesh.vertices.size() && i < tangents->count; ++i) {
            mesh.vertices[i].tangent = {t[i * 4], t[i * 4 + 1], t[i * 4 + 2], t[i * 4 + 3] < 0.0f ? -1.0f : 1.0f};
        }
    } else {
        compute_tangents(mesh);
    }
    return mesh;
}

// Decodes a glTF image: inside a buffer (.glb), in a data URI, or in a file next to the model.
Image read_image(const cgltf_image& image, const std::string& base_directory, const std::string& name) {
    if (image.buffer_view != nullptr) {
        const cgltf_buffer_view& view = *image.buffer_view;
        if (view.buffer->data == nullptr) {
            throw std::runtime_error("image '" + name + "' is in a buffer that was not loaded");
        }
        return decode_image(static_cast<const std::uint8_t*>(view.buffer->data) + view.offset, view.size, name);
    }
    if (image.uri == nullptr) {
        throw std::runtime_error("image '" + name + "' has neither data nor URI");
    }
    const std::string uri = image.uri;
    if (uri.rfind("data:", 0) == 0) {
        const std::size_t comma = uri.find(";base64,");
        if (comma == std::string::npos) {
            throw std::runtime_error("image '" + name + "' has a data URI that is not base64");
        }
        const std::string base64 = uri.substr(comma + 8);
        std::size_t padding = 0;
        while (padding < base64.size() && base64[base64.size() - 1 - padding] == '=') {
            ++padding;
        }
        const std::size_t size = base64.size() / 4 * 3 - padding;
        cgltf_options options = {};
        void* bytes = nullptr;
        if (cgltf_load_buffer_base64(&options, size, base64.c_str(), &bytes) != cgltf_result_success) {
            throw std::runtime_error("image '" + name + "' has an invalid base64 data URI");
        }
        try {
            Image decoded = decode_image(bytes, size, name);
            std::free(bytes);  // cgltf allocated it with malloc (no custom allocator given)
            return decoded;
        } catch (...) {
            std::free(bytes);
            throw;
        }
    }
    std::string path = uri;
    cgltf_decode_uri(path.data());  // "%20" and the like
    path.resize(std::strlen(path.c_str()));
    return load_image(base_directory + path);
}

}  // namespace

Aabb ModelData::bounds() const {
    Aabb box;
    for (const ModelPart& part : parts) {
        const Aabb moved = transform_box(part.mesh.bounds(), part.transform);
        if (!moved.empty()) {
            box.add(moved.min);
            box.add(moved.max);
        }
    }
    return box;
}

std::size_t ModelData::triangle_count() const {
    std::size_t count = 0;
    for (const ModelPart& part : parts) {
        count += part.mesh.triangle_count();
    }
    return count;
}

ModelData parse_gltf(const void* data, std::size_t size, const std::string& name, const std::string& base_directory) {
    try {
        cgltf_options options = {};
        options.file.read = read_file;
        options.file.release = release_file;

        cgltf_data* raw = nullptr;
        cgltf_result result = cgltf_parse(&options, data, size, &raw);
        if (result != cgltf_result_success) {
            throw std::runtime_error(describe(result));
        }
        const std::unique_ptr<cgltf_data, CgltfDeleter> gltf(raw);

        if (gltf->extensions_required_count > 0) {
            // Nothing required is supported yet (Draco or meshopt compression, quantized meshes...).
            std::string names;
            for (cgltf_size i = 0; i < gltf->extensions_required_count; ++i) {
                names += (i > 0 ? ", " : "") + std::string(gltf->extensions_required[i]);
            }
            throw std::runtime_error("required extensions not supported: " + names);
        }
        // cgltf builds external paths from the directory of this "file" name.
        const std::string gltf_path = base_directory + "model.gltf";
        result = cgltf_load_buffers(&options, gltf.get(), gltf_path.c_str());
        if (result != cgltf_result_success) {
            throw std::runtime_error(std::string("buffers: ") + describe(result));
        }
        result = cgltf_validate(gltf.get());
        if (result != cgltf_result_success) {
            throw std::runtime_error(describe(result));
        }

        ModelData model;
        // An image is decoded once per use kind: the same file may serve as color and as data.
        std::map<std::pair<const cgltf_image*, bool>, int> image_indices;
        const auto image_of = [&](const cgltf_texture_view& view, bool srgb, const std::string& role) {
            if (view.texture == nullptr || view.texture->image == nullptr) {
                return -1;
            }
            const cgltf_image* image = view.texture->image;
            auto found = image_indices.find({image, srgb});
            if (found == image_indices.end()) {
                std::string image_name = role;
                if (image->name != nullptr) {
                    image_name = image->name;
                } else if (image->uri != nullptr && std::strncmp(image->uri, "data:", 5) != 0) {
                    image_name = image->uri;
                }
                model.images.push_back({image_name, read_image(*image, base_directory, image_name), srgb});
                found = image_indices.emplace(std::make_pair(image, srgb), static_cast<int>(model.images.size() - 1)).first;
            }
            return found->second;
        };
        for (cgltf_size i = 0; i < gltf->materials_count; ++i) {
            const cgltf_material& source = gltf->materials[i];
            ModelMaterial material;
            material.name = source.name != nullptr ? source.name : "material " + std::to_string(i);
            if (source.has_pbr_metallic_roughness) {
                const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
                material.base_color = glm::make_vec4(pbr.base_color_factor);
                material.metallic = pbr.metallic_factor;
                material.roughness = pbr.roughness_factor;
                material.base_color_image = image_of(pbr.base_color_texture, true, material.name + " base color");
                material.metallic_roughness_image = image_of(pbr.metallic_roughness_texture, false, material.name + " metal roughness");
            }
            material.normal_image = image_of(source.normal_texture, false, material.name + " normal");
            if (source.normal_texture.texture != nullptr) {
                material.normal_scale = source.normal_texture.scale;
            }
            material.occlusion_image = image_of(source.occlusion_texture, false, material.name + " occlusion");
            if (source.occlusion_texture.texture != nullptr) {
                material.occlusion_strength = source.occlusion_texture.scale;  // cgltf keeps "strength" here
            }
            material.emissive = glm::make_vec3(source.emissive_factor);
            if (source.has_emissive_strength) {
                material.emissive *= source.emissive_strength.emissive_strength;
            }
            material.emissive_image = image_of(source.emissive_texture, true, material.name + " emissive");
            material.double_sided = source.double_sided;
            model.materials.push_back(std::move(material));
        }

        // The default scene, else the first one, else every root node.
        std::vector<const cgltf_node*> roots;
        const cgltf_scene* scene = gltf->scene != nullptr ? gltf->scene : (gltf->scenes_count > 0 ? &gltf->scenes[0] : nullptr);
        if (scene != nullptr) {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
                roots.push_back(scene->nodes[i]);
            }
        } else {
            for (cgltf_size i = 0; i < gltf->nodes_count; ++i) {
                if (gltf->nodes[i].parent == nullptr) {
                    roots.push_back(&gltf->nodes[i]);
                }
            }
        }

        std::vector<const cgltf_node*> pending(roots.rbegin(), roots.rend());
        while (!pending.empty()) {
            const cgltf_node* node = pending.back();
            pending.pop_back();
            for (cgltf_size i = node->children_count; i > 0; --i) {
                pending.push_back(node->children[i - 1]);  // depth first, in file order
            }
            if (node->mesh == nullptr) {
                continue;
            }
            glm::mat4 world(1.0f);
            cgltf_node_transform_world(node, glm::value_ptr(world));
            const std::string node_name = node->name != nullptr ? node->name : "node";
            const std::string mesh_name = node->mesh->name != nullptr ? node->mesh->name : "mesh";
            for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
                const cgltf_primitive& primitive = node->mesh->primitives[p];
                const std::string part_name = node_name + "/" + mesh_name + "#" + std::to_string(p);
                if (primitive.type != cgltf_primitive_type_triangles) {
                    SDL_Log("Model '%s': part '%s' is not made of triangles, skipped", name.c_str(), part_name.c_str());
                    continue;
                }
                ModelPart part;
                part.name = part_name;
                try {
                    part.mesh = read_primitive(primitive);
                } catch (const std::runtime_error& e) {
                    throw std::runtime_error("part '" + part_name + "': " + e.what());
                }
                part.transform = world;
                part.material = primitive.material != nullptr ? static_cast<int>(primitive.material - gltf->materials) : -1;
                if (!part.mesh.indices.empty()) {
                    model.parts.push_back(std::move(part));
                }
            }
        }
        if (model.parts.empty()) {
            throw std::runtime_error("no triangle mesh in the default scene");
        }
        return model;
    } catch (const std::exception& e) {
        throw std::runtime_error("Model '" + name + "': " + e.what());
    }
}

ModelData load_gltf(const std::string& path) {
    std::size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        throw std::runtime_error("Model '" + path + "': cannot read the file: " + SDL_GetError());
    }
    const std::size_t slash = path.find_last_of("/\\");
    const std::string directory = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    try {
        ModelData model = parse_gltf(data, size, path, directory);  // a .glb's buffer points into `data`
        SDL_free(data);
        return model;
    } catch (...) {
        SDL_free(data);
        throw;
    }
}

Model Model::create(Renderer& renderer, const ModelData& data, const std::string& name) {
    Model model;
    // Colors in sRGB, data (normals, roughness, occlusion) as stored; mipmaps for all; straight alpha
    // (3D surfaces do not blend like sprites).
    for (const ModelImage& image : data.images) {
        TextureSettings settings;
        settings.srgb = image.srgb;
        settings.mipmaps = true;
        settings.premultiply = false;
        model.textures.push_back(std::make_unique<Texture>(renderer.create_texture(image.image, settings, image.name.c_str())));
    }
    const auto texture = [&model](int index) -> const Texture* {
        return index >= 0 ? model.textures[static_cast<std::size_t>(index)].get() : nullptr;
    };
    for (const ModelMaterial& source : data.materials) {
        Material material;
        material.base_color = source.base_color;
        material.metallic = source.metallic;
        material.roughness = source.roughness;
        material.normal_scale = source.normal_scale;
        material.occlusion_strength = source.occlusion_strength;
        material.emissive = source.emissive;
        material.double_sided = source.double_sided;
        material.base_color_texture = texture(source.base_color_image);
        material.metallic_roughness_texture = texture(source.metallic_roughness_image);
        material.normal_texture = texture(source.normal_image);
        material.occlusion_texture = texture(source.occlusion_image);
        material.emissive_texture = texture(source.emissive_image);
        model.materials.push_back(material);
    }
    for (const ModelPart& source : data.parts) {
        Part part;
        part.mesh = Mesh::create(renderer, source.mesh, (name + ":" + source.name).c_str());
        part.transform = source.transform;
        part.material = source.material;
        model.parts.push_back(std::move(part));
    }
    model.bounds = data.bounds();
    model.triangle_count = data.triangle_count();
    return model;
}

Model Model::load(Renderer& renderer, const std::string& path) {
    return create(renderer, load_gltf(path), path);
}

}  // namespace moteur
