#include "moteur/model_import.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "moteur/binary_io.hpp"
#include "moteur/ktx_texture.hpp"
#include "moteur/mesh_optimize.hpp"
#include "moteur/skeleton.hpp"

namespace moteur {

namespace {

const char kMagic[4] = {'M', 'M', 'D', 'L'};

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        hash = (hash ^ ((value >> (8 * i)) & 0xFF)) * 1099511628211ull;
    }
    return hash;
}

std::uint64_t bits(float value) {
    std::uint32_t b;
    std::memcpy(&b, &value, sizeof(b));
    return b;
}

// The levels of detail of one part, its triangles and vertices reordered.
void import_part(ModelPart& part, const ModelImportSettings& settings, float max_error) {
    MeshData& mesh = part.mesh;
    const std::size_t vertex_count = mesh.vertices.size();
    std::vector<std::vector<std::uint32_t>> levels;
    std::vector<float> errors;
    levels.push_back(mesh.indices);
    errors.push_back(0.0f);
    if (settings.lods > 0 && mesh.indices.size() / 3 >= static_cast<std::size_t>(settings.lod_min_triangles)) {
        std::vector<glm::vec3> positions(vertex_count);
        for (std::size_t v = 0; v < vertex_count; ++v) {
            positions[v] = mesh.vertices[v].position;
        }
        for (int level = 1; level <= settings.lods; ++level) {
            const std::vector<std::uint32_t>& previous = levels.back();
            const auto target = static_cast<std::size_t>(static_cast<float>(previous.size()) * settings.lod_reduction) / 3 * 3;
            const SimplifyResult simplified = simplify(positions, previous, target, max_error);
            // A level that gains less than a tenth is not worth its memory: the levels stop there.
            if (simplified.indices.empty() || simplified.indices.size() * 10 > previous.size() * 9) {
                break;
            }
            levels.push_back(simplified.indices);
            errors.push_back(std::max(simplified.error, errors.back()));
        }
    }
    if (settings.optimize) {
        for (std::vector<std::uint32_t>& level : levels) {
            optimize_vertex_cache(level, vertex_count);
        }
        // The vertices in the order the full level first uses them; every level remapped.
        const std::vector<std::uint32_t> order = vertex_fetch_order(levels.front(), vertex_count);
        std::vector<std::uint32_t> remap(vertex_count);
        for (std::size_t i = 0; i < order.size(); ++i) {
            remap[order[i]] = static_cast<std::uint32_t>(i);
        }
        std::vector<Vertex3D> vertices(vertex_count);
        for (std::size_t i = 0; i < order.size(); ++i) {
            vertices[i] = mesh.vertices[order[i]];
        }
        mesh.vertices = std::move(vertices);
        if (!mesh.skin.empty()) {
            std::vector<VertexSkin> skin(vertex_count);
            for (std::size_t i = 0; i < order.size(); ++i) {
                skin[i] = mesh.skin[order[i]];
            }
            mesh.skin = std::move(skin);
        }
        for (std::vector<std::uint32_t>& level : levels) {
            for (std::uint32_t& index : level) {
                index = remap[index];
            }
        }
    }
    mesh.indices.clear();
    mesh.lods.clear();
    for (std::size_t l = 0; l < levels.size(); ++l) {
        mesh.lods.push_back({static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(levels[l].size()), errors[l]});
        mesh.indices.insert(mesh.indices.end(), levels[l].begin(), levels[l].end());
    }
}

void write_track(BinaryWriter& out, const KeyTrack<glm::vec3>& track) {
    out.array(track.times);
    out.array(track.values);
}
void write_track(BinaryWriter& out, const KeyTrack<glm::quat>& track) {
    out.array(track.times);
    out.array(track.values);
}
template <typename T>
KeyTrack<T> read_track(BinaryReader& in) {
    KeyTrack<T> track;
    track.times = in.array<float>();
    track.values = in.array<T>();
    if (track.times.size() != track.values.size()) {
        in.fail("a track has " + std::to_string(track.times.size()) + " times and " + std::to_string(track.values.size()) +
                " values");
    }
    return track;
}

}  // namespace

