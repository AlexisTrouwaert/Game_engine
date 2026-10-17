// Regression tests of the defects found while documenting the API (milestone 8, part 9): platform.

#include <doctest/doctest.h>

#include <SDL3/SDL.h>
#include <ktx.h>

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "moteur/application.hpp"
#include "moteur/crash.hpp"
#include "moteur/display.hpp"
#include "moteur/ktx_texture.hpp"
#include "moteur/model.hpp"

namespace {

std::filesystem::path fresh_folder(const char* name) {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "moteur_review_platform" / name;
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder);
    return folder;
}

std::string read_all(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// MOTEUR_CRASH_SILENT for the duration of a test: no message box to click.
struct SilentCrashReports {
    bool was_set = SDL_getenv("MOTEUR_CRASH_SILENT") != nullptr;
    SilentCrashReports() { SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "MOTEUR_CRASH_SILENT", "1", true); }
    ~SilentCrashReports() {
        if (!was_set) {
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "MOTEUR_CRASH_SILENT");
        }
    }
};

void test_terminate() {
    std::abort();
}

}  // namespace

TEST_CASE("review platform: reports of the same second keep their own files, .txt and .dmp under one stem") {
    const SilentCrashReports silent;
    const std::filesystem::path folder = fresh_folder("same_second");
    moteur::install_crash_reporter(folder.string(), "review");
    // Three in a row take well under a second: at least two of them share their second.
    for (const char* what : {"first event", "second event", "third event"}) {
        moteur::report_fatal_error(what, "");
    }
    moteur::uninstall_crash_reporter();

    std::set<std::string> texts;
    std::set<std::string> dumps;
    std::set<std::string> events;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        const std::string stem = entry.path().stem().string();
        if (entry.path().extension() == ".txt") {
            texts.insert(stem);
            const std::string text = read_all(entry.path());
            for (const char* what : {"first event", "second event", "third event"}) {
                if (text.find(std::string("event: ") + what) != std::string::npos) {
                    events.insert(what);
                }
            }
        } else if (entry.path().extension() == ".dmp") {
            dumps.insert(stem);
        }
    }
    CHECK(texts.size() == 3);
    CHECK(events.size() == 3);  // none overwrote another
#if defined(_WIN32)
    CHECK(dumps == texts);  // each .dmp next to its .txt, same stem
#endif
    CHECK(std::any_of(texts.begin(), texts.end(), [](const std::string& stem) { return stem.ends_with("_2"); }));
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}

TEST_CASE("review platform: uninstall_crash_reporter gives back the handlers it replaced") {
    const SilentCrashReports silent;
    const std::filesystem::path folder = fresh_folder("uninstall");
    const std::terminate_handler previous_terminate = std::set_terminate(test_terminate);
#if defined(_WIN32)
    const LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = SetUnhandledExceptionFilter(nullptr);
    SetUnhandledExceptionFilter(previous_filter);
    const auto previous_abort = std::signal(SIGABRT, SIG_DFL);
    std::signal(SIGABRT, previous_abort);
#else
    struct sigaction previous_segv = {};
    sigaction(SIGSEGV, nullptr, &previous_segv);
#endif

    moteur::uninstall_crash_reporter();  // not installed: nothing
    CHECK(std::get_terminate() == test_terminate);

    moteur::install_crash_reporter(folder.string(), "review");
    CHECK_FALSE(moteur::crash_reports_directory().empty());
    CHECK(std::get_terminate() != test_terminate);
    moteur::uninstall_crash_reporter();
    CHECK(moteur::crash_reports_directory().empty());
    CHECK(std::get_terminate() == test_terminate);
#if defined(_WIN32)
    CHECK(SetUnhandledExceptionFilter(previous_filter) == previous_filter);
    CHECK(std::signal(SIGABRT, previous_abort) == previous_abort);
#else
    struct sigaction now = {};
    sigaction(SIGSEGV, nullptr, &now);
    CHECK(now.sa_handler == previous_segv.sa_handler);
#endif

    // Installed again afterwards, and reporting again.
    moteur::install_crash_reporter(folder.string(), "review");
    CHECK_FALSE(moteur::crash_reports_directory().empty());
    moteur::report_fatal_error("after reinstalling", "");
    moteur::uninstall_crash_reporter();
    bool reported = false;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        reported = reported || (entry.path().extension() == ".txt" &&
                                read_all(entry.path()).find("event: after reinstalling") != std::string::npos);
    }
    CHECK(reported);
    // Uninstalled: a fatal error only logs.
    moteur::report_fatal_error("not reported", "");
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        CHECK(read_all(entry.path()).find("not reported") == std::string::npos);
    }

    std::set_terminate(previous_terminate);
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}

