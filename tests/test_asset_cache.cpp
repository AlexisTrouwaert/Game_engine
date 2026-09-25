#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/asset_cache.hpp"

namespace {

// A stand-in for a GPU asset: a value, and a count of how many were ever made.
struct Fake {
    int value = 0;
};

// A load function returning `value`, counting its calls, and naming `files`.
moteur::AssetCache<Fake>::Load loader(int value, int& calls, std::vector<std::string> files = {}) {
    return [value, &calls, files](std::vector<std::string>& out) {
        ++calls;
        out.insert(out.end(), files.begin(), files.end());
        return Fake{value};
    };
}

moteur::AssetCache<Fake>::Load failing(int& calls, std::vector<std::string> files = {}) {
    return [&calls, files](std::vector<std::string>& out) -> Fake {
        ++calls;
        out.insert(out.end(), files.begin(), files.end());
        throw std::runtime_error("cannot read the file");
    };
}

}  // namespace

TEST_CASE("normalize_asset_path gives one key per file") {
    CHECK(moteur::normalize_asset_path("models/a.gltf") == "models/a.gltf");
    CHECK(moteur::normalize_asset_path("models\\a.gltf") == "models/a.gltf");
    CHECK(moteur::normalize_asset_path("./models//a.gltf") == "models/a.gltf");
    CHECK(moteur::normalize_asset_path("models/barrel/../a.gltf") == "models/a.gltf");
    CHECK(moteur::normalize_asset_path("models/a.gltf/") == "models/a.gltf");
    CHECK(moteur::normalize_asset_path("Models/A.gltf") == "Models/A.gltf");  // case kept

    CHECK_THROWS_AS(moteur::normalize_asset_path(""), std::invalid_argument);
    CHECK_THROWS_AS(moteur::normalize_asset_path("."), std::invalid_argument);
    CHECK_THROWS_AS(moteur::normalize_asset_path("/models/a.gltf"), std::invalid_argument);
    CHECK_THROWS_AS(moteur::normalize_asset_path("C:/models/a.gltf"), std::invalid_argument);
    CHECK_THROWS_AS(moteur::normalize_asset_path("../secret.txt"), std::invalid_argument);
    CHECK_THROWS_AS(moteur::normalize_asset_path("models/../../secret.txt"), std::invalid_argument);
}

TEST_CASE("check_asset_case refuses a wrong case, even where the system accepts it") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "moteur_test_asset_case";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Models");
    std::ofstream(root / "Models" / "Barrel.gltf") << "{}";
    const std::string root_text = root.generic_string() + "/";

    CHECK_NOTHROW(moteur::check_asset_case(root_text, "Models/Barrel.gltf"));
    CHECK_THROWS_WITH_AS(moteur::check_asset_case(root_text, "models/Barrel.gltf"),
                         doctest::Contains("'models' is written 'Models'"), std::runtime_error);
    CHECK_THROWS_AS(moteur::check_asset_case(root_text, "Models/barrel.gltf"), std::runtime_error);
    // Missing files are not this function's business: the loader reports them.
    CHECK_NOTHROW(moteur::check_asset_case(root_text, "Models/missing.gltf"));
    CHECK_NOTHROW(moteur::check_asset_case(root_text, "Textures/a.png"));

    std::filesystem::remove_all(root);
}

TEST_CASE("AssetCache loads each key once and shares it") {
    moteur::AssetCache<Fake> cache("fake");
    int calls = 0;
    const moteur::Asset<Fake> a = cache.get("a", loader(1, calls));
    const moteur::Asset<Fake> again = cache.get("a", loader(2, calls));  // not called: already there
    const moteur::Asset<Fake> b = cache.get("b", loader(3, calls));

    CHECK(calls == 2);
    CHECK(a->value == 1);
    CHECK(again->value == 1);
    CHECK(&*a == &*again);
    CHECK(b->value == 3);
    CHECK(cache.size() == 2);
    CHECK(cache.loads() == 2);
    CHECK(cache.contains("a"));
    CHECK_FALSE(cache.contains("c"));
}

TEST_CASE("AssetCache frees only what nobody holds") {
    moteur::AssetCache<Fake> cache("fake");
    int calls = 0;
    moteur::Asset<Fake> kept = cache.get("kept", loader(1, calls));
    cache.get("dropped", loader(2, calls));  // handle not kept

    const std::vector<moteur::AssetInfo> infos = cache.infos();
    REQUIRE(infos.size() == 2);
    for (const moteur::AssetInfo& info : infos) {
        CHECK(info.users == (info.key == "kept" ? 1 : 0));
    }

    CHECK(cache.collect_garbage() == 1);
    CHECK(cache.contains("kept"));
    CHECK_FALSE(cache.contains("dropped"));
    CHECK(kept->value == 1);

    // Asked again after being freed: loaded again.
    cache.get("dropped", loader(2, calls));
    CHECK(calls == 3);

    kept = {};
    CHECK(cache.collect_garbage() == 2);
    CHECK(cache.size() == 0);
}