ModelImportSettings ModelImportSettings::from_json(const nlohmann::json& import, std::vector<std::string>& warnings) {
    using Collision = ModelImportSettings::Collision;
    ModelImportSettings settings;
    if (!import.is_object()) {
        return settings;
    }
    for (const auto& item : import.items()) {
        const std::string& key = item.key();
        const nlohmann::json& value = item.value();
        const auto number = [&key, &value, &warnings](double min, double max) -> std::optional<double> {
            if (!value.is_number() || value.get<double>() < min || value.get<double>() > max) {
                warnings.push_back(std::string("import > ").append(key).append(": expected a number in [").append(std::to_string(min))
                                       .append(", ").append(std::to_string(max)).append("], default kept"));
                return std::nullopt;
            }
            return value.get<double>();
        };
        if (key == "lods") {
            if (const auto v = number(0, 8)) {
                settings.lods = static_cast<int>(*v);
            }
        } else if (key == "lod_reduction") {
            if (const auto v = number(0.05, 0.95)) {
                settings.lod_reduction = static_cast<float>(*v);
            }
        } else if (key == "lod_error") {
            if (const auto v = number(0.0, 1.0)) {
                settings.lod_error = static_cast<float>(*v);
            }
        } else if (key == "lod_min_triangles") {
            if (const auto v = number(0, 1e9)) {
                settings.lod_min_triangles = static_cast<int>(*v);
            }
        } else if (key == "optimize") {
            if (value.is_boolean()) {
                settings.optimize = value.get<bool>();
            } else {
                warnings.push_back("import > optimize: expected true or false, default kept");
            }
        } else if (key == "collision") {
            if (value == "named") {
                settings.collision = Collision::Named;
            } else if (value == "visible") {
                settings.collision = Collision::Visible;
            } else if (value == "none") {
                settings.collision = Collision::None;
            } else {
                warnings.push_back("import > collision: expected \"named\", \"visible\" or \"none\", default kept");
            }
        } else if (key == "textures") {
            if (value == "ktx2") {
                settings.compress_textures = true;
            } else if (value == "keep") {
                settings.compress_textures = false;
            } else {
                warnings.push_back("import > textures: expected \"ktx2\" or \"keep\", default kept");
            }
        } else if (!key.empty() && key[0] != '_') {
            warnings.push_back("import > " + key + ": unknown setting (typo?)");
        }
    }
    return settings;
}

std::uint64_t ModelImportSettings::hash() const {
    std::uint64_t h = 14695981039346656037ull;
    h = mix(h, static_cast<std::uint64_t>(lods));
    h = mix(h, bits(lod_reduction));
    h = mix(h, bits(lod_error));
    h = mix(h, static_cast<std::uint64_t>(lod_min_triangles));
    h = mix(h, optimize ? 1 : 0);
    h = mix(h, compress_textures ? 1 : 0);
    h = mix(h, static_cast<std::uint64_t>(collision) + 7);
    return h;
}

namespace {

// The kind of a triangle by its slope: walkable up to 45 degrees, a wall beyond (and facing down).
SurfaceKind kind_by_slope(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const glm::vec3 n = glm::cross(b - a, c - a);
    const float length = glm::length(n);
    return length > 0.0f && n.y / length >= 0.7071f ? SurfaceKind::Walkable : SurfaceKind::Wall;
}

// Appends a part's triangles in model space; `kind` or, if empty, the slope of each.
void add_part_collision(CollisionMeshData& out, const ModelPart& part, std::optional<SurfaceKind> kind) {
    const auto first = static_cast<std::uint32_t>(out.vertices.size());
    for (const Vertex3D& v : part.mesh.vertices) {
        out.vertices.emplace_back(part.transform * glm::vec4(v.position, 1.0f));
    }
    const bool mirrored = glm::determinant(glm::mat3(part.transform)) < 0.0f;
    // The full mesh only: indices past its first level of detail are coarser copies.
    const std::size_t count = part.mesh.lods.empty() ? part.mesh.indices.size() : part.mesh.lods.front().index_count;
    for (std::size_t i = 0; i + 2 < count; i += 3) {
        const std::uint32_t ia = first + part.mesh.indices[i];
        const std::uint32_t ib = first + part.mesh.indices[mirrored ? i + 2 : i + 1];
        const std::uint32_t ic = first + part.mesh.indices[mirrored ? i + 1 : i + 2];
        out.indices.insert(out.indices.end(), {ia, ib, ic});
        out.kinds.push_back(kind ? *kind : kind_by_slope(out.vertices[ia], out.vertices[ib], out.vertices[ic]));
    }
}

}  // namespace

