// Damaged files (milestone 7, part 10): every reader of the game's files answers a broken one with
// a message naming it, never with a crash; what is the game's to judge (an unknown object type)
// is kept for the game to leave out.
//
// The files fuzzing found (milestone 8, part 8; tools/fuzz/fuzzer.py) are cases here too: the file
// itself when it is small (tests/data/fuzz/), and the damage it carried, made again on a sound file.

#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <ktx.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/animation_set.hpp"
#include "moteur/archive.hpp"
#include "moteur/cbor_check.hpp"
#include "moteur/data_table.hpp"
#include "moteur/file_io.hpp"
#include "moteur/ktx_texture.hpp"
#include "moteur/map_data.hpp"
#include "moteur/map_document.hpp"
#include "moteur/model.hpp"
#include "moteur/particles.hpp"
#include "moteur/save_file.hpp"

#include <nlohmann/json.hpp>

namespace {

const char* const kDamaged[] = {"", "{", "{}", "[]", "null", "{\"version\": 1", "\xEF\xBB\xBF{", "{\"version\": \"un\"}"};

const std::string kTestData = MOTEUR_TEST_DATA;

std::vector<std::uint8_t> read_test_file(const std::string& path) {
    const auto bytes = moteur::read_file_bytes(path);
    REQUIRE_MESSAGE(bytes, "missing test file: " << path);
    return *bytes;
}

std::string temporary(const std::string& name) {
    char* folder = SDL_GetPrefPath("moteur", "tests");
    std::string path = std::string(folder != nullptr ? folder : "") + name;
    SDL_free(folder);
    return path;
}

template <typename T>
void put_le(std::vector<std::uint8_t>& bytes, std::size_t at, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) >> (8 * i));
    }
}

// The archive's index checksum (FNV-1a, as moteur/archive.cpp writes it): a test that damages a size
// of the index fixes the checksum, so that the size itself is what the reader meets.
std::uint64_t fnv1a(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

// A sound 8 x 8 KTX2 file: RGBA8 as it is, or UASTC supercompressed with Zstandard (the form of the
// project's textures, and of the file fuzzing broke).
std::vector<std::uint8_t> make_ktx2(bool uastc_zstd) {
    ktxTextureCreateInfo info = {};
    info.vkFormat = 37;  // VK_FORMAT_R8G8B8A8_UNORM
    info.baseWidth = 8;
    info.baseHeight = 8;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 1;
    info.numLayers = 1;
    info.numFaces = 1;
    ktxTexture2* texture = nullptr;
    REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS);
    std::vector<std::uint8_t> pixels(8 * 8 * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint8_t>(i * 7);
    }
    REQUIRE(ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, pixels.data(), pixels.size()) == KTX_SUCCESS);
    if (uastc_zstd) {
        ktxBasisParams params = {};
        params.structSize = sizeof(params);
        params.uastc = KTX_TRUE;
        params.threadCount = 1;
        params.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT;
        REQUIRE(ktxTexture2_CompressBasisEx(texture, &params) == KTX_SUCCESS);
        REQUIRE(ktxTexture2_DeflateZstd(texture, 18) == KTX_SUCCESS);
    }
    ktx_uint8_t* bytes = nullptr;
    ktx_size_t size = 0;
    REQUIRE(ktxTexture_WriteToMemory(ktxTexture(texture), &bytes, &size) == KTX_SUCCESS);
    std::vector<std::uint8_t> file(bytes, bytes + size);
    std::free(bytes);
    ktxTexture_Destroy(ktxTexture(texture));
    return file;
}

std::uint64_t read_le64(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[at + i]) << (8 * i);
    }
    return value;
}

// Where the KTX2 header keeps the first level's sizes (KTX 2.0, section 3.9).
constexpr std::size_t kKtxLevel0Offset = 80;
constexpr std::size_t kKtxLevel0Length = 88;
constexpr std::size_t kKtxLevel0Uncompressed = 96;

}  // namespace

TEST_CASE("Damaged maps, animation sets and effects give a message naming the file") {
    for (const char* text : kDamaged) {
        CAPTURE(std::string(text));
        CHECK_THROWS_WITH_AS(moteur::MapData::parse(text, "maps/abimee.json"), doctest::Contains("abimee.json"), std::runtime_error);
        CHECK_THROWS_WITH_AS(moteur::AnimationSet::parse(text, "animations/abime.json"), doctest::Contains("abime.json"),
                             std::runtime_error);
        CHECK_THROWS_WITH_AS(moteur::ParticleEffect::parse(text, "effects/abime.json"), doctest::Contains("abime.json"),
                             std::runtime_error);
    }
}