TEST_CASE("AssetCache replaces a failed asset by the placeholder, or throws without one") {
    int calls = 0;
    moteur::AssetCache<Fake> with_placeholder("fake", [] { return Fake{-1}; });
    const moteur::Asset<Fake> missing = with_placeholder.get("missing", failing(calls));
    CHECK(missing->value == -1);
    CHECK(with_placeholder.failures() == 1);
    CHECK(with_placeholder.loads() == 0);
    CHECK(with_placeholder.error("missing") == "cannot read the file");
    CHECK(with_placeholder.infos().front().failed);
    // Asked again: still the placeholder, no new attempt.
    with_placeholder.get("missing", failing(calls));
    CHECK(calls == 1);

    moteur::AssetCache<Fake> without("fake");
    CHECK_THROWS_WITH_AS(without.get("missing", failing(calls)), "cannot read the file", std::runtime_error);
    CHECK(without.failures() == 1);
    CHECK_FALSE(without.contains("missing"));
}

TEST_CASE("AssetCache reloads in place, and keeps the old content when the reload fails") {
    moteur::AssetCache<Fake> cache("fake", [] { return Fake{-1}; });
    int value = 1;
    int calls = 0;
    const auto current = [&value, &calls](std::vector<std::string>& files) {
        ++calls;
        files.push_back("textures/a.png");
        if (value < 0) {
            throw std::runtime_error("broken file");
        }
        return Fake{value};
    };
    const moteur::Asset<Fake> handle = cache.get("a", current);
    const Fake* address = &*handle;

    value = 2;
    CHECK(cache.reload("a"));
    CHECK(handle->value == 2);
    CHECK(&*handle == address);  // same object: every holder sees the change

    value = -1;
    CHECK_FALSE(cache.reload("a"));
    CHECK(handle->value == 2);
    CHECK(cache.infos().front().reload_error == "broken file");  // shown by the asset browser
    CHECK_FALSE(cache.infos().front().failed);

    value = 3;
    CHECK(cache.reload("a"));
    CHECK(cache.infos().front().reload_error.empty());

    CHECK_FALSE(cache.reload("unknown"));
    CHECK(calls == 4);
}

TEST_CASE("AssetCache fixes a placeholder when its file is repaired") {
    moteur::AssetCache<Fake> cache("fake", [] { return Fake{-1}; });
    bool broken = true;
    const auto load = [&broken](std::vector<std::string>& files) {
        files.push_back("models/a.gltf");  // named before failing, so the file is watched anyway
        if (broken) {
            throw std::runtime_error("broken file");
        }
        return Fake{7};
    };
    const moteur::Asset<Fake> handle = cache.get("models/a.gltf", load);
    CHECK(handle->value == -1);
    CHECK(cache.keys_using("models/a.gltf") == std::vector<std::string>{"models/a.gltf"});

    broken = false;
    CHECK(cache.reload("models/a.gltf"));
    CHECK(handle->value == 7);
    CHECK(cache.error("models/a.gltf").empty());
    CHECK_FALSE(cache.infos().front().failed);
}

TEST_CASE("AssetCache finds the assets made from a file") {
    moteur::AssetCache<Fake> cache("fake");
    int calls = 0;
    cache.get("models/a.gltf", loader(1, calls, {"models/a.gltf", "models/shared.bin"}));
    cache.get("models/b.gltf", loader(2, calls, {"models/b.gltf", "models/shared.bin"}));

    CHECK(cache.keys_using("models/a.gltf") == std::vector<std::string>{"models/a.gltf"});
    std::vector<std::string> shared = cache.keys_using("models/shared.bin");
    std::sort(shared.begin(), shared.end());
    CHECK(shared == std::vector<std::string>{"models/a.gltf", "models/b.gltf"});
    CHECK(cache.keys_using("models/c.gltf").empty());
}

TEST_CASE("AssetCache measures its assets") {
    moteur::AssetCache<Fake> cache("fake", {}, [](const Fake& fake) { return static_cast<std::size_t>(fake.value) * 100; });
    int calls = 0;
    const moteur::Asset<Fake> a = cache.get("a", loader(3, calls));
    const std::vector<moteur::AssetInfo> infos = cache.infos();
    REQUIRE(infos.size() == 1);
    CHECK(infos[0].bytes == 300);
    CHECK(infos[0].key == "a");
}

TEST_CASE("AssetCache reloads through its replace function, which may refuse") {
    const auto replace = [](Fake& current, Fake&& fresh) {
        if (fresh.value < 0) {
            throw std::runtime_error("structure changed");
        }
        current.value += fresh.value;  // visibly not a plain assignment
    };
    moteur::AssetCache<Fake> cache("fake", {}, {}, replace);
    int value = 5;
    const moteur::Asset<Fake> handle = cache.get("a", [&value](std::vector<std::string>&) { return Fake{value}; });
    CHECK(cache.reload("a"));
    CHECK(handle->value == 10);
    value = -1;
    CHECK_FALSE(cache.reload("a"));
    CHECK(handle->value == 10);
}