void import_model(ModelData& data, const ModelImportSettings& settings, const std::string& name) {
    // Collision nodes first: never drawn, and not part of the model's size.
    data.collision = CollisionMeshData{};
    std::vector<ModelPart> drawn;
    drawn.reserve(data.parts.size());
    for (ModelPart& part : data.parts) {
        if (is_collision_node(part.node)) {
            if (settings.collision != ModelImportSettings::Collision::None && part.skin < 0) {
                add_part_collision(data.collision, part, surface_kind_of_node(part.node));
            }
        } else {
            drawn.push_back(std::move(part));
        }
    }
    data.parts = std::move(drawn);
    if (settings.collision == ModelImportSettings::Collision::Visible) {
        for (const ModelPart& part : data.parts) {
            if (part.skin < 0) {
                add_part_collision(data.collision, part, std::nullopt);
            }
        }
    }
    const Aabb box = data.bounds();
    const float size = box.empty() ? 1.0f : glm::length(box.max - box.min);
    const float max_error = std::max(settings.lod_error * size, 1e-6f);
    for (ModelPart& part : data.parts) {
        if (part.mesh.indices.empty()) {
            continue;
        }
        import_part(part, settings, max_error);
    }
    if (!data.clips.empty()) {
        data.prepared_clips = ClipLibrary::create(data.skeleton, data.clips, name).save();
        data.clips.clear();
    }
    if (settings.compress_textures) {
        for (ModelImage& image : data.images) {
            if (image.file.empty() && image.ktx2.empty() && !image.image.pixels.empty()) {
                image.ktx2 = encode_ktx2(image.image, image.srgb, image.normal, name + " > " + image.name);
                image.image = Image{};
            }
        }
    }
}

std::vector<std::uint8_t> encode_prepared_model(const ModelData& data, std::uint64_t source_stamp) {
    BinaryWriter out;
    out.raw(kMagic, 4);
    out.u32(kPreparedModelVersion);
    out.value(source_stamp);

    out.u32(static_cast<std::uint32_t>(data.parts.size()));
    for (const ModelPart& part : data.parts) {
        out.text(part.name);
        out.text(part.node);
        out.array(part.mesh.vertices);
        out.array(part.mesh.indices);
        out.array(part.mesh.skin);
        out.array(part.mesh.lods);
        out.mat4(part.transform);
        out.value<std::int32_t>(part.material);
        out.value<std::int32_t>(part.skin);
        out.value<std::int32_t>(part.joint);
        out.mat4(part.joint_offset);
        out.f32(part.skin_radius);
    }
    out.u32(static_cast<std::uint32_t>(data.materials.size()));
    for (const ModelMaterial& m : data.materials) {
        out.text(m.name);
        out.vec4(m.base_color);
        out.f32(m.metallic);
        out.f32(m.roughness);
        out.f32(m.normal_scale);
        out.f32(m.occlusion_strength);
        out.vec3(m.emissive);
        out.value<std::uint8_t>(m.double_sided ? 1 : 0);
        out.f32(m.alpha_cutoff);
        out.value<std::uint8_t>(m.blend ? 1 : 0);
        for (const int image : {m.base_color_image, m.metallic_roughness_image, m.normal_image, m.occlusion_image, m.emissive_image}) {
            out.value<std::int32_t>(image);
        }
    }
    out.u32(static_cast<std::uint32_t>(data.images.size()));
    for (const ModelImage& image : data.images) {
        out.text(image.name);
        out.value<std::int32_t>(image.image.width);
        out.value<std::int32_t>(image.image.height);
        out.blob(image.image.pixels);
        out.blob(image.ktx2);
        out.value<std::uint8_t>(image.srgb ? 1 : 0);
        out.value<std::uint8_t>(image.normal ? 1 : 0);
        out.text(image.file);
    }
    out.u32(static_cast<std::uint32_t>(data.skeleton.joints.size()));
    for (const JointData& joint : data.skeleton.joints) {
        out.text(joint.name);
        out.value<std::int32_t>(joint.parent);
        out.vec3(joint.rest.translation);
        out.quat(joint.rest.rotation);
        out.vec3(joint.rest.scale);
    }
    out.u32(static_cast<std::uint32_t>(data.skins.size()));
    for (const SkinData& skin : data.skins) {
        out.text(skin.name);
        out.array(skin.joints);
        out.array(skin.inverse_binds);
    }
    out.u32(static_cast<std::uint32_t>(data.clips.size()));
    for (const ClipData& clip : data.clips) {
        out.text(clip.name);
        out.f32(clip.duration);
        out.u32(static_cast<std::uint32_t>(clip.tracks.size()));
        for (const JointTrack& track : clip.tracks) {
            write_track(out, track.translations);
            write_track(out, track.rotations);
            write_track(out, track.scales);
        }
    }
    out.blob(data.prepared_clips);
    out.u32(static_cast<std::uint32_t>(data.files.size()));
    for (const std::string& file : data.files) {
        out.text(file);
    }
    out.array(data.collision.vertices);
    out.array(data.collision.indices);
    out.array(data.collision.kinds);
    return std::move(out.bytes());
}

