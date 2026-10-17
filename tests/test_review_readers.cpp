// Regression tests of the defects found while documenting the API (milestone 8, part 9): readers.

#include <doctest/doctest.h>

#include <SDL3/SDL.h>
#include <ktx.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/audio.hpp"
#include "moteur/cook.hpp"
#include "moteur/map_data.hpp"
#include "moteur/particles.hpp"
#include "moteur/variables.hpp"

namespace {

// The SDL log messages written while it lives (the warnings of the readers).
class LogCapture {
public:
    LogCapture() {
        SDL_GetLogOutputFunction(&previous_, &previous_data_);
        SDL_SetLogOutputFunction(
            [](void* data, int, SDL_LogPriority, const char* message) {
                static_cast<LogCapture*>(data)->lines.emplace_back(message);
            },
            this);
    }
    ~LogCapture() { SDL_SetLogOutputFunction(previous_, previous_data_); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    bool has(const std::string& text) const {
        for (const std::string& line : lines) {
            if (line.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    std::vector<std::string> lines;

private:
    SDL_LogOutputFunction previous_ = nullptr;
    void* previous_data_ = nullptr;
};

std::string error_of(const std::function<void()>& parse) {
    try {
        parse();
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return {};
}

std::string effect(const std::string& emitter_keys) {
    return R"({"version": 1, "emitters": [{"texture": "particles/spark.png", "burst": 1)" + emitter_keys + "}]}";
}

std::filesystem::path fresh_folder(const char* name) {
    char* prefs = SDL_GetPrefPath("moteur", "tests");
    std::filesystem::path folder = std::filesystem::path(prefs != nullptr ? prefs : "") / name;
    SDL_free(prefs);
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder / "assets", error);
    return folder;
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
}

void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// An 8 x 8 RGBA8 KTX2 file, in Basis UASTC (transcoded by the cook) or uncompressed (copied).
std::vector<std::uint8_t> make_ktx2(bool uastc) {
    ktxTextureCreateInfo info = {};
    info.vkFormat = 37;  // VK_FORMAT_R8G8B8A8_UNORM
    info.baseWidth = 8;
    info.baseHeight = 8;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 1;
    info.numLayers = 1;
    info.numFaces = 1;
    info.isArray = KTX_FALSE;
    info.generateMipmaps = KTX_FALSE;
    ktxTexture2* texture = nullptr;
    REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS);
    std::vector<std::uint8_t> pixels(8 * 8 * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint8_t>(i % 4 == 3 ? 255 : 60 + i);
    }
    REQUIRE(ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, pixels.data(), pixels.size()) == KTX_SUCCESS);
    if (uastc) {
        ktxBasisParams params = {};
        params.structSize = sizeof(params);
        params.uastc = KTX_TRUE;
        params.threadCount = 1;
        params.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT;
        REQUIRE(ktxTexture2_CompressBasisEx(texture, &params) == KTX_SUCCESS);
    }
    ktx_uint8_t* bytes = nullptr;
    ktx_size_t size = 0;
    REQUIRE(ktxTexture_WriteToMemory(ktxTexture(texture), &bytes, &size) == KTX_SUCCESS);
    std::vector<std::uint8_t> file(bytes, bytes + size);
    std::free(bytes);
    ktxTexture_Destroy(ktxTexture(texture));
    return file;
}

bool has_warning(const moteur::CookReport& report, const std::string& text) {
    for (const std::string& warning : report.warnings) {
        if (warning.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

// 1. A curve may be a single value, as the header says: a constant.
TEST_CASE("review readers: every particle curve takes a single value, a constant") {
    const moteur::ParticleEffect single =
        moteur::ParticleEffect::parse(effect(R"(, "size": 0.2, "color": [1, 0.5, 0.25, 1])"), "single.json");
    const moteur::EmitterDesc& d = single.emitters()[0];
    REQUIRE(d.size.times.size() == 1);
    CHECK(d.size.at(0.0f) == 0.2f);
    CHECK(d.size.at(0.7f) == 0.2f);
    CHECK(d.color.at(0.5f) == glm::vec4(1.0f, 0.5f, 0.25f, 1.0f));

    // The same as one key: what the files written with keys give is unchanged.
    const moteur::ParticleEffect keyed =
        moteur::ParticleEffect::parse(effect(R"(, "size": [[0, 0.2]], "color": [[0, [1, 0.5, 0.25, 1]]])"), "keyed.json");
    CHECK(keyed.emitters()[0].size.times == d.size.times);
    CHECK(keyed.emitters()[0].size.values == d.size.values);
    CHECK(keyed.emitters()[0].color.values == d.color.values);
    const moteur::ParticleEffect curve =
        moteur::ParticleEffect::parse(effect(R"(, "size": [[0, 0.1], [1, 0.3]])"), "curve.json");
    CHECK(curve.emitters()[0].size.at(0.5f) == doctest::Approx(0.2f));

    // Not a value, nor keys: refused, naming the curve and the file.
    for (const char* bad : {R"(, "size": [0.2])", R"(, "size": "big")", R"(, "size": [])", R"(, "color": 1)",
                            R"(, "color": [1, 0.5])", R"(, "size": [[0, [1, 1, 1, 1]]])"}) {
        CAPTURE(bad);
        const std::string message = error_of([&] { (void)moteur::ParticleEffect::parse(effect(bad), "bad.json"); });
        CHECK(message.find("Effect 'bad.json'") == 0);
    }
    CHECK(error_of([] { (void)moteur::ParticleEffect::parse(effect(R"(, "size": [0.2])"), "bad.json"); })
              .find("\"size\" needs a single value or keys") != std::string::npos);
    CHECK(error_of([] { (void)moteur::ParticleEffect::parse(effect(R"(, "color": [[0, 1]])"), "bad.json"); })
              .find("a key's value must be a color") != std::string::npos);
}

// 2. What version 2 added is refused in a version 1 file; a legend's tiles are always a list.
TEST_CASE("review readers: a version 1 map has no version 2 keys, and a legend's tiles are a list") {
    const std::string base = R"("tiles": {"floor": {}}, "legend": {".": ["floor"]}, "rows": ["..", ".."])";
    const auto map = [&](int version, const std::string& more) {
        return "{\"version\": " + std::to_string(version) + ", " + base + more + "}";
    };
    const std::string object = R"(, "objects": [{"id": "o1", "type": "tree", "x": 0.5, "z": 0.5}])";
    const std::string connector = R"(, "connectors": [{"name": "door", "x": 0, "z": 0}])";
    CHECK(moteur::MapData::parse(map(2, object + connector + R"(, "chunk": true)"), "v2.json").objects().size() == 1);
    CHECK(moteur::MapData::parse(map(1, ""), "v1.json").objects().empty());
    CHECK(error_of([&] { (void)moteur::MapData::parse(map(1, object), "v1.json"); }) ==
          "Map 'v1.json': \"objects\" needs \"version\": 2");
    CHECK(error_of([&] { (void)moteur::MapData::parse(map(1, connector), "v1.json"); }) ==
          "Map 'v1.json': \"connectors\" needs \"version\": 2");
    CHECK(error_of([&] { (void)moteur::MapData::parse(map(1, R"(, "chunk": true)"), "v1.json"); }) ==
          "Map 'v1.json': \"chunk\" needs \"version\": 2");

    for (const char* legend : {R"({".": "floor"})", R"({".": {"tiles": "floor"}})", R"({".": null})"}) {
        CAPTURE(legend);
        const std::string text =
            std::string(R"({"version": 1, "tiles": {"floor": {}}, "legend": )") + legend + R"(, "rows": [".."]})";
        CHECK(error_of([&] { (void)moteur::MapData::parse(text, "m.json"); }) ==
              "Map 'm.json': legend '.': the tiles must be a list of names");
    }

    // Every map of the game reads as before.
    for (const auto& entry : std::filesystem::directory_iterator(std::string(MOTEUR_SOURCE_ASSETS) + "maps")) {
        if (entry.path().extension() == ".json") {
            CAPTURE(entry.path().string());
            CHECK_NOTHROW((void)moteur::MapData::parse(read(entry.path()), entry.path().filename().string()));
        }
    }
}

// 3. The preferences file: archived variables only, bad values in the log, any text read back.
TEST_CASE("review readers: the preferences file sets archived variables only, logs bad values, keeps any text") {
    moteur::Variables vars;
    moteur::Variable& volume = vars.add_int("audio.volume", 5, 0, 10, "", moteur::Variable::Archive);
    moteur::Variable& tool = vars.add_int("tool.size", 2, 1, 9, "");  // not archived
    {
        LogCapture log;
        vars.set_pending("set audio.volume \"42\"\nset tool.size \"5\"\nset later.tool \"7\"\nset later.bad \"x\"\n");
        CHECK(volume.as_int() == 5);  // out of bounds: refused...
        CHECK(log.has("audio.volume"));  // ...and said
        CHECK(tool.as_int() == 2);  // the file holds archived variables only
        CHECK(log.has("'tool.size' is not kept in the preferences"));
        const std::size_t before = log.lines.size();
        moteur::Variable& later_tool = vars.add_int("later.tool", 1, 0, 9, "");  // declared later, not archived
        CHECK(later_tool.as_int() == 1);
        moteur::Variable& later_bad = vars.add_int("later.bad", 3, 0, 9, "", moteur::Variable::Archive);
        CHECK(later_bad.as_int() == 3);
        CHECK(log.lines.size() == before + 2);  // both said
    }

    // A text value with quotes, backslashes and line ends survives the file.
    moteur::Variable& name = vars.add_text("player.name", "", "", moteur::Variable::Archive);
    const std::string tricky = "say \"hi\"\nC:\\path\\ \\n end\r";
    std::string error;
    REQUIRE(name.set_text(tricky, error));
    moteur::Variable& pending_name = vars.add_text("player.title", "", "", moteur::Variable::Archive);
    REQUIRE(pending_name.set_text("a \"b\"", error));
    const std::string text = vars.archived_text();
    CHECK(std::count(text.begin(), text.end(), '\n') == 2);  // one line each
    moteur::Variables again;
    again.set_pending(text);
    CHECK(again.add_text("player.name", "", "", moteur::Variable::Archive).as_text() == tricky);
    // Kept pending (not declared in this run), then written again: the same line.
    CHECK(again.archived_text().find(text.substr(text.find("set player.title"))) != std::string::npos);
    std::vector<std::string> problems;
    moteur::Variables third;
    moteur::Variable& name3 = third.add_text("player.name", "", "", moteur::Variable::Archive);
    third.apply_archived(text, problems);
    CHECK(name3.as_text() == tricky);
    // A value written before the escapes (a Windows path) reads as it was.
    third.apply_archived("set player.name \"C:\\Users\\me\"\n", problems);
    CHECK(name3.as_text() == "C:\\Users\\me");
    third.apply_archived("set player.name \"open\n", problems);
    CHECK(name3.as_text() == "C:\\Users\\me");
    CHECK(problems.size() == 2);  // player.title unknown, the badly quoted line
}

// 4. Audio settings: all or nothing.
TEST_CASE("review readers: audio settings with a bad value change nothing") {
    moteur::AudioConfig config;
    config.device = false;
    config.sample_rate = 48000;
    config.channels = 2;
    moteur::Audio audio(config);
    audio.set_master_volume(0.8f);
    audio.set_group_volume(moteur::SoundGroup::Music, 0.6f);
    audio.mark_settings_saved();
    const std::string before = audio.settings_json();
    CHECK_THROWS_AS(audio.apply_settings_json(R"({"version": 1, "master": 0.1, "volumes": {"music": "loud"}})", "s.json"),
                    std::runtime_error);
    CHECK_THROWS_AS(audio.apply_settings_json(R"({"version": 1, "master": 0.1, "mute_in_background": 3})", "s.json"),
                    std::runtime_error);
    CHECK(audio.master_volume() == 0.8f);
    CHECK(audio.group_volume(moteur::SoundGroup::Music) == 0.6f);
    CHECK(audio.settings_json() == before);
    audio.apply_settings_json(R"({"version": 1, "master": 0.1, "volumes": {"music": 0.2}})", "s.json");
    CHECK(audio.master_volume() == 0.1f);
    CHECK(audio.group_volume(moteur::SoundGroup::Music) == 0.2f);
}

// 5. The cook: a root asset can be credited; an up-to-date texture copied is not "transcoded".
TEST_CASE("review readers: the cook credits a root asset and counts only the textures it transcoded") {
    {
        const std::filesystem::path folder = fresh_folder("review_cook_credits");
        write(folder / "assets" / "font.ttf", std::string("not a real font"));
        write(folder / "assets" / "environments" / "sky.hdr", std::string("not a real sky"));
        write(folder / "assets" / "credits.json",
              std::string(R"({"fonts": [{"name": "Font", "file": "assets/font.ttf", "license_file": "assets/OFL.txt"}]})"));
        write(folder / "manifest.json",
              std::string(R"({"version": 1, "include": ["font.ttf", "environments/*", "credits.json"]})"));
        moteur::CookOptions options;
        options.sources = {(folder / "assets").string()};
        options.manifest = (folder / "manifest.json").string();
        options.output = (folder / "out").string();
        const moteur::CookReport report = moteur::cook(options);
        CHECK_MESSAGE(report.ok(), report.text());
        CHECK_FALSE(has_warning(report, "'font.ttf'"));  // its own entry covers it
        CHECK(has_warning(report, "'environments/sky.hdr': no credits entry"));  // a root entry covers nothing else
    }
    {
        const std::filesystem::path folder = fresh_folder("review_cook_transcoded");
        write(folder / "assets" / "textures" / "basis.ktx2", make_ktx2(true));
        write(folder / "assets" / "textures" / "raw.ktx2", make_ktx2(false));
        write(folder / "manifest.json", std::string(R"({"version": 1, "include": ["textures/*"]})"));
        moteur::CookOptions options;
        options.sources = {(folder / "assets").string()};
        options.manifest = (folder / "manifest.json").string();
        options.output = (folder / "out").string();
        const moteur::CookReport first = moteur::cook(options);
        CHECK_MESSAGE(first.ok(), first.text());
        CHECK(first.prepared == 2);
        CHECK(first.transcoded == 1);
        const moteur::CookReport second = moteur::cook(options);  // up to date
        CHECK(second.prepared == 0);
        CHECK(second.transcoded == 1);  // was 2: the copied one counted too

        // A state written before the list of transcoded textures: they are prepared again.
        nlohmann::json state = nlohmann::json::parse(read(folder / "out" / "cook_state.json"));
        state.erase("transcoded");
        write(folder / "out" / "cook_state.json", state.dump());
        const moteur::CookReport third = moteur::cook(options);
        CHECK_MESSAGE(third.ok(), third.text());
        CHECK(third.prepared == 2);
        CHECK(third.transcoded == 1);
    }
}
