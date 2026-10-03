#include "moteur/model.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/type_ptr.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>

#include "moteur/ktx_texture.hpp"
#include "moteur/paths.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

namespace {

// cgltf reads external buffers through these, so they go through read_file(), which handles
// UTF-8 paths on Windows (cgltf's own fopen does not).
cgltf_result read_buffer_file(const cgltf_memory_options*, const cgltf_file_options*, const char* path,
                              cgltf_size* size, void** data) {
    try {
        FileData file = read_file(path);
        *size = file.size();
        *data = file.release();  // freed by release_file
        return cgltf_result_success;
    } catch (const std::runtime_error&) {
        return cgltf_result_file_not_found;
    }
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

// The decoded URI of a file next to the model ("%20" and the like), empty for data URIs.
std::string file_uri(const char* uri) {
    if (uri == nullptr || std::strncmp(uri, "data:", 5) == 0) {
        return {};
    }
    std::string path = uri;
    cgltf_decode_uri(path.data());
    path.resize(std::strlen(path.c_str()));
    return path;
}

// The encoded bytes of a glTF image (PNG, JPEG or KTX2): inside a buffer (.glb), in a data URI,
// or in a file next to the model.
std::vector<std::uint8_t> read_image_bytes(const cgltf_image& image, const std::string& base_directory,
                                           const std::string& name) {
    if (image.buffer_view != nullptr) {
        const cgltf_buffer_view& view = *image.buffer_view;
        if (view.buffer->data == nullptr) {
            throw std::runtime_error("image '" + name + "' is in a buffer that was not loaded");
        }
        const auto* bytes = static_cast<const std::uint8_t*>(view.buffer->data) + view.offset;
        return std::vector<std::uint8_t>(bytes, bytes + view.size);
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
        const auto* begin = static_cast<const std::uint8_t*>(bytes);
        std::vector<std::uint8_t> decoded(begin, begin + size);
        std::free(bytes);  // cgltf allocated it with malloc (no custom allocator given)
        return decoded;
    }
    const std::string path = base_directory + file_uri(image.uri);
    try {
        const FileData file = read_file(path);
        const auto* begin = static_cast<const std::uint8_t*>(file.data());
        return std::vector<std::uint8_t>(begin, begin + file.size());
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Cannot read image '" + path + "': " + e.what());
    }
}

// A node's transformation relative to its parent. glTF gives either TRS or a matrix (without shear,
// the specification says), which is split here.
JointPose node_pose(const cgltf_node& node) {
    JointPose pose;
    if (node.has_matrix) {
        const glm::mat4 m = glm::make_mat4(node.matrix);
        pose.translation = glm::vec3(m[3]);
        pose.scale = {glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
        if (glm::determinant(glm::mat3(m)) < 0.0f) {
            pose.scale.x = -pose.scale.x;  // a mirror: one negative scale, put on x
        }
        glm::mat3 rotation(m);
        for (int i = 0; i < 3; ++i) {
            rotation[i] = pose.scale[i] != 0.0f ? rotation[i] / pose.scale[i] : rotation[i];
        }
        pose.rotation = glm::normalize(glm::quat_cast(rotation));
        return pose;
    }
    if (node.has_translation) {
        pose.translation = glm::make_vec3(node.translation);
    }
    if (node.has_rotation) {  // glTF: x, y, z, w
        pose.rotation = glm::normalize(glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
    }
    if (node.has_scale) {
        pose.scale = glm::make_vec3(node.scale);
    }
    return pose;
}

// What was adjusted while reading the skins of a model, logged once for the whole model.
struct SkinReport {
    std::size_t over_four = 0;   // vertices with more than four influences (the weakest dropped)
    std::size_t unweighted = 0;  // vertices without any weight (given to the palette's first joint)
};

// The joints and weights of each vertex of a skinned primitive: JOINTS_0 / WEIGHTS_0, and
// JOINTS_1 / WEIGHTS_1 when present; the four strongest kept, normalized to 65535.
std::vector<VertexSkin> read_skin(const cgltf_primitive& primitive, std::size_t vertex_count, std::size_t palette,
                                  SkinReport& report) {
    struct Influence {
        cgltf_uint joint = 0;
        float weight = 0.0f;
    };
    std::vector<std::vector<Influence>> influences(vertex_count);
    for (cgltf_int set = 0;; ++set) {
        const cgltf_accessor* joints = cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, set);
        const cgltf_accessor* weights = cgltf_find_accessor(&primitive, cgltf_attribute_type_weights, set);
        if (joints == nullptr || weights == nullptr) {
            if (set == 0) {
                throw std::runtime_error("a skinned primitive has no JOINTS_0 or WEIGHTS_0");
            }
            break;
        }
        if (joints->count < vertex_count || weights->count < vertex_count) {
            throw std::runtime_error("JOINTS_" + std::to_string(set) + " or WEIGHTS_" + std::to_string(set) +
                                     " has fewer elements than POSITION");
        }
        if (cgltf_num_components(joints->type) != 4) {
            throw std::runtime_error("JOINTS_" + std::to_string(set) + " is not a VEC4");
        }
        const std::vector<float> w = read_floats(weights, 4);
        for (std::size_t v = 0; v < vertex_count; ++v) {
            cgltf_uint indices[4] = {};
            if (!cgltf_accessor_read_uint(joints, v, indices, 4)) {
                throw std::runtime_error("JOINTS_" + std::to_string(set) + " cannot be read");
            }
            for (int k = 0; k < 4; ++k) {
                const float weight = w[v * 4 + static_cast<std::size_t>(k)];
                if (weight <= 0.0f) {
                    continue;
                }
                if (indices[k] >= palette) {
                    throw std::runtime_error("a vertex uses joint " + std::to_string(indices[k]) + " of a skin of " +
                                             std::to_string(palette) + " joints");
                }
                influences[v].push_back({indices[k], weight});
            }
        }
    }

    std::vector<VertexSkin> skin(vertex_count);
    for (std::size_t v = 0; v < vertex_count; ++v) {
        std::vector<Influence>& list = influences[v];
        // Strongest first; equal weights by joint index, so the result never depends on the file's order.
        std::sort(list.begin(), list.end(), [](const Influence& a, const Influence& b) {
            return a.weight != b.weight ? a.weight > b.weight : a.joint < b.joint;
        });
        if (list.size() > 4) {
            ++report.over_four;
            list.resize(4);
        }
        VertexSkin& out = skin[v];
        float sum = 0.0f;
        for (const Influence& influence : list) {
            sum += influence.weight;
        }
        if (list.empty() || sum <= 0.0f) {
            ++report.unweighted;
            continue;  // the default VertexSkin: joint 0, weight 1
        }
        int total = 0;
        for (std::size_t k = 0; k < list.size(); ++k) {
            out.joints[static_cast<int>(k)] = static_cast<std::uint8_t>(list[k].joint);
            const int weight = static_cast<int>(std::lround(list[k].weight / sum * 65535.0f));
            out.weights[static_cast<int>(k)] = static_cast<std::uint16_t>(weight);
            total += weight;
        }
        // Rounding may miss 65535 by a few units: the strongest influence takes the difference.
        out.weights[0] = static_cast<std::uint16_t>(out.weights[0] + (65535 - total));
    }
    return skin;
}

// A glTF animation sampler turned into linear keys, times still those of the file.
template <typename T>
KeyTrack<T> read_keys(const cgltf_animation_sampler& sampler, std::size_t components) {
    const std::vector<float> times = read_floats(sampler.input, 1);
    const std::vector<float> values = read_floats(sampler.output, components);
    const auto value_at = [&](std::size_t element) {
        T value;
        for (std::size_t c = 0; c < components; ++c) {
            value[static_cast<int>(c)] = values[element * components + c];
        }
        return value;
    };
    const bool cubic = sampler.interpolation == cgltf_interpolation_type_cubic_spline;
    if (times.empty() || values.size() / components < times.size() * (cubic ? 3 : 1)) {
        throw std::runtime_error("an animation sampler has fewer values than times");
    }
    for (std::size_t i = 1; i < times.size(); ++i) {
        if (!(times[i] > times[i - 1])) {
            throw std::runtime_error("an animation sampler has times that do not increase");
        }
    }

    KeyTrack<T> keys;
    const auto push = [&keys](float time, const T& value) {
        keys.times.push_back(time);
        keys.values.push_back(value);
    };
    switch (sampler.interpolation) {
        case cgltf_interpolation_type_step: {
            // The value holds until the next key: a key just before it keeps the previous value.
            constexpr float kStepGap = 1e-4f;
            for (std::size_t i = 0; i < times.size(); ++i) {
                if (i > 0 && times[i] - kStepGap > times[i - 1]) {
                    push(times[i] - kStepGap, value_at(i - 1));
                }
                push(times[i], value_at(i));
            }
            break;
        }
        case cgltf_interpolation_type_cubic_spline: {
            // Each key is (in-tangent, value, out-tangent); tangents are scaled by the segment's
            // duration (glTF specification, appendix C). Sampled at 60 Hz and at every key.
            constexpr float kRate = 60.0f;
            const auto element = [&](std::size_t key, std::size_t which) { return value_at(key * 3 + which); };
            for (std::size_t k = 0; k + 1 < times.size(); ++k) {
                const float t0 = times[k];
                const float dt = times[k + 1] - t0;
                const int steps = std::max(1, static_cast<int>(std::ceil(dt * kRate - 1e-3f)));
                for (int s = 0; s < steps; ++s) {
                    const float u = static_cast<float>(s) / static_cast<float>(steps);
                    const float u2 = u * u;
                    const float u3 = u2 * u;
                    const T value = (2.0f * u3 - 3.0f * u2 + 1.0f) * element(k, 1) +
                                    (u3 - 2.0f * u2 + u) * dt * element(k, 2) +
                                    (-2.0f * u3 + 3.0f * u2) * element(k + 1, 1) + (u3 - u2) * dt * element(k + 1, 0);
                    push(t0 + u * dt, value);
                }
            }
            push(times.back(), element(times.size() - 1, 1));
            break;
        }
        default:  // linear
            for (std::size_t i = 0; i < times.size(); ++i) {
                push(times[i], value_at(i));
            }
            break;
    }
    return keys;
}

// glTF stores quaternions as x, y, z, w; glm's constructor takes w first.
KeyTrack<glm::quat> to_rotations(const KeyTrack<glm::vec4>& xyzw) {
    KeyTrack<glm::quat> rotations;
    rotations.times = xyzw.times;
    for (const glm::vec4& q : xyzw.values) {
        const glm::quat rotation(q.w, q.x, q.y, q.z);
        const float length = glm::length(rotation);
        rotations.values.push_back(length > 0.0f ? rotation / length : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }
    return rotations;
}

template <typename T>
void shift_times(KeyTrack<T>& keys, float start) {
    for (float& time : keys.times) {
        time -= start;
    }
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

ModelData parse_gltf(const void* data, std::size_t size, const std::string& name, const std::string& base_directory,
                     const GltfOptions& gltf_options) {
    try {
        cgltf_options options = {};
        options.file.read = read_buffer_file;
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
        for (cgltf_size i = 0; i < gltf->buffers_count; ++i) {
            if (std::string file = file_uri(gltf->buffers[i].uri); !file.empty()) {
                model.files.push_back(std::move(file));
            }
        }
        // An image is decoded once per use kind: the same file may serve as color, as data and as
        // a normal map.
        std::map<std::tuple<const cgltf_image*, bool, bool>, int> image_indices;
        const auto image_of = [&](const cgltf_texture_view& view, bool srgb, const std::string& role, bool normal = false) {
            if (view.texture == nullptr) {
                return -1;
            }
            // KHR_texture_basisu: a KTX2 version of the image, preferred unless told otherwise; the
            // plain image, when there is one, is the fallback for tools that cannot read KTX2.
            const cgltf_image* image = view.texture->image;
            if (view.texture->has_basisu && view.texture->basisu_image != nullptr &&
                (gltf_options.prefer_ktx2 || image == nullptr)) {
                image = view.texture->basisu_image;
            }
            if (image == nullptr) {
                return -1;
            }
            auto found = image_indices.find({image, srgb, normal});
            if (found == image_indices.end()) {
                std::string image_name = role;
                if (image->name != nullptr) {
                    image_name = image->name;
                } else if (image->uri != nullptr && std::strncmp(image->uri, "data:", 5) != 0) {
                    image_name = image->uri;
                }
                ModelImage entry;
                entry.name = image_name;
                entry.srgb = srgb;
                entry.normal = normal;
                entry.file = image->buffer_view == nullptr ? file_uri(image->uri) : std::string();
                if (entry.file.empty() || gltf_options.decode_external_images) {
                    std::vector<std::uint8_t> bytes = read_image_bytes(*image, base_directory, image_name);
                    if (is_ktx2(bytes.data(), bytes.size())) {
                        entry.ktx2 = std::move(bytes);  // decoded for the GPU that will read it (Model::create)
                    } else {
                        entry.image = decode_image(bytes.data(), bytes.size(), image_name);
                    }
                    if (!entry.file.empty()) {
                        model.files.push_back(entry.file);
                    }
                }
                model.images.push_back(std::move(entry));
                found = image_indices.emplace(std::make_tuple(image, srgb, normal), static_cast<int>(model.images.size() - 1)).first;
            }
            return found->second;
        };
        for (cgltf_size i = 0; gltf_options.meshes && i < gltf->materials_count; ++i) {
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
            material.normal_image = image_of(source.normal_texture, false, material.name + " normal", true);
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

        // Every node of the scene, depth first, children in file order.
        std::vector<const cgltf_node*> scene_nodes;
        {
            std::vector<const cgltf_node*> pending(roots.rbegin(), roots.rend());
            while (!pending.empty()) {
                const cgltf_node* node = pending.back();
                pending.pop_back();
                scene_nodes.push_back(node);
                for (cgltf_size i = node->children_count; i > 0; --i) {
                    pending.push_back(node->children[i - 1]);
                }
            }
        }
        const auto index_of = [&gltf](const cgltf_node* node) { return static_cast<std::size_t>(node - gltf->nodes); };
        const auto node_name_of = [&](const cgltf_node* node) {
            return node->name != nullptr && node->name[0] != '\0' ? std::string(node->name)
                                                                  : "node " + std::to_string(index_of(node));
        };

        // The skeleton: the nodes the skins use and the nodes the animations move, with all their
        // ancestors, in the order of the scene (parents first).
        std::vector<char> in_skeleton(gltf->nodes_count, 0);
        const auto mark = [&](const cgltf_node* node) {
            for (; node != nullptr && !in_skeleton[index_of(node)]; node = node->parent) {
                in_skeleton[index_of(node)] = 1;
            }
        };
        for (cgltf_size s = 0; s < gltf->skins_count; ++s) {
            for (cgltf_size j = 0; j < gltf->skins[s].joints_count; ++j) {
                mark(gltf->skins[s].joints[j]);
            }
        }
        for (cgltf_size a = 0; a < gltf->animations_count; ++a) {
            for (cgltf_size c = 0; c < gltf->animations[a].channels_count; ++c) {
                const cgltf_animation_channel& channel = gltf->animations[a].channels[c];
                if (channel.target_path != cgltf_animation_path_type_weights) {
                    mark(channel.target_node);
                }
            }
        }
        std::vector<int> joint_of(gltf->nodes_count, -1);
        for (const cgltf_node* node : scene_nodes) {
            if (!in_skeleton[index_of(node)]) {
                continue;
            }
            JointData joint;
            joint.name = node_name_of(node);
            joint.parent = node->parent != nullptr ? joint_of[index_of(node->parent)] : -1;
            joint.rest = node_pose(*node);
            joint_of[index_of(node)] = static_cast<int>(model.skeleton.joints.size());
            model.skeleton.joints.push_back(std::move(joint));
        }
        const std::vector<glm::mat4> rest_model = model.skeleton.rest_model_matrices();

        for (cgltf_size s = 0; s < gltf->skins_count; ++s) {
            const cgltf_skin& source = gltf->skins[s];
            SkinData skin;
            skin.name = source.name != nullptr ? source.name : "skin " + std::to_string(s);
            if (source.joints_count > 256) {
                throw std::runtime_error("skin '" + skin.name + "' has " + std::to_string(source.joints_count) +
                                         " joints (256 at most)");
            }
            std::vector<float> inverse_binds;
            if (source.inverse_bind_matrices != nullptr) {
                inverse_binds = read_floats(source.inverse_bind_matrices, 16);
                if (inverse_binds.size() < source.joints_count * 16) {
                    throw std::runtime_error("skin '" + skin.name + "' has fewer inverse bind matrices than joints");
                }
            }
            for (cgltf_size j = 0; j < source.joints_count; ++j) {
                const int joint = joint_of[index_of(source.joints[j])];
                if (joint < 0) {
                    throw std::runtime_error("skin '" + skin.name + "' uses node '" + node_name_of(source.joints[j]) +
                                             "', which is not in the default scene");
                }
                skin.joints.push_back(joint);
                skin.inverse_binds.push_back(inverse_binds.empty() ? glm::mat4(1.0f)
                                                                   : glm::make_mat4(&inverse_binds[j * 16]));
            }
            model.skins.push_back(std::move(skin));
        }

        SkinReport skin_report;
        for (const cgltf_node* node : gltf_options.meshes ? scene_nodes : std::vector<const cgltf_node*>{}) {
            if (node->mesh == nullptr) {
                continue;
            }
            glm::mat4 world(1.0f);
            cgltf_node_transform_world(node, glm::value_ptr(world));
            const std::string node_name = node->name != nullptr ? node->name : "node";
            const std::string mesh_name = node->mesh->name != nullptr ? node->mesh->name : "mesh";
            // A mesh without weights under a joint follows the nearest one.
            int carrier = -1;
            for (const cgltf_node* above = node; above != nullptr && carrier < 0; above = above->parent) {
                carrier = joint_of[index_of(above)];
            }
            for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
                const cgltf_primitive& primitive = node->mesh->primitives[p];
                const std::string part_name = node_name + "/" + mesh_name + "#" + std::to_string(p);
                if (primitive.type != cgltf_primitive_type_triangles) {
                    SDL_Log("Model '%s': part '%s' is not made of triangles, skipped", name.c_str(), part_name.c_str());
                    continue;
                }
                ModelPart part;
                part.name = part_name;
                part.node = node_name;
                const bool skinned = node->skin != nullptr &&
                                     cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 0) != nullptr;
                try {
                    part.mesh = read_primitive(primitive);
                    if (skinned) {
                        part.skin = static_cast<int>(node->skin - gltf->skins);
                        part.mesh.skin = read_skin(primitive, part.mesh.vertices.size(), node->skin->joints_count, skin_report);
                    }
                } catch (const std::runtime_error& e) {
                    throw std::runtime_error("part '" + part_name + "': " + e.what());
                }
                if (skinned) {
                    // glTF ignores the node of a skinned mesh: the joints place it. In the rest
                    // pose, a joint's rest matrix times its inverse bind matrix takes the mesh into
                    // the model (the identity when the file was bound in the rest pose; not for a
                    // Z-up rig such as Khronos's RiggedFigure). The first joint of the skin stands
                    // for all: skinning (milestone 5, part 5) does each vertex exactly.
                    const SkinData& skin = model.skins[static_cast<std::size_t>(part.skin)];
                    part.transform = skin.joints.empty() ? glm::mat4(1.0f)
                                                         : rest_model[static_cast<std::size_t>(skin.joints[0])] * skin.inverse_binds[0];
                    // The farthest vertex from its main joint (weights are sorted, strongest first).
                    for (std::size_t v = 0; v < part.mesh.vertices.size(); ++v) {
                        const auto entry = static_cast<std::size_t>(part.mesh.skin[v].joints[0]);
                        const glm::vec3 joint(rest_model[static_cast<std::size_t>(skin.joints[entry])][3]);
                        const glm::vec3 vertex(part.transform * glm::vec4(part.mesh.vertices[v].position, 1.0f));
                        part.skin_radius = std::max(part.skin_radius, glm::length(vertex - joint));
                    }
                } else {
                    part.transform = world;
                    if (carrier >= 0) {
                        part.joint = carrier;
                        part.joint_offset = glm::inverse(rest_model[static_cast<std::size_t>(carrier)]) * world;
                    }
                }
                part.material = primitive.material != nullptr ? static_cast<int>(primitive.material - gltf->materials) : -1;
                if (!part.mesh.indices.empty()) {
                    model.parts.push_back(std::move(part));
                }
            }
        }
        if (skin_report.over_four > 0) {
            SDL_Log("Model '%s': %zu vertices have more than four joint influences; the four strongest are kept",
                    name.c_str(), skin_report.over_four);
        }
        if (skin_report.unweighted > 0) {
            SDL_Log("Model '%s': %zu skinned vertices have no weight; they follow their skin's first joint",
                    name.c_str(), skin_report.unweighted);
        }
        if (gltf_options.meshes && model.parts.empty()) {
            throw std::runtime_error("no triangle mesh in the default scene");
        }

        // Clips: the channels that move skeleton nodes, as linear keys starting at 0.
        std::size_t morph_channels = 0;
        std::set<std::string> clip_names;
        for (cgltf_size a = 0; a < gltf->animations_count && !model.skeleton.empty(); ++a) {
            const cgltf_animation& animation = gltf->animations[a];
            ClipData clip;
            clip.name = animation.name != nullptr && animation.name[0] != '\0' ? animation.name
                                                                              : "animation " + std::to_string(a);
            const std::string base_name = clip.name;
            for (int copy = 2; clip_names.count(clip.name) != 0; ++copy) {
                clip.name = base_name + " (" + std::to_string(copy) + ")";  // names must be unique
            }
            clip.tracks.resize(model.skeleton.joints.size());
            float start = 0.0f;
            float end = 0.0f;
            bool any = false;
            try {
                for (cgltf_size c = 0; c < animation.channels_count; ++c) {
                    const cgltf_animation_channel& channel = animation.channels[c];
                    if (channel.target_path == cgltf_animation_path_type_weights) {
                        ++morph_channels;
                        continue;
                    }
                    if (channel.target_node == nullptr || channel.sampler == nullptr) {
                        continue;
                    }
                    const int joint = joint_of[index_of(channel.target_node)];
                    if (joint < 0) {
                        continue;  // a node outside the default scene
                    }
                    JointTrack& track = clip.tracks[static_cast<std::size_t>(joint)];
                    const cgltf_animation_sampler& sampler = *channel.sampler;
                    float first = 0.0f;
                    float last = 0.0f;
                    switch (channel.target_path) {
                        case cgltf_animation_path_type_translation:
                            track.translations = read_keys<glm::vec3>(sampler, 3);
                            first = track.translations.times.front();
                            last = track.translations.times.back();
                            break;
                        case cgltf_animation_path_type_rotation:
                            track.rotations = to_rotations(read_keys<glm::vec4>(sampler, 4));
                            first = track.rotations.times.front();
                            last = track.rotations.times.back();
                            break;
                        case cgltf_animation_path_type_scale:
                            track.scales = read_keys<glm::vec3>(sampler, 3);
                            first = track.scales.times.front();
                            last = track.scales.times.back();
                            break;
                        default:
                            continue;
                    }
                    start = any ? std::min(start, first) : first;
                    end = any ? std::max(end, last) : last;
                    any = true;
                }
            } catch (const std::runtime_error& e) {
                throw std::runtime_error("animation '" + clip.name + "': " + e.what());
            }
            if (!any) {
                SDL_Log("Model '%s': animation '%s' moves no node of the skeleton, skipped", name.c_str(), clip.name.c_str());
                continue;
            }
            // Blender exports the start of the scene's frame range: the clip starts at its first key.
            for (JointTrack& track : clip.tracks) {
                shift_times(track.translations, start);
                shift_times(track.rotations, start);
                shift_times(track.scales, start);
            }
            constexpr float kPoseDuration = 1.0f / 60.0f;
            clip.duration = end - start > 1e-6f ? end - start : kPoseDuration;
            clip_names.insert(clip.name);
            model.clips.push_back(std::move(clip));
        }
        if (morph_channels > 0) {
            SDL_Log("Model '%s': %zu morph target channels skipped (not supported)", name.c_str(), morph_channels);
        }
        return model;
    } catch (const std::exception& e) {
        throw std::runtime_error("Model '" + name + "': " + e.what());
    }
}

ModelData load_gltf(const std::string& path, const GltfOptions& gltf_options) {
    FileData file;
    try {
        file = read_file(path);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Model '" + path + "': " + e.what());
    }
    const std::size_t slash = path.find_last_of("/\\");
    const std::string directory = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    return parse_gltf(file.data(), file.size(), path, directory, gltf_options);  // a .glb's buffer points into `file`
}

Model Model::create(Renderer& renderer, const ModelData& data, const std::string& name,
                    const TextureSource& texture_source) {
    Model model;
    // Colors in sRGB, data (normals, roughness, occlusion) as stored; mipmaps for all; straight alpha
    // (3D surfaces do not blend like sprites).
    for (const ModelImage& image : data.images) {
        TextureSettings settings;
        settings.srgb = image.srgb;
        settings.mipmaps = true;
        settings.premultiply = false;
        settings.normal_map = image.normal;
        if (texture_source && !image.file.empty()) {
            model.textures.push_back(texture_source(image, settings));
            continue;
        }
        std::shared_ptr<Texture> texture;
        if (!image.ktx2.empty()) {
            const CompressedImage decoded = decode_ktx2(image.ktx2.data(), image.ktx2.size(), image.name, settings,
                                                        renderer.compressed_formats());
            texture = std::make_shared<Texture>(renderer.create_texture(decoded, image.name.c_str()));
        } else if (!image.image.pixels.empty()) {
            texture = std::make_shared<Texture>(renderer.create_texture(image.image, settings, image.name.c_str()));
        } else {
            throw std::runtime_error("Model '" + name + "': image '" + image.name + "' was not decoded");
        }
        model.gpu_bytes += texture->gpu_bytes;
        model.textures.push_back(std::move(texture));
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
        model.gpu_bytes += part.mesh.gpu_bytes;
        part.transform = source.transform;
        part.material = source.material;
        part.node = source.node;
        part.skin = source.skin;
        part.joint = source.joint;
        part.joint_offset = source.joint_offset;
        part.skin_radius = source.skin_radius;
        model.parts.push_back(std::move(part));
    }
    model.skins = data.skins;
    model.joint_count = data.skeleton.joints.size();
    model.bounds = data.bounds();
    model.triangle_count = data.triangle_count();
    return model;
}

Model Model::load(Renderer& renderer, const std::string& path) {
    return create(renderer, load_gltf(path), path);
}

void Model::replace_in_place(Model&& fresh) {
    if (fresh.parts.size() != parts.size() || fresh.materials.size() != materials.size() ||
        fresh.textures.size() != textures.size()) {
        throw std::runtime_error("its structure changed (parts, materials or textures): reload the scene");
    }
    // A texture only this model holds was created by it, and can take the new pixels in place. A
    // shared one (from a cache) must be the very same.
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (fresh.textures[i] != textures[i] && textures[i].use_count() > 1) {
            throw std::runtime_error("it uses another texture file: reload the scene");
        }
    }
    const auto remap = [&](const Texture* texture) -> const Texture* {
        for (std::size_t i = 0; i < fresh.textures.size(); ++i) {
            if (fresh.textures[i].get() == texture) {
                return textures[i].get();
            }
        }
        return texture;
    };
    // Nothing can fail from here on.
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (fresh.textures[i] != textures[i]) {
            *textures[i] = std::move(*fresh.textures[i]);
        }
    }
    for (std::size_t i = 0; i < materials.size(); ++i) {
        Material material = fresh.materials[i];
        material.base_color_texture = remap(material.base_color_texture);
        material.metallic_roughness_texture = remap(material.metallic_roughness_texture);
        material.normal_texture = remap(material.normal_texture);
        material.occlusion_texture = remap(material.occlusion_texture);
        material.emissive_texture = remap(material.emissive_texture);
        materials[i] = material;
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        parts[i] = std::move(fresh.parts[i]);  // each Mesh stays at its address, with the new buffers
    }
    skins = std::move(fresh.skins);
    joint_count = fresh.joint_count;
    bounds = fresh.bounds;
    triangle_count = fresh.triangle_count;
    gpu_bytes = fresh.gpu_bytes;
}

std::vector<std::uint32_t> Model::parts_of(std::string_view node) const {
    std::vector<std::uint32_t> indices;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].node == node) {
            indices.push_back(static_cast<std::uint32_t>(i));
        }
    }
    return indices;
}

}  // namespace moteur