std::uint64_t prepared_model_stamp(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if (size < 16 || std::memcmp(bytes, kMagic, 4) != 0) {
        return 0;
    }
    std::uint32_t version;
    std::memcpy(&version, bytes + 4, 4);
    if (version != kPreparedModelVersion) {
        return 0;
    }
    std::uint64_t stamp;
    std::memcpy(&stamp, bytes + 8, 8);
    return stamp;
}

ModelData decode_prepared_model(const void* data, std::size_t size, const std::string& name, std::uint64_t* source_stamp) {
    BinaryReader in(static_cast<const std::uint8_t*>(data), size, "Model '" + name + "'");
    char magic[4];
    in.raw(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) {
        in.fail("not a prepared model (MMDL)");
    }
    const std::uint32_t version = in.u32();
    if (version != kPreparedModelVersion) {
        in.fail("prepared model format " + std::to_string(version) + " (expected " + std::to_string(kPreparedModelVersion) + ")");
    }
    const auto stamp = in.value<std::uint64_t>();
    if (source_stamp != nullptr) {
        *source_stamp = stamp;
    }
    ModelData model;
    const std::uint32_t parts = in.count(64);
    for (std::uint32_t p = 0; p < parts; ++p) {
        ModelPart part;
        part.name = in.text();
        part.node = in.text();
        part.mesh.vertices = in.array<Vertex3D>();
        part.mesh.indices = in.array<std::uint32_t>();
        part.mesh.skin = in.array<VertexSkin>();
        part.mesh.lods = in.array<MeshLod>();
        part.transform = in.mat4();
        part.material = in.value<std::int32_t>();
        part.skin = in.value<std::int32_t>();
        part.joint = in.value<std::int32_t>();
        part.joint_offset = in.mat4();
        part.skin_radius = in.f32();
        // What the rest of the engine relies on: indices within the vertices, levels within the indices.
        if (part.mesh.vertices.empty() || part.mesh.indices.empty() || part.mesh.indices.size() % 3 != 0) {
            in.fail("part '" + part.name + "' has no triangles");
        }
        for (const std::uint32_t index : part.mesh.indices) {
            if (index >= part.mesh.vertices.size()) {
                in.fail("part '" + part.name + "' has an index beyond its vertices");
            }
        }
        if (!part.mesh.skin.empty() && part.mesh.skin.size() != part.mesh.vertices.size()) {
            in.fail("part '" + part.name + "' has a skin of another size than its vertices");
        }
        for (const MeshLod& lod : part.mesh.lods) {
            if (lod.index_count == 0 || lod.index_count % 3 != 0 ||
                static_cast<std::uint64_t>(lod.first_index) + lod.index_count > part.mesh.indices.size()) {
                in.fail("part '" + part.name + "' has a level of detail beyond its indices");
            }
        }
        if (part.mesh.lods.size() > 255) {
            in.fail("part '" + part.name + "' has too many levels of detail");
        }
        model.parts.push_back(std::move(part));
    }
    const std::uint32_t materials = in.count(64);
    for (std::uint32_t m = 0; m < materials; ++m) {
        ModelMaterial material;
        material.name = in.text();
        material.base_color = in.vec4();
        material.metallic = in.f32();
        material.roughness = in.f32();
        material.normal_scale = in.f32();
        material.occlusion_strength = in.f32();
        material.emissive = in.vec3();
        material.double_sided = in.value<std::uint8_t>() != 0;
        material.alpha_cutoff = in.f32();
        material.blend = in.value<std::uint8_t>() != 0;
        for (int* image : {&material.base_color_image, &material.metallic_roughness_image, &material.normal_image,
                           &material.occlusion_image, &material.emissive_image}) {
            *image = in.value<std::int32_t>();
        }
        model.materials.push_back(std::move(material));
    }
    const std::uint32_t images = in.count(16);
    for (std::uint32_t i = 0; i < images; ++i) {
        ModelImage image;
        image.name = in.text();
        image.image.width = in.value<std::int32_t>();
        image.image.height = in.value<std::int32_t>();
        image.image.pixels = in.blob();
        if (image.image.width < 0 || image.image.height < 0 ||
            image.image.pixels.size() != static_cast<std::size_t>(image.image.width) * static_cast<std::size_t>(image.image.height) * 4) {
            in.fail("image '" + image.name + "' has a wrong size");
        }
        image.ktx2 = in.blob();
        image.srgb = in.value<std::uint8_t>() != 0;
        image.normal = in.value<std::uint8_t>() != 0;
        image.file = in.text();
        model.images.push_back(std::move(image));
    }
    for (const ModelMaterial& material : model.materials) {
        for (const int image : {material.base_color_image, material.metallic_roughness_image, material.normal_image,
                                material.occlusion_image, material.emissive_image}) {
            if (image < -1 || image >= static_cast<int>(model.images.size())) {
                in.fail("material '" + material.name + "' names a missing image");
            }
        }
    }
    const std::uint32_t joints = in.count(48);
    for (std::uint32_t j = 0; j < joints; ++j) {
        JointData joint;
        joint.name = in.text();
        joint.parent = in.value<std::int32_t>();
        if (joint.parent < -1 || joint.parent >= static_cast<std::int32_t>(j)) {
            in.fail("joint '" + joint.name + "' comes before its parent");
        }
        joint.rest.translation = in.vec3();
        joint.rest.rotation = in.quat();
        joint.rest.scale = in.vec3();
        model.skeleton.joints.push_back(std::move(joint));
    }
    const std::uint32_t skins = in.count(12);
    for (std::uint32_t s = 0; s < skins; ++s) {
        SkinData skin;
        skin.name = in.text();
        skin.joints = in.array<int>();
        skin.inverse_binds = in.array<glm::mat4>();
        if (skin.joints.size() != skin.inverse_binds.size() || skin.joints.empty() || skin.joints.size() > 256) {
            in.fail("skin '" + skin.name + "' is malformed");
        }
        for (const int joint : skin.joints) {
            if (joint < 0 || joint >= static_cast<int>(model.skeleton.joints.size())) {
                in.fail("skin '" + skin.name + "' names a missing joint");
            }
        }
        model.skins.push_back(std::move(skin));
    }
    for (const ModelPart& part : model.parts) {
        if (part.material < -1 || part.material >= static_cast<int>(model.materials.size()) || part.skin < -1 ||
            part.skin >= static_cast<int>(model.skins.size()) || part.joint < -1 ||
            part.joint >= static_cast<int>(model.skeleton.joints.size())) {
            in.fail("part '" + part.name + "' names a missing material, skin or joint");
        }
        if (part.skin >= 0) {
            const std::size_t palette = model.skins[static_cast<std::size_t>(part.skin)].joints.size();
            for (const VertexSkin& vertex : part.mesh.skin) {
                for (int k = 0; k < 4; ++k) {
                    if (vertex.weights[k] > 0 && vertex.joints[k] >= palette) {
                        in.fail("part '" + part.name + "' has a vertex on a joint beyond its skin");
                    }
                }
            }
        }
    }
    const std::uint32_t clips = in.count(12);
    for (std::uint32_t c = 0; c < clips; ++c) {
        ClipData clip;
        clip.name = in.text();
        clip.duration = in.f32();
        if (!std::isfinite(clip.duration) || clip.duration <= 0.0f || clip.duration > 3600.0f) {
            in.fail("clip '" + clip.name + "' has a wrong duration");
        }
        const std::uint32_t tracks = in.count(24);
        if (tracks != model.skeleton.joints.size()) {
            in.fail("clip '" + clip.name + "' has a track count that is not the joint count");
        }
        for (std::uint32_t t = 0; t < tracks; ++t) {
            JointTrack track;
            track.translations = read_track<glm::vec3>(in);
            track.rotations = read_track<glm::quat>(in);
            track.scales = read_track<glm::vec3>(in);
            for (const std::vector<float>* times : {&track.translations.times, &track.rotations.times, &track.scales.times}) {
                for (std::size_t k = 0; k < times->size(); ++k) {
                    if (!std::isfinite((*times)[k]) || (*times)[k] < 0.0f || (*times)[k] > 3600.0f ||
                        (k > 0 && (*times)[k] <= (*times)[k - 1])) {
                        in.fail("clip '" + clip.name + "' has key times out of order");
                    }
                }
            }
            clip.tracks.push_back(std::move(track));
        }
        model.clips.push_back(std::move(clip));
    }
    model.prepared_clips = in.blob();
    const std::uint32_t files = in.count(4);
    for (std::uint32_t f = 0; f < files; ++f) {
        model.files.push_back(in.text());
    }
    model.collision.vertices = in.array<glm::vec3>();
    model.collision.indices = in.array<std::uint32_t>();
    model.collision.kinds = in.array<SurfaceKind>();
    try {
        model.collision.validate("collision");
    } catch (const std::runtime_error& e) {
        in.fail(e.what());
    }
    if (in.left() != 0) {
        in.fail("bytes after the end");
    }
    return model;
}

}  // namespace moteur
