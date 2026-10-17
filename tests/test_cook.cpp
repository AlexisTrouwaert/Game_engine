#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "moteur/cook.hpp"

TEST_CASE("cook: globs match within a folder (*) and across folders (**)") {
    using moteur::glob_match;
    CHECK(glob_match("maps/tranche.json", "maps/tranche.json"));
    CHECK_FALSE(glob_match("maps/tranche.json", "maps/arene.json"));
    CHECK(glob_match("effects/*.json", "effects/fire.json"));
    CHECK_FALSE(glob_match("effects/*.json", "effects/old/fire.json"));  // * stays in its folder
    CHECK(glob_match("models/characters/**", "models/characters/kaykit/Knight.glb"));
    CHECK(glob_match("models/**/*.glb", "models/a/b/c.glb"));
    CHECK(glob_match("models/**/*.glb", "models/c.glb"));  // ** may be no folder at all
    CHECK_FALSE(glob_match("models/**/*.glb", "models/a/c.gltf"));
    CHECK(glob_match("**/*.blend", "art/x/y.blend"));
    CHECK(glob_match("audio/**", "audio/kenney/step.ogg"));
    CHECK_FALSE(glob_match("audio/**", "audiox/step.ogg"));
}

namespace {

std::filesystem::path fresh_folder(const char* name) {
    char* prefs = SDL_GetPrefPath("moteur", "tests");
    std::filesystem::path folder = std::filesystem::path(prefs != nullptr ? prefs : "") / name;
    SDL_free(prefs);
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder / "assets" / "input", error);
    return folder;
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}

}  // namespace

TEST_CASE("cook: the manifest's English keys, and its French keys of before with a warning") {
    for (const bool french : {false, true}) {
        CAPTURE(french);
        const std::filesystem::path folder = fresh_folder(french ? "cook_ancien" : "cook_english");
        write(folder / "assets" / "input" / "keys.json", R"({"version": 1})");
        write(folder / "assets" / "input" / "left_behind.json", R"({"version": 1})");
        write(folder / "manifest.json",
              french ? R"({"version": 1, "inclure": ["input/*.json"], "exclure": ["input/left_*"],
                          "archives": [{"nom": "entrees", "inclure": ["input/**"]}]})"
                     : R"({"version": 1, "include": ["input/*.json"], "exclude": ["input/left_*"],
                          "archives": [{"name": "entrees", "include": ["input/**"]}]})");
        moteur::CookOptions options;
        options.sources = {(folder / "assets").string()};
        options.manifest = (folder / "manifest.json").string();
        options.output = (folder / "out").string();
        const moteur::CookReport report = moteur::cook(options);
        CHECK_MESSAGE(report.ok(), report.text());
        CHECK(report.shipped == 1);  // keys.json; left_behind.json excluded
        REQUIRE(report.archives.size() == 1);
        CHECK(report.archives[0].name == "entrees.pak");
        CHECK(std::filesystem::exists(folder / "out" / "archives" / "entrees.pak"));
        const bool deprecated = std::any_of(report.warnings.begin(), report.warnings.end(), [](const std::string& w) {
            return w.find("deprecated") != std::string::npos;
        });
        CHECK(deprecated == french);
    }
}