TEST_CASE("review platform: an Application that fails to start uninstalls the crash reporter it installed") {
    const SilentCrashReports silent;
    const std::terminate_handler previous_terminate = std::set_terminate(test_terminate);
    moteur::ApplicationConfig config;
    config.organization = "moteur_tests";
    config.application = "review_platform";
    config.log_file = false;
    config.crash_reports = true;
    config.archives_directory.clear();
    config.tick_rate = 0;  // refused after the crash reporter is installed
    CHECK_THROWS_AS(moteur::Application{config}, std::invalid_argument);
    CHECK(moteur::crash_reports_directory().empty());
    CHECK(std::get_terminate() == test_terminate);

    // A reporter the game installed itself is left to it.
    const std::filesystem::path folder = fresh_folder("game_owned");
    moteur::install_crash_reporter(folder.string(), "review");
    CHECK_THROWS_AS(moteur::Application{config}, std::invalid_argument);
    CHECK(moteur::crash_reports_directory() == folder.string() + "/");
    moteur::uninstall_crash_reporter();

    std::set_terminate(previous_terminate);
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    // The preferences folder the Application made (.../moteur_tests/review_platform/crash_reports/).
    if (char* preferences = SDL_GetPrefPath("moteur_tests", "review_platform")) {
        const std::string text = preferences;
        SDL_free(preferences);
        const std::filesystem::path path(std::u8string(text.begin(), text.end()));
        std::filesystem::remove_all(path.parent_path().parent_path(), error);  // the trailing separator, then the application
    }
}

TEST_CASE("review platform: display.json values of the wrong type are refused with a warning") {
    std::vector<std::string> warnings;
    const moteur::DisplaySettings read = moteur::DisplaySettings::from_json(
        R"({"version": 1, "vsync": 1, "display": "1", "mode": 2, "frame_limit": "60", "render_scale": true,
            "anti_aliasing": 4, "shadows": null})",
        warnings);
    CHECK(read == moteur::DisplaySettings{});
    CHECK(warnings.size() == 7);
    for (const char* key : {"'vsync'", "'display'", "'mode'", "'frame_limit'", "'render_scale'", "'anti_aliasing'", "'shadows'"}) {
        CAPTURE(key);
        CHECK(std::any_of(warnings.begin(), warnings.end(), [key](const std::string& w) { return w.find(key) != std::string::npos; }));
    }
    // The French keys too, under their English names.
    warnings.clear();
    moteur::DisplaySettings::from_json(R"({"version": 1, "synchronisation": "oui"})", warnings);
    CHECK(warnings.size() == 1);
}

TEST_CASE("review platform: display.json of another version gives the defaults, with a warning") {
    moteur::DisplaySettings defaults;
    defaults.vsync = false;
    for (const char* version : {"2", "0", "\"1\"", "1.5"}) {
        CAPTURE(version);
        std::vector<std::string> warnings;
        const moteur::DisplaySettings read = moteur::DisplaySettings::from_json(
            std::string(R"({"version": )") + version + R"(, "mode": "fullscreen", "frame_limit": 30})", warnings, defaults);
        CHECK(read == defaults);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].find("version") != std::string::npos);
    }
    // Without a version: read as version 1.
    std::vector<std::string> warnings;
    CHECK(moteur::DisplaySettings::from_json(R"({"mode": "fullscreen"})", warnings).mode == moteur::WindowMode::Fullscreen);
    CHECK(warnings.empty());
}

TEST_CASE("review platform: a key missing from display.json keeps the given default (the game's vsync)") {
    moteur::DisplaySettings defaults;
    defaults.vsync = false;
    std::vector<std::string> warnings;
    const moteur::DisplaySettings read =
        moteur::DisplaySettings::from_json(R"({"version": 1, "mode": "fullscreen"})", warnings, defaults);
    CHECK(warnings.empty());
    CHECK_FALSE(read.vsync);
    CHECK(read.mode == moteur::WindowMode::Fullscreen);
    // Unreadable: the given defaults, not the struct's.
    CHECK(moteur::DisplaySettings::from_json("{", warnings, defaults) == defaults);
    // What the file says still wins.
    CHECK(moteur::DisplaySettings::from_json(R"({"version": 1, "vsync": true})", warnings, defaults).vsync);
}

