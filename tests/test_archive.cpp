#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/archive.hpp"
#include "moteur/file_io.hpp"
#include "moteur/paths.hpp"

namespace {

std::vector<std::uint8_t> bytes_of(const std::string& text) {
    return {text.begin(), text.end()};
}

std::string temporary(const std::string& name) {
    const char* folder = SDL_GetPrefPath("moteur", "tests");
    std::string path = std::string(folder != nullptr ? folder : "") + name;
    SDL_free(const_cast<char*>(folder));
    return path;
}

}  // namespace

TEST_CASE("archive: what is written is read back, compressed or not, sorted and aligned") {
    const std::string path = temporary("test_archive.pak");
    std::string big(20000, 'a');
    std::vector<moteur::ArchiveInput> inputs = {
        {"assets/maps/b.json", bytes_of("{\"b\": 2}"), true},
        {"assets/maps/a.json", bytes_of(big), true},          // compresses well
        {"assets/textures/t.ktx2", bytes_of("KTX2-bytes"), false},
        {"shaders/empty.dxil", {}, true},
    };
    std::string error;
    REQUIRE(moteur::write_archive(path, inputs, error));

    const auto archive = moteur::Archive::open(path, error);
    REQUIRE(archive != nullptr);
    REQUIRE(archive->entries().size() == 4);
    CHECK(archive->entries()[0].path == "assets/maps/a.json");  // sorted by path
    for (const moteur::Archive::Entry& entry : archive->entries()) {
        CHECK(entry.offset % moteur::Archive::kAlignment == 0);
    }
    const moteur::Archive::Entry* a = archive->find("assets/maps/a.json");
    REQUIRE(a != nullptr);
    CHECK(a->compression == 1);
    CHECK(a->stored_size < a->size);
    CHECK(archive->read(*a) == bytes_of(big));
    CHECK(archive->read(*archive->find("assets/maps/b.json")) == bytes_of("{\"b\": 2}"));
    CHECK(archive->read(*archive->find("assets/textures/t.ktx2")) == bytes_of("KTX2-bytes"));
    CHECK(archive->read(*archive->find("shaders/empty.dxil")).empty());
    CHECK(archive->find("assets/maps/A.json") == nullptr);  // exact case
    CHECK(archive->find("assets/maps/c.json") == nullptr);
    CHECK_FALSE(moteur::archive_should_compress("x/y.ogg"));
    CHECK(moteur::archive_should_compress("x/y.json"));
}

TEST_CASE("archive: paths that differ only by their case are refused") {
    std::string error;
    CHECK_FALSE(moteur::write_archive(temporary("test_collision.pak"),
                                      {{"assets/Maps/a.json", bytes_of("1"), true}, {"assets/maps/a.json", bytes_of("2"), true}},
                                      error));
    CHECK(error.find("collide") != std::string::npos);
}

TEST_CASE("archive: a damaged index or entry gives a message, never garbage") {
    const std::string path = temporary("test_damaged.pak");
    std::string error;
    REQUIRE(moteur::write_archive(path, {{"assets/a.json", bytes_of(std::string(5000, 'x')), false}}, error));
    auto bytes = moteur::read_file_bytes(path);
    REQUIRE(bytes);

    // A byte of the data changed: the archive opens, the entry is refused.
    std::vector<std::uint8_t> data = *bytes;
    data[data.size() - 10] ^= 0xFF;
    REQUIRE(moteur::write_file_atomic(path, data.data(), data.size(), error));
    auto archive = moteur::Archive::open(path, error);
    REQUIRE(archive != nullptr);
    CHECK_THROWS_WITH_AS(archive->read(archive->entries()[0]), doctest::Contains("CRC32"), std::runtime_error);

    // A byte of the index changed: refused when opened.
    std::vector<std::uint8_t> index = *bytes;
    index[30] ^= 0xFF;
    REQUIRE(moteur::write_file_atomic(path, index.data(), index.size(), error));
    error.clear();
    CHECK(moteur::Archive::open(path, error) == nullptr);
    CHECK(error.find("damaged") != std::string::npos);

    // Truncated (an interrupted copy): refused when opened.
    std::vector<std::uint8_t> truncated(bytes->begin(), bytes->end() - 100);
    REQUIRE(moteur::write_file_atomic(path, truncated.data(), truncated.size(), error));
    error.clear();
    CHECK(moteur::Archive::open(path, error) == nullptr);
    CHECK(error.find("truncated") != std::string::npos);

    // Not an archive at all.
    REQUIRE(moteur::write_file_atomic(path, std::string("hello, world, not an archive"), error));
    CHECK(moteur::Archive::open(path, error) == nullptr);
}

TEST_CASE("archive: once mounted, read_file finds its files under the program's folder") {
    const std::string path = temporary("test_mount.pak");
    std::string error;
    REQUIRE(moteur::write_archive(path, {{"assets/zz_archive_test/hello.json", bytes_of("{\"bonjour\": 1}"), true}}, error));
    REQUIRE(moteur::mount_archive(path, error));
    const std::string file = moteur::asset_path("zz_archive_test/hello.json");
    CHECK(moteur::file_exists(file));
    CHECK(moteur::read_text_file(file) == "{\"bonjour\": 1}");
    CHECK(moteur::read_text_file(moteur::asset_path("zz_archive_test/./x/../hello.json")) == "{\"bonjour\": 1}");
    CHECK_FALSE(moteur::file_exists(moteur::asset_path("zz_archive_test/absent.json")));

    // Archives only: a file of the program's folder outside them does not exist.
    moteur::set_archives_only(true);
    CHECK_THROWS_AS(moteur::read_file(moteur::base_path() + "moteur_tests.exe"), std::runtime_error);
    moteur::unmount_archives();
    CHECK_FALSE(moteur::archives_only());
    CHECK_FALSE(moteur::file_exists(file));
}