TEST_CASE("A map with an object type the game does not know is read; the type is the game's to judge") {
    const char* text = R"({
      "version": 2, "tiles": { "sol": {} }, "legend": { ".": ["sol"] }, "rows": [ "...", "..." ],
      "objects": [ { "id": "o1", "type": "dragon_inconnu", "x": 1.5, "z": 0.5, "props": { "hauteur": "pas un nombre" } } ]
    })";
    const moteur::MapData map = moteur::MapData::parse(text, "maps/inconnu.json");
    REQUIRE(map.objects().size() == 1);
    CHECK(map.objects()[0].type == "dragon_inconnu");
    // The editor keeps it as it is, and writes it back.
    const moteur::MapDocument doc = moteur::MapDocument::from(map);
    CHECK(doc.to_json().find("dragon_inconnu") != std::string::npos);
    CHECK(moteur::MapData::parse(doc.to_json(), "back.json").objects().size() == 1);
}

TEST_CASE("Damaged saves are refused with a reason, whatever is cut or changed") {
    moteur::SaveGame game;
    game.header.game_version = "essai";
    game.set_section("monde", 1, {{"tick", 42}, {"entites", nlohmann::json::array({1, 2, 3})}});
    for (const moteur::SaveEncoding encoding : {moteur::SaveEncoding::Json, moteur::SaveEncoding::Cbor}) {
        const std::vector<std::uint8_t> bytes = game.encode(encoding);
        std::string error;
        REQUIRE(moteur::SaveGame::decode(bytes, error));
        // Cut anywhere: refused, never read half.
        for (std::size_t size : {std::size_t{0}, std::size_t{3}, bytes.size() / 2, bytes.size() - 1}) {
            const std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
            error.clear();
            CHECK_FALSE(moteur::SaveGame::decode(cut, error));
            CHECK_FALSE(error.empty());
        }
        // One byte changed in the body: the checksum notices.
        std::vector<std::uint8_t> changed = bytes;
        changed[changed.size() - 2] ^= 0x5A;
        error.clear();
        CHECK_FALSE(moteur::SaveGame::decode(changed, error));
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("Fuzzing: an archive whose header or index declares sizes beyond the file is refused before any allocation") {
    // The file fuzzing found: an index of 4 GB declared in a 271-byte file.
    std::string error;
    CHECK(moteur::Archive::open(kTestData + "fuzz/archive_index_demesure.pak", error) == nullptr);
    CHECK(error.find("archive_index_demesure.pak") != std::string::npos);

    const std::string path = temporary("test_robustesse.pak");
    REQUIRE(moteur::write_archive(path, {{"assets/a.json", std::vector<std::uint8_t>(5000, 'x'), true}}, error));
    const std::vector<std::uint8_t> sound = read_test_file(path);

    // The header's index size: far more than the file holds.
    std::vector<std::uint8_t> bytes = sound;
    put_le<std::uint32_t>(bytes, 12, 0xFFFFFFF0u);
    REQUIRE(moteur::write_file_atomic(path, bytes.data(), bytes.size(), error));
    error.clear();
    CHECK(moteur::Archive::open(path, error) == nullptr);
    CHECK(error.find("truncated") != std::string::npos);

    // An entry's decompressed size, the index's checksum made right: the archive opens, the entry
    // is refused when read (its stored frames cannot hold that much).
    const std::size_t index = 24;                       // after the header
    const std::size_t size_field = index + 2 + 13 + 16;  // path length, "assets/a.json", offset, stored size
    bytes = sound;
    put_le<std::uint64_t>(bytes, size_field, 1ull << 40);
    const std::uint32_t index_size = static_cast<std::uint32_t>(bytes[12]) | static_cast<std::uint32_t>(bytes[13]) << 8 |
                                     static_cast<std::uint32_t>(bytes[14]) << 16 | static_cast<std::uint32_t>(bytes[15]) << 24;
    put_le<std::uint64_t>(bytes, 16, fnv1a(bytes.data() + index, index_size));
    REQUIRE(moteur::write_file_atomic(path, bytes.data(), bytes.size(), error));
    error.clear();
    const auto archive = moteur::Archive::open(path, error);
    REQUIRE_MESSAGE(archive != nullptr, error);
    REQUIRE(archive->entries().size() == 1);
    CHECK(archive->entries()[0].size == 1ull << 40);
    CHECK_THROWS_WITH_AS(archive->read(archive->entries()[0]), doctest::Contains("cannot decompress"), std::runtime_error);
}

TEST_CASE("Fuzzing: a glTF skin without joints is refused, not read as joint 0 of an empty palette") {
    // The file fuzzing found: the skinned reference model with its skin's "joints" key damaged.
    const std::vector<std::uint8_t> found = read_test_file(kTestData + "fuzz/gltf_skin_sans_os.glb");
    CHECK_THROWS_WITH_AS(moteur::parse_gltf(found.data(), found.size(), "gltf_skin_sans_os.glb"),
                         doctest::Contains("no joints"), std::runtime_error);

    // The same damage on the sound file: a key of the same length, so the GLB's chunk sizes hold.
    std::vector<std::uint8_t> bytes = read_test_file(std::string(MOTEUR_SOURCE_ASSETS) + "models/skinned_reference.glb");
    CHECK_NOTHROW((void)moteur::parse_gltf(bytes.data(), bytes.size(), "skinned_reference.glb"));
    const std::string key = "\"joints\":";
    const auto at = std::search(bytes.begin(), bytes.end(), key.begin(), key.end());
    REQUIRE(at != bytes.end());
    std::memcpy(&*at, "\"jxints\":", key.size());
    CHECK_THROWS_WITH_AS(moteur::parse_gltf(bytes.data(), bytes.size(), "sans_os.glb"),
                         doctest::Contains("sans_os.glb"), std::runtime_error);
}

TEST_CASE("Fuzzing: a KTX2 header that declares more than its format and size make is refused before libktx allocates") {
    const moteur::CompressedFormats bc{true, true};
    for (const bool uastc_zstd : {false, true}) {
        CAPTURE(uastc_zstd);
        const std::vector<std::uint8_t> sound = make_ktx2(uastc_zstd);
        CHECK_NOTHROW((void)moteur::decode_ktx2(sound.data(), sound.size(), "sain.ktx2", {}, bc));

        // What fuzzing found: a level's uncompressed size of 4 GB (libktx allocated it).
        std::vector<std::uint8_t> bytes = sound;
        put_le<std::uint64_t>(bytes, kKtxLevel0Uncompressed, 0xFFFFFF00ull);
        CHECK_THROWS_WITH_AS(moteur::decode_ktx2(bytes.data(), bytes.size(), "niveau.ktx2", {}, bc),
                             doctest::Contains("niveau.ktx2"), std::runtime_error);
        CHECK_THROWS_AS(moteur::transcode_ktx2_for_shipping(bytes.data(), bytes.size(), "niveau.ktx2"), std::runtime_error);

        // A level beyond the end of the file.
        bytes = sound;
        put_le<std::uint64_t>(bytes, kKtxLevel0Offset, sound.size() - 4);
        CHECK_THROWS_WITH_AS(moteur::decode_ktx2(bytes.data(), bytes.size(), "loin.ktx2", {}, bc),
                             doctest::Contains("beyond the end"), std::runtime_error);
        bytes = sound;
        put_le<std::uint64_t>(bytes, kKtxLevel0Length, ~0ull);
        CHECK_THROWS_AS(moteur::decode_ktx2(bytes.data(), bytes.size(), "long.ktx2", {}, bc), std::runtime_error);

        // Dimensions: none, or more than a GPU takes.
        for (const std::uint32_t width : {0u, 65536u}) {
            bytes = sound;
            put_le<std::uint32_t>(bytes, 20, width);
            CHECK_THROWS_WITH_AS(moteur::decode_ktx2(bytes.data(), bytes.size(), "taille.ktx2", {}, bc),
                                 doctest::Contains("2D textures"), std::runtime_error);
        }
        // More mip levels than the size allows.
        bytes = sound;
        put_le<std::uint32_t>(bytes, 40, 30u);
        CHECK_THROWS_AS(moteur::decode_ktx2(bytes.data(), bytes.size(), "niveaux.ktx2", {}, bc), std::runtime_error);
    }
}

TEST_CASE("Long fuzzing: KTX2 mip levels stored out of order are refused before libktx sizes its data") {
    // A sound 8 x 8 RGBA8 texture with its 4 levels, stored smallest first as the specification says.
    ktxTextureCreateInfo info = {};
    info.vkFormat = 37;  // VK_FORMAT_R8G8B8A8_UNORM
    info.baseWidth = 8;
    info.baseHeight = 8;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 4;
    info.numLayers = 1;
    info.numFaces = 1;
    ktxTexture2* texture = nullptr;
    REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS);
    for (ktx_uint32_t level = 0; level < 4; ++level) {
        const std::size_t side = std::size_t{8} >> level;
        const std::vector<std::uint8_t> pixels(side * side * 4, 200);
        REQUIRE(ktxTexture_SetImageFromMemory(ktxTexture(texture), level, 0, 0, pixels.data(), pixels.size()) == KTX_SUCCESS);
    }
    ktx_uint8_t* written = nullptr;
    ktx_size_t written_size = 0;
    REQUIRE(ktxTexture_WriteToMemory(ktxTexture(texture), &written, &written_size) == KTX_SUCCESS);
    const std::vector<std::uint8_t> sound(written, written + written_size);
    std::free(written);
    ktxTexture_Destroy(ktxTexture(texture));
    const moteur::CompressedFormats bc{true, true};
    CHECK_NOTHROW((void)moteur::decode_ktx2(sound.data(), sound.size(), "sain.ktx2", {}, bc));

    // Level 1 moved onto level 0's place: each level is inside the file and of the right size, but
    // the order is broken (what fuzzing found made libktx's data size wrap around).
    std::vector<std::uint8_t> bytes = sound;
    const std::uint64_t level0_offset = read_le64(bytes, kKtxLevel0Offset);
    put_le<std::uint64_t>(bytes, kKtxLevel0Offset + 24, level0_offset);  // level 1's byteOffset
    CHECK_THROWS_WITH_AS(moteur::decode_ktx2(bytes.data(), bytes.size(), "desordre.ktx2", {}, bc),
                         doctest::Contains("is not stored before"), std::runtime_error);
}

TEST_CASE("Long fuzzing: a glTF animation with key times beyond an hour is refused at once") {
    // The file fuzzing found: its sampler's times ran the 60 Hz sampling of a cubic spline for ever.
    const std::vector<std::uint8_t> found = read_test_file(kTestData + "fuzz/gltf_temps_demesures.glb");
    const auto start = std::chrono::steady_clock::now();
    CHECK_THROWS_AS(moteur::parse_gltf(found.data(), found.size(), "gltf_temps_demesures.glb"), std::runtime_error);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
}

TEST_CASE("Long fuzzing: CBOR nested too deep is refused before it is read (compiled tables, saves)") {
    // Within the limit: what the engine writes, and the forms it may meet (indefinite lengths, tags).
    nlohmann::json document = {{"table", "monstres"}, {"rows", nlohmann::json::array({{{"vie", 50}, {"nom", "squelette"}}})}};
    const std::vector<std::uint8_t> written = nlohmann::json::to_cbor(document);
    std::string error;
    CHECK_MESSAGE(moteur::check_cbor_structure(written.data(), written.size(), error), error);
    const std::vector<std::uint8_t> indefinite = {0x9F, 0x01, 0xBF, 0x61, 'a', 0x02, 0xFF, 0x7F, 0x61, 'x', 0xFF, 0xC1, 0x00, 0xFF};
    CHECK_MESSAGE(moteur::check_cbor_structure(indefinite.data(), indefinite.size(), error), error);

    // Exactly the limit, then one level more.
    const auto nested = [](int depth) {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(depth), 0x81);  // arrays of one item
        bytes.push_back(0x00);
        return bytes;
    };
    std::vector<std::uint8_t> bytes = nested(moteur::kMaxCborDepth);
    CHECK_MESSAGE(moteur::check_cbor_structure(bytes.data(), bytes.size(), error), error);
    bytes = nested(moteur::kMaxCborDepth + 1);
    CHECK_FALSE(moteur::check_cbor_structure(bytes.data(), bytes.size(), error));
    CHECK(error.find("deeper") != std::string::npos);

    // Truncated, a container larger than what is left, a break with nothing to close.
    for (const std::vector<std::uint8_t>& broken : {std::vector<std::uint8_t>{0x82, 0x01},
                                                     std::vector<std::uint8_t>{0x9B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                                                     std::vector<std::uint8_t>{0xFF}}) {
        CHECK_FALSE(moteur::check_cbor_structure(broken.data(), broken.size(), error));
    }

    // What killed the program: 200 000 nested arrays in a compiled table. Now an error naming it.
    std::vector<std::uint8_t> table = {'M', 'D', 'A', 'T', 1, 0, 0, 0};
    const std::vector<std::uint8_t> deep = nested(200000);
    table.insert(table.end(), deep.begin(), deep.end());
    moteur::DataIssues issues;
    const moteur::DataRows rows = moteur::DataTableBase::decode(table, "monstres", issues);
    CHECK(rows.ids.empty());
    CHECK(issues.text().find("deeper") != std::string::npos);
}