TEST_CASE("review platform: a compressed image is uploaded by the engine's private side, not the public Renderer") {
    // The upload of a CompressedImage is a function of ktx_texture.hpp, built on the public
    // Renderer::create_texture_levels(); the public header no longer knows the type.
    static_assert(std::is_same_v<decltype(&moteur::create_compressed_texture),
                                 moteur::Texture (*)(moteur::Renderer&, const moteur::CompressedImage&, const char*)>);
    const std::string header = read_all(std::filesystem::path(MOTEUR_SOURCE_ASSETS) / "../src/moteur/include/moteur/renderer.hpp");
    REQUIRE_FALSE(header.empty());
    CHECK(header.find("CompressedImage") == std::string::npos);
    CHECK(header.find("friend") == std::string::npos);
}

namespace {

std::string base64(const std::vector<std::uint8_t>& bytes) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t chunk = (bytes[i] << 16) | (i + 1 < bytes.size() ? bytes[i + 1] << 8 : 0) |
                                    (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        out += alphabet[(chunk >> 18) & 63];
        out += alphabet[(chunk >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(chunk >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[chunk & 63] : '=';
    }
    return out;
}

// A 4 x 4 RGBA8 KTX2 file, one level.
std::vector<std::uint8_t> small_ktx2() {
    ktxTextureCreateInfo info = {};
    info.vkFormat = 37;  // VK_FORMAT_R8G8B8A8_UNORM
    info.baseWidth = 4;
    info.baseHeight = 4;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 1;
    info.numLayers = 1;
    info.numFaces = 1;
    info.isArray = KTX_FALSE;
    info.generateMipmaps = KTX_FALSE;
    ktxTexture2* texture = nullptr;
    REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS);
    const std::vector<std::uint8_t> pixels(4 * 4 * 4, 200);
    REQUIRE(ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, pixels.data(), pixels.size()) == KTX_SUCCESS);
    ktx_uint8_t* bytes = nullptr;
    ktx_size_t size = 0;
    REQUIRE(ktxTexture_WriteToMemory(ktxTexture(texture), &bytes, &size) == KTX_SUCCESS);
    std::vector<std::uint8_t> file(bytes, bytes + size);
    std::free(bytes);
    ktxTexture_Destroy(ktxTexture(texture));
    return file;
}

// A triangle whose material's base color is a KTX2-only texture: KHR_texture_basisu required.
std::string ktx2_only_gltf(const std::string& required) {
    const float positions[9] = {0, 0, 0, 0, 0, 1, 1, 0, 0};
    std::vector<std::uint8_t> bytes(sizeof(positions));
    std::memcpy(bytes.data(), positions, sizeof(positions));
    return R"({"asset": {"version": "2.0"},
        "extensionsUsed": ["KHR_texture_basisu"], "extensionsRequired": )" + required + R"(,
        "buffers": [{"byteLength": 36, "uri": "data:application/octet-stream;base64,)" + base64(bytes) + R"("}],
        "bufferViews": [{"buffer": 0, "byteLength": 36}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 0, 1]}],
        "images": [{"name": "color", "uri": "data:image/ktx2;base64,)" + base64(small_ktx2()) + R"("}],
        "textures": [{"extensions": {"KHR_texture_basisu": {"source": 0}}}],
        "materials": [{"name": "m", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "meshes": [{"name": "tri", "primitives": [{"attributes": {"POSITION": 0}, "material": 0}]}],
        "nodes": [{"name": "n", "mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0})";
}

}  // namespace

TEST_CASE("review platform: a glTF may require the extensions the reader supports") {
    const std::string json = ktx2_only_gltf(R"(["KHR_texture_basisu", "KHR_materials_emissive_strength"])");
    const moteur::ModelData model = moteur::parse_gltf(json.data(), json.size(), "ktx2_only.gltf");
    REQUIRE(model.materials.size() == 1);
    REQUIRE(model.materials[0].base_color_image == 0);
    REQUIRE(model.images.size() == 1);
    CHECK(moteur::is_ktx2(model.images[0].ktx2.data(), model.images[0].ktx2.size()));

    // The others are still refused, by name (and only them).
    const std::string draco = ktx2_only_gltf(R"(["KHR_texture_basisu", "KHR_draco_mesh_compression"])");
    try {
        moteur::parse_gltf(draco.data(), draco.size(), "draco.gltf");
        FAIL("a required extension the reader does not read was accepted");
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        CHECK(message.find("KHR_draco_mesh_compression") != std::string::npos);
        CHECK(message.find("KHR_texture_basisu") == std::string::npos);
    }
}
