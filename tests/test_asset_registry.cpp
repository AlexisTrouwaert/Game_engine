#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "moteur/asset_registry.hpp"
#include "moteur/paths.hpp"

// Milestone 9, part 4: stable asset identifiers, in .meta files next to the assets.

namespace {

struct TempFolders {
    std::filesystem::path root;
    TempFolders() {
        root = std::filesystem::temp_directory_path() / "moteur_registry_test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~TempFolders() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void write(const std::string& path, const std::string& text) const {
        const std::filesystem::path file = root / path;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << text;
    }
    std::string read(const std::string& path) const {
        std::ifstream file(root / path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }
    bool exists(const std::string& path) const { return std::filesystem::exists(root / path); }
    std::string folder(const std::string& name) const { return (root / name).generic_string() + "/"; }
};

}  // namespace

TEST_CASE("asset identifiers: random version 4 UUIDs, written and read back") {
    std::set<moteur::AssetId> seen;
    for (int i = 0; i < 1000; ++i) {
        const moteur::AssetId id = moteur::AssetId::generate();
        CHECK(id.valid());
        CHECK(seen.insert(id).second);
        const std::string text = id.text();
        REQUIRE(text.size() == 36);
        CHECK(text[14] == '4');                                         // version 4
        CHECK(std::string("89ab").find(text[19]) != std::string::npos);  // variant 10xx
        CHECK(moteur::AssetId::parse(text) == id);
    }
    const auto upper = moteur::AssetId::parse("3F2A9C1E-7B4D-4E0A-9C55-0D1E2F3A4B5C");
    REQUIRE(upper);
    CHECK(upper->text() == "3f2a9c1e-7b4d-4e0a-9c55-0d1e2f3a4b5c");
    CHECK_FALSE(moteur::AssetId::parse("3f2a9c1e7b4d4e0a9c550d1e2f3a4b5c"));
    CHECK_FALSE(moteur::AssetId::parse("3f2a9c1e-7b4d-4e0a-9c55-0d1e2f3a4b5g"));
    CHECK_FALSE(moteur::AssetId{}.valid());
}

TEST_CASE("asset identifiers: the .meta file") {
    moteur::AssetMeta meta;
    meta.id = *moteur::AssetId::parse("3f2a9c1e-7b4d-4e0a-9c55-0d1e2f3a4b5c");
    meta.import = {{"lods", 3}};
    const std::string text = moteur::asset_meta_text(meta);
    CHECK(text == "{\n  \"id\": \"3f2a9c1e-7b4d-4e0a-9c55-0d1e2f3a4b5c\",\n  \"import\": {\n    \"lods\": 3\n  },\n  \"version\": 1\n}\n");
    const moteur::AssetMeta back = moteur::parse_asset_meta(text, "a.meta");
    CHECK(back.id == meta.id);
    CHECK(back.import == meta.import);
    CHECK_THROWS_WITH_AS(moteur::parse_asset_meta(R"({"version": 1, "id": "x"})", "b.meta"), doctest::Contains("not a UUID"),
                         std::runtime_error);
    CHECK_THROWS_AS(moteur::parse_asset_meta(R"({"version": 2, "id": "3f2a9c1e-7b4d-4e0a-9c55-0d1e2f3a4b5c"})", "c.meta"),
                    std::runtime_error);

    CHECK(moteur::is_identified_asset("models/a.glb"));
    CHECK(moteur::is_identified_asset("prefabs/lampadaire.prefab.json"));
    CHECK_FALSE(moteur::is_identified_asset("models/a.bin"));
    CHECK_FALSE(moteur::is_identified_asset("models/a.glb.meta"));
    CHECK_FALSE(moteur::is_identified_asset("data/monstres/a.json"));
    CHECK_FALSE(moteur::is_identified_asset(".imported/x.mmdl"));
    CHECK_FALSE(moteur::is_identified_asset("models/.git/x"));
}

TEST_CASE("asset registry: a scan gives every asset an identity, keeps it, repairs copies") {
    TempFolders folders;
    folders.write("game/models/a.glb", "a");
    folders.write("game/models/a.bin", "buffer");
    folders.write("game/textures/b.png", "b");
    folders.write("game/data/monstres/x.json", "{}");
    folders.write("game/lost.png.meta", R"({"version": 1, "id": "11111111-1111-4111-8111-111111111111"})");
    folders.write("other/props/c.glb", "c");
    folders.write("other/models/a.glb", "the same key in a second folder");

    moteur::AssetRegistry registry;
    moteur::AssetScanReport report = registry.scan({folders.folder("game"), folders.folder("other")}, true);
    CHECK(report.assets == 3);
    CHECK(report.created.size() == 3);
    CHECK(report.orphans == std::vector<std::string>{"lost.png.meta"});
    CHECK(report.problems.empty());
    CHECK(report.shadowed == std::vector<std::string>{"models/a.glb"});
    CHECK(folders.exists("game/models/a.glb.meta"));
    CHECK(folders.exists("other/props/c.glb.meta"));
    CHECK_FALSE(folders.exists("game/models/a.bin.meta"));
    const moteur::AssetRecord* a = registry.find("models/a.glb");
    REQUIRE(a != nullptr);
    CHECK(a->root == folders.folder("game"));
    const moteur::AssetId a_id = a->id;
    CHECK(registry.find(a_id)->key == "models/a.glb");

    // A second scan keeps every identifier.
    report = registry.scan({folders.folder("game"), folders.folder("other")}, true);
    CHECK(report.created.empty());
    CHECK(registry.find("models/a.glb")->id == a_id);

    // A copy made with its .meta (Explorer's copy-paste): the copy, the path that sorts last, gets a
    // new identifier; the original keeps its own.
    folders.write("game/models/z_copy.glb", "a");
    folders.write("game/models/z_copy.glb.meta", folders.read("game/models/a.glb.meta"));
    report = registry.scan({folders.folder("game"), folders.folder("other")}, true);
    REQUIRE(report.reassigned.size() == 1);
    CHECK(report.reassigned[0].find("models/z_copy.glb") != std::string::npos);
    CHECK(registry.find("models/a.glb")->id == a_id);
    CHECK(registry.find("models/z_copy.glb")->id != a_id);
    CHECK(moteur::parse_asset_meta(folders.read("game/models/z_copy.glb.meta"), "z").id == registry.find("models/z_copy.glb")->id);

    // Without writing: a new asset gets a temporary identifier, nothing is written.
    folders.write("game/sounds/d.ogg", "d");
    report = registry.scan({folders.folder("game")}, false);
    CHECK(report.created == std::vector<std::string>{"sounds/d.ogg"});
    CHECK_FALSE(folders.exists("game/sounds/d.ogg.meta"));
}

TEST_CASE("asset registry: moving an asset keeps its identity; the frozen form") {
    TempFolders folders;
    folders.write("game/models/a.glb", "a");
    moteur::AssetRegistry registry;
    registry.scan({folders.folder("game")}, true);
    const moteur::AssetId id = registry.find("models/a.glb")->id;
    std::string error;
    CHECK(registry.move("models/a.glb", "props/renamed.glb", error));
    CHECK(error.empty());
    CHECK(folders.exists("game/props/renamed.glb"));
    CHECK(folders.exists("game/props/renamed.glb.meta"));
    CHECK_FALSE(folders.exists("game/models/a.glb.meta"));
    CHECK(registry.find(id)->key == "props/renamed.glb");
    CHECK(registry.find("models/a.glb") == nullptr);
    CHECK_FALSE(registry.move("models/a.glb", "x.glb", error));
    folders.write("game/other.glb", "o");
    registry.scan({folders.folder("game")}, true);
    CHECK_FALSE(registry.move("props/renamed.glb", "other.glb", error));
    CHECK(error.find("exists already") != std::string::npos);
    CHECK(registry.find(id)->key == "props/renamed.glb");  // a new scan finds it where it went

    const std::vector<std::uint8_t> bytes = registry.encode();
    const moteur::AssetRegistry frozen = moteur::AssetRegistry::decode(bytes, "asset_registry.mreg");
    CHECK(frozen.size() == 2);
    CHECK(frozen.find(id)->key == "props/renamed.glb");
    CHECK(frozen.find(id)->root.empty());
    std::vector<std::uint8_t> damaged = bytes;
    damaged.resize(damaged.size() - 3);
    CHECK_THROWS_AS(moteur::AssetRegistry::decode(damaged, "x"), std::runtime_error);
    CHECK_THROWS_AS(moteur::AssetRegistry::decode({'M', 'D', 'A', 'T'}, "x"), std::runtime_error);
}

TEST_CASE("mounted asset folders: a file missing from the assets is read from them") {
    TempFolders folders;
    folders.write("mounted/props/only_here.txt", "mounted");
    moteur::mount_asset_folder(folders.folder("mounted"));
    const std::string path = moteur::asset_path("props/only_here.txt");
    CHECK(moteur::file_exists(path));
    CHECK(moteur::read_text_file(path) == "mounted");
    CHECK(moteur::disk_path(path) == folders.folder("mounted") + "props/only_here.txt");
    CHECK_FALSE(moteur::file_exists(moteur::asset_path("props/nowhere.txt")));
    CHECK_FALSE(moteur::file_exists(folders.folder("elsewhere") + "props/only_here.txt"));  // outside the assets
    moteur::unmount_asset_folders();
    CHECK_FALSE(moteur::file_exists(path));
}
