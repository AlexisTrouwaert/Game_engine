// Regression tests of the defects found while documenting the API (milestone 8, part 9): core.

#include <doctest/doctest.h>

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <ktx.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/archive.hpp"
#include "moteur/console.hpp"
#include "moteur/cook.hpp"
#include "moteur/credits.hpp"
#include "moteur/data_reader.hpp"
#include "moteur/file_io.hpp"
#include "moteur/fixed_timestep.hpp"
#include "moteur/log_file.hpp"
#include "moteur/map_data.hpp"
#include "moteur/paths.hpp"
#include "moteur/profiler.hpp"
#include "moteur/save_file.hpp"
#include "moteur/variables.hpp"
#include "moteur/world.hpp"
#include "moteur/world_save.hpp"

namespace {

std::string temporary(const std::string& name) {
    const char* folder = SDL_GetPrefPath("moteur", "tests");
    std::string path = std::string(folder != nullptr ? folder : "") + name;
    SDL_free(const_cast<char*>(folder));
    return path;
}

std::vector<std::uint8_t> bytes_of(const std::string& text) {
    return {text.begin(), text.end()};
}

// The archive's index hash (FNV-1a 64), to forge indexes that pass it.
std::uint64_t fnv1a(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

// Writes an archive of two raw entries "a.txt" and "b.txt", lets `edit` change its index (it starts
// at byte 24), rewrites the index hash and returns the path.
template <typename Edit>
std::string forged_archive(const std::string& name, Edit edit) {
    const std::string path = temporary(name);
    std::string error;
    std::vector<moteur::ArchiveInput> inputs = {{"a.txt", bytes_of("aaa"), false}, {"b.txt", bytes_of("bbb"), false}};
    REQUIRE_MESSAGE(moteur::write_archive(path, inputs, error), error);
    auto file = moteur::read_file_bytes(path);
    REQUIRE(file.has_value());
    std::vector<std::uint8_t>& bytes = *file;
    std::uint32_t index_size = 0;
    std::memcpy(&index_size, bytes.data() + 12, 4);
    edit(bytes.data() + 24);
    const std::uint64_t hash = fnv1a(bytes.data() + 24, index_size);
    std::memcpy(bytes.data() + 16, &hash, 8);
    REQUIRE_MESSAGE(moteur::write_file_atomic(path, bytes.data(), bytes.size(), error), error);
    return path;
}

}  // namespace

TEST_CASE("review core: reset and toggle of a Logic variable are game commands, like set") {
    moteur::Console console;
    moteur::Variable& flag = console.variables().add_bool("logic.flag", false, "test", moteur::Variable::Logic);
    moteur::Variable& speed = console.variables().add_float("logic.speed", 1.0f, 0.0f, 4.0f, "test", moteur::Variable::Logic);
    moteur::Variable& tool = console.variables().add_bool("tool.flag", false, "test");
    speed.set_float(2.0f);

    console.submit("toggle logic.flag");
    console.submit("reset logic.speed");
    CHECK_FALSE(flag.as_bool());  // not yet: on the next tick
    CHECK(speed.as_float() == 2.0f);
    const std::vector<std::string> queued = console.take_game_commands();
    REQUIRE(queued.size() == 2);
    for (const std::string& line : queued) {
        console.run_game_command(line);
    }
    CHECK(flag.as_bool());
    CHECK(speed.as_float() == 1.0f);

    console.submit("toggle tool.flag");  // not Logic: at once
    CHECK(tool.as_bool());
    CHECK(console.take_game_commands().empty());

    const moteur::Console::Line line;
    CHECK(line.level == moteur::Console::Level::Info);
}

TEST_CASE("review core: profiler totals merge the zones of one name text at different addresses") {
    static const char first[] = "zone";
    static const char second[] = "zone";  // another array: another address, the same text
    REQUIRE(static_cast<const void*>(first) != static_cast<const void*>(second));
    moteur::Profiler profiler;
    for (int frame = 0; frame < 2; ++frame) {
        profiler.begin_frame();
        profiler.begin_zone(first);
        profiler.end_zone();
        profiler.begin_zone(second);
        profiler.end_zone();
        profiler.end_frame();
    }
    const std::vector<moteur::Profiler::Summary> totals = profiler.totals();
    REQUIRE(totals.size() == 1);
    CHECK(totals[0].name == "zone");
    CHECK(totals[0].calls == 2.0);  // both, every frame
}

TEST_CASE("review core: a variable declared with its minimum above its maximum is refused") {
    moteur::Variables variables;
    try {
        variables.add_int("bad.int", 0, 5, 1, "test");
        FAIL("no exception");
    } catch (const std::invalid_argument& e) {
        CHECK(std::string(e.what()).find("bad.int") != std::string::npos);
    }
    CHECK_THROWS_AS(variables.add_float("bad.float", 0.0f, 2.0f, 1.0f, "test"), std::invalid_argument);
    CHECK(variables.find("bad.int") == nullptr);
    CHECK(variables.find("bad.float") == nullptr);
    CHECK(variables.add_int("good", 3, 1, 1, "test").as_int() == 1);  // min == max is fine
}

TEST_CASE("review core: DataIssues::clear() forgets the errors too") {
    moteur::DataIssues issues;
    issues.error("a", "wrong");
    issues.warning("b", "odd");
    issues.clear();
    CHECK_FALSE(issues.has_errors());
    CHECK(issues.errors() == 0);
    CHECK(issues.warnings() == 0);
    issues.warning("c", "odd");
    CHECK(issues.warnings() == 1);
}

TEST_CASE("review core: an out-of-range number shows its float bounds as written") {
    const nlohmann::json row = {{"x", 5.0}, {"y", 5}};
    moteur::DataIssues issues;
    moteur::DataReader reader(row, "f > r", issues);
    (void)reader.number("x", 0.1f, 2.5f);
    (void)reader.number("y", 1.0f, 2.0f);
    REQUIRE(issues.all().size() == 2);
    CHECK(issues.all()[0].message == "5.0 out of range [0.1, 2.5]");
    CHECK(issues.all()[1].message == "5.0 out of range [1.0, 2.0]");
}

TEST_CASE("review core: a section below version 1 with no migration is refused, not a crash") {
    const moteur::SaveMigrations migrations;
    int version = 0;
    nlohmann::json data = nlohmann::json::object();
    std::string error;
    CHECK_FALSE(migrations.upgrade("world", version, data, error));
    CHECK(error.find("world") != std::string::npos);
    version = -3;
    CHECK_FALSE(migrations.upgrade("world", version, data, error));
    version = 1;
    CHECK(migrations.upgrade("world", version, data, error));
}

TEST_CASE("review core: a world record without id or components is a problem, not an exception") {
    moteur::ComponentSerializers serializers;
    serializers.add_engine_components();
    const nlohmann::json records = nlohmann::json::parse(
        R"([{"c": {}}, {"id": 2}, 7, {"id": -1, "c": {}}, {"id": 3, "c": 4}, {"id": 5, "c": {}}])");
    entt::registry registry;
    std::vector<std::string> problems;
    int spawned = 0;
    CHECK_NOTHROW(serializers.load(
        registry, records,
        [&spawned](entt::registry& r, const nlohmann::json&) {
            ++spawned;
            return r.create();
        },
        problems));
    CHECK(problems.size() == 5);
    CHECK(spawned == 1);  // only the whole record
    problems.clear();
    serializers.load(registry, nlohmann::json::object(), [](entt::registry& r, const nlohmann::json&) { return r.create(); },
                     problems);
    CHECK(problems.size() == 1);
}

TEST_CASE("review core: a credits file is applied whole or not at all") {
    const std::string path = temporary("review_credits.json");
    std::string error;
    REQUIRE(moteur::write_file_atomic(path, std::string(R"({"libraries": [{"name": "Good"}], "fonts": [{"name": 5}]})"),
                                      error));
    moteur::Credits credits;
    credits.add_file(path);
    CHECK_FALSE(credits.error.empty());
    CHECK(credits.libraries.empty());
    CHECK(credits.fonts.empty());

    REQUIRE(moteur::write_file_atomic(path, std::string(R"({"libraries": [{"name": "Good"}], "fonts": [{"name": "Font"}]})"),
                                      error));
    moteur::Credits fine;
    fine.add_file(path);
    CHECK(fine.error.empty());
    REQUIRE(fine.libraries.size() == 1);
    CHECK(fine.libraries[0].name == "Good");
    CHECK(fine.fonts.size() == 1);
}

TEST_CASE("review core: an archive index out of order, with a path twice or an unknown compression is refused") {
    std::string error;
    // The first entry's path at bytes 2..6 of the index, its compression at 2 + 5 + 24.
    const std::string unsorted = forged_archive("review_unsorted.pak", [](std::uint8_t* index) { index[2] = 'c'; });
    CHECK(moteur::Archive::open(unsorted, error) == nullptr);
    CHECK(error.find("sorted") != std::string::npos);
    const std::string twice = forged_archive("review_twice.pak", [](std::uint8_t* index) { index[2] = 'b'; });
    CHECK(moteur::Archive::open(twice, error) == nullptr);
    const std::string compression = forged_archive("review_compression.pak", [](std::uint8_t* index) { index[2 + 5 + 24] = 2; });
    CHECK(moteur::Archive::open(compression, error) == nullptr);
    CHECK(error.find("compression") != std::string::npos);
    const std::string untouched = forged_archive("review_untouched.pak", [](std::uint8_t*) {});
    const auto archive = moteur::Archive::open(untouched, error);
    REQUIRE(archive != nullptr);
    CHECK(archive->read(*archive->find("b.txt")) == bytes_of("bbb"));
}

TEST_CASE("review core: a ** inside a glob still needs the rest of the glob to match") {
    using moteur::glob_match;
    CHECK(glob_match("models/**.gltf", "models/x.gltf"));
    CHECK(glob_match("models/**.gltf", "models/a/b/x.gltf"));
    CHECK_FALSE(glob_match("models/**.gltf", "models/x.png"));
    CHECK_FALSE(glob_match("models/**.gltf", "other/x.gltf"));
    CHECK(glob_match("a/**/b/**", "a/x/b/y/z"));
    CHECK_FALSE(glob_match("a/**/b/**", "a/x/c/y/z"));
    CHECK(glob_match("audio/**", "audio/kenney/step.ogg"));
    CHECK(glob_match("**", "anything/at/all"));
}

TEST_CASE("review core: a log line reaches the file once formatted") {
    const std::string folder = temporary("review_log/");
    std::string error;
    REQUIRE(moteur::write_file_atomic(folder + "keep", std::string("x"), error));
    moteur::LogFile log;
    REQUIRE(log.open(folder));
    SDL_Log("review line");
    log.close();
    const auto text = moteur::read_file_bytes(folder + "log.txt");
    REQUIRE(text.has_value());
    const std::string content(text->begin(), text->end());
    CHECK(content.find("[info] review line\n") != std::string::npos);
}

TEST_CASE("review core: an Application that cannot be made undoes what it set up, SDL included") {
    const int tick_rate = moteur::tick_rate();
    moteur::Profiler* const profiler = moteur::Profiler::current();
    moteur::ApplicationConfig config;
    config.log_file = false;
    config.crash_reports = false;
    config.archives_directory.clear();
    config.tick_rate = 0;  // refused once SDL is initialized
    CHECK_THROWS_AS(moteur::Application{config}, std::invalid_argument);
    CHECK(SDL_WasInit(0) == 0);
    CHECK(moteur::tick_rate() == tick_rate);
    CHECK(moteur::Profiler::current() == profiler);
}

// Group "last" of the review's defects.

TEST_CASE("review last: write_archive refuses paths that differ only by case, even apart once sorted") {
    // Byte order: 'Audio/' < 'Maps/' < 'audio/': the two colliding paths are not neighbours.
    std::vector<moteur::ArchiveInput> inputs = {
        {"Audio/a.ogg", bytes_of("1"), false}, {"Maps/b.json", bytes_of("2"), false}, {"audio/a.ogg", bytes_of("3"), false}};
    std::string error;
    CHECK_FALSE(moteur::write_archive(temporary("review_last_case.pak"), inputs, error));
    CHECK(error.find("'Audio/a.ogg' and 'audio/a.ogg' collide") != std::string::npos);
}

TEST_CASE("review last: a file the cook fails to prepare is prepared again, and its error reported again") {
    char* prefs = SDL_GetPrefPath("moteur", "tests");
    const std::filesystem::path folder = std::filesystem::path(prefs != nullptr ? prefs : "") / "review_last_cook";
    SDL_free(prefs);
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    const std::filesystem::path texture = folder / "assets" / "textures" / "t.ktx2";
    std::filesystem::create_directories(texture.parent_path());
    std::string error;

    // An uncompressed 4 x 4 KTX2: copied as it is.
    ktxTextureCreateInfo info = {};
    info.vkFormat = 37;  // VK_FORMAT_R8G8B8A8_UNORM
    info.baseWidth = 4;
    info.baseHeight = 4;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 1;
    info.numLayers = 1;
    info.numFaces = 1;
    ktxTexture2* made = nullptr;
    REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &made) == KTX_SUCCESS);
    ktx_uint8_t* bytes = nullptr;
    ktx_size_t size = 0;
    REQUIRE(ktxTexture_WriteToMemory(ktxTexture(made), &bytes, &size) == KTX_SUCCESS);
    REQUIRE(moteur::write_file_atomic(texture.string(), bytes, size, error));
    std::free(bytes);
    ktxTexture_Destroy(ktxTexture(made));
    REQUIRE(moteur::write_file_atomic((folder / "manifest.json").string(),
                                      std::string(R"({"version": 1, "include": ["textures/*"]})"), error));

    moteur::CookOptions options;
    options.sources = {(folder / "assets").string()};
    options.manifest = (folder / "manifest.json").string();
    options.output = (folder / "out").string();
    const moteur::CookReport first = moteur::cook(options);
    REQUIRE_MESSAGE(first.ok(), first.text());
    CHECK(first.prepared == 1);

    // The source damaged: it cannot be prepared, and the output of the first cook stays in place.
    REQUIRE(moteur::write_file_atomic(texture.string(), std::string("not a KTX2 file"), error));
    const moteur::CookReport second = moteur::cook(options);
    CHECK_FALSE(second.ok());
    const moteur::CookReport third = moteur::cook(options);  // was ok: the stale output kept as up to date
    CHECK_FALSE(third.ok());
    CHECK(third.prepared == 0);
}

TEST_CASE("review last: a save's record with the id 0, or an id given twice, is refused and reported") {
    moteur::ComponentSerializers serializers;
    serializers.add_engine_components();
    const nlohmann::json records = nlohmann::json::parse(
        R"([{"id": 0, "c": {}}, {"id": 3, "c": {"hidden": true}}, {"id": 3, "c": {}}, {"id": 4, "c": {}}])");
    entt::registry registry;
    std::vector<std::string> problems;
    int spawned = 0;
    serializers.load(
        registry, records,
        [&spawned](entt::registry& r, const nlohmann::json&) {
            ++spawned;
            return r.create();
        },
        problems);
    CHECK(spawned == 2);
    REQUIRE(problems.size() == 2);
    CHECK(problems[0].find("record 0: \"id\" 0") != std::string::npos);
    CHECK(problems[1].find("record 2: \"id\" 3 already given") != std::string::npos);
    int threes = 0;
    for (const auto& [entity, id] : registry.view<moteur::PersistentId>().each()) {
        if (id.value == 3) {
            ++threes;
            CHECK(registry.all_of<moteur::Hidden>(entity));  // the first record of the id
        }
    }
    CHECK(threes == 1);
}

TEST_CASE("review last: a map's version must be an integer") {
    for (const char* version : {"1.5", "2.0", "\"1\""}) {
        const std::string text = std::string(R"({"version": )") + version + "}";
        try {
            (void)moteur::MapData::parse(text, "maps/m.json");
            FAIL("version " << version << " accepted");
        } catch (const std::runtime_error& e) {
            CHECK(std::string(e.what()).find("Map 'maps/m.json': expected an object with \"version\"") == 0);
        }
    }
}

TEST_CASE("review last: an archived float reads back to the same float") {
    moteur::Variables vars;
    moteur::Variable& precise = vars.add_float("last.precise", 1.0f, 0.0f, 10.0f, "test", moteur::Variable::Archive);
    moteur::Variable& low = vars.add_float("last.low", 1.0f, 0.1f, 10.0f, "test", moteur::Variable::Archive);
    moteur::Variable& count = vars.add_int("last.count", 1, 0, 10000000, "test", moteur::Variable::Archive);
    precise.set_float(1.2345678f);  // more than 6 significant digits
    low.set_float(0.1f);            // its minimum, which no decimal text gives exactly
    count.set_int(1234567);
    const std::string text = vars.archived_text();
    CHECK(text.find("set last.precise \"1.2345678\"") != std::string::npos);
    CHECK(text.find("set last.count \"1234567\"") != std::string::npos);

    moteur::Variables again;
    moteur::Variable& precise2 = again.add_float("last.precise", 1.0f, 0.0f, 10.0f, "test", moteur::Variable::Archive);
    moteur::Variable& low2 = again.add_float("last.low", 1.0f, 0.1f, 10.0f, "test", moteur::Variable::Archive);
    moteur::Variable& count2 = again.add_int("last.count", 1, 0, 10000000, "test", moteur::Variable::Archive);
    std::vector<std::string> problems;
    again.apply_archived(text, problems);
    CHECK(problems.empty());
    CHECK(precise2.as_float() == 1.2345678f);
    CHECK(low2.as_float() == 0.1f);
    CHECK(count2.as_int() == 1234567);
    CHECK(again.archived_text() == text);

    // The default's text reads back to the default.
    std::string error;
    REQUIRE(low2.set_text(low2.default_text(), error));
    CHECK(low2.is_default());
    moteur::Variable& tenth = again.add_float("last.tenth", 0.1f, 0.0f, 1.0f, "test");
    tenth.set_float(0.5f);
    REQUIRE(tenth.set_text("0.1", error));
    CHECK(tenth.is_default());
}
