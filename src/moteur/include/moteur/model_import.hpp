#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "moteur/model.hpp"

namespace moteur {

/// How a model is imported (milestone 9, part 4): the `import` object of its `.meta` file
/// (AssetMeta::import). Every key is optional:
///
/// | Key | Default | Meaning |
/// |---|---|---|
/// | `lods` | 3 | levels of detail made beyond the full mesh (0: none) |
/// | `lod_reduction` | 0.5 | triangles of a level over those of the previous one |
/// | `lod_error` | 0.02 | the largest error of a level, as a share of the model's size (the diagonal of its box) |
/// | `lod_min_triangles` | 96 | parts with fewer triangles get no level of detail |
/// | `optimize` | true | triangles reordered for the GPU's vertex cache, vertices for its fetch |
/// | `textures` | `"ktx2"` | the images inside the model encoded to KTX2 (UASTC, mipmaps); `"keep"`: as they are |
/// | `collision` | `"named"` | its collision geometry (ModelData::collision): `"named"`, the meshes of its `COL_` / `UCX_` nodes; `"visible"`, those and every visible triangle; `"none"` |
///
/// Collision nodes are never drawn, whatever the setting. A collision node's kind comes from its
/// name (surface_kind_of_node(): `COL_wall`, `COL_walk`, `COL_obstacle`, `COL_void`); otherwise, and for
/// visible triangles, from the slope: walkable up to 45 degrees, a wall beyond.
struct ModelImportSettings {
    /// Where the collision geometry comes from (`collision`).
    enum class Collision : std::uint8_t {
        Named,    ///< the collision nodes only
        Visible,  ///< the collision nodes and every visible triangle
        None,     ///< none (collision nodes are still left out of what is drawn)
    };
    int lods = 3;                 ///< see the table
    float lod_reduction = 0.5f;   ///< see the table
    float lod_error = 0.02f;      ///< see the table
    int lod_min_triangles = 96;   ///< see the table
    bool optimize = true;         ///< see the table
    bool compress_textures = true;  ///< `textures`: "ktx2"
    Collision collision = Collision::Named;  ///< `collision`

    /// Reads them from a `.meta`'s `import` object; a key of the wrong type or out of bounds keeps
    /// its default, with a message in `warnings`; unknown keys are warnings too.
    static ModelImportSettings from_json(const nlohmann::json& import, std::vector<std::string>& warnings);
    /// What they change in the result: part of an imported model's stamp (an import with other
    /// settings is another result).
    std::uint64_t hash() const;
};

/// Imports a model read from glTF (load_gltf(), its embedded images decoded) for the engine: its
/// clips built once into their playable form (ModelData::prepared_clips, ClipLibrary::save()); each
/// part's levels of detail (simplify(): coarser levels as ranges of MeshData::indices over the same
/// vertices), its triangles in the vertex cache's order and its vertices in the order of their first
/// use, and its embedded images encoded to KTX2 (encode_ktx2(): sRGB for colors, two channels for
/// normal maps). Images in their own files stay references (ModelImage::file): textures of their own.
/// What draws is unchanged; the levels are what the renderer picks from (MeshRenderer). Long (the
/// encoding of textures): on a worker thread or in a tool. `name` appears in messages.
void import_model(ModelData& data, const ModelImportSettings& settings, const std::string& name);

/// The prepared form of an imported model (`.mmdl`): `MMDL`, its format version (u32), the stamp of
/// what it was made from (`source_stamp`: the source files' content, the import settings and the
/// importer's version, see Assets), then the whole ModelData in binary (parts with their levels,
/// materials, images, skeleton, skins, clips, the files it came from). What the game loads instead
/// of the glTF, in development (from the import cache) and in the shipped game (from the archives).
std::vector<std::uint8_t> encode_prepared_model(const ModelData& data, std::uint64_t source_stamp);

/// Reads a `.mmdl` (encode_prepared_model()). Throws std::runtime_error naming `name` for another
/// format or version, or a damaged file. `source_stamp`, if not null, gets its stamp.
ModelData decode_prepared_model(const void* data, std::size_t size, const std::string& name, std::uint64_t* source_stamp = nullptr);

/// The stamp a `.mmdl` only reads (without the model): its version must be kPreparedModelVersion;
/// otherwise, or for anything that is not a `.mmdl`, nullopt-like 0.
std::uint64_t prepared_model_stamp(const void* data, std::size_t size);

/// The version of the `.mmdl` format, and of the importer: a change of either makes every imported
/// model be imported again.
inline constexpr std::uint32_t kPreparedModelVersion = 3;  // 2: the clips in their ozz form; 3: the collision geometry

}  // namespace moteur
