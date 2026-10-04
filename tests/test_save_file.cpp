#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "moteur/file_io.hpp"
#include "moteur/save_file.hpp"

// Milestone 7, part 5: save files.

namespace {

struct TempDir {
    std::filesystem::path root;
    TempDir() {
        static int counter = 0;
        root = std::filesystem::temp_directory_path() / ("moteur_save_test_" + std::to_string(++counter));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    std::string str() const {
        const std::u8string text = (root / "").u8string();
        return std::string(text.begin(), text.end());
    }
};

moteur::SaveGame sample() {
    moteur::SaveGame save;
    save.header.game_version = "0.7";
    save.header.time = 1790000000;
    save.header.play_seconds = 754.5;
    save.header.summary = {{"lieu", "Crypte"}, {"niveau", 3}};
    save.set_section("monde", 2, {{"tick", 12345}, {"position", {1.25, 0.1}}, {"nom", "Salle des échos"}});
    save.set_section("joueur", 1, {{"vie", 0.7f}});
    return save;
}

}  // namespace

TEST_CASE("CRC-32 matches the reference value") {
    const char* text = "123456789";
    CHECK(moteur::crc32(text, 9) == 0xCBF43926u);
    // Continued in two parts, the same.
    CHECK(moteur::crc32(text + 4, 5, moteur::crc32(text, 4)) == 0xCBF43926u);
}

TEST_CASE("A save round-trips in JSON and in CBOR, floats exactly") {
    const moteur::SaveGame save = sample();
    for (const moteur::SaveEncoding encoding : {moteur::SaveEncoding::Json, moteur::SaveEncoding::Cbor}) {
        std::string error;
        const auto bytes = save.encode(encoding);
        const auto back = moteur::SaveGame::decode(bytes, error);
        INFO(error);
        REQUIRE(back);
        CHECK(back->header.game_version == "0.7");
        CHECK(back->header.time == 1790000000);
        CHECK(back->header.play_seconds == 754.5);
        CHECK(back->header.summary["lieu"] == "Crypte");
        CHECK(back->section_version("monde") == 2);
        CHECK((*back->section("monde"))["nom"] == "Salle des échos");
        CHECK((*back->section("joueur"))["vie"].get<float>() == 0.7f);  // bit for bit
        CHECK(back->section("absent") == nullptr);
        const auto header = moteur::SaveGame::decode_header(bytes, error);
        REQUIRE(header);
        CHECK(header->summary["niveau"] == 3);
    }
    CHECK(save.encode(moteur::SaveEncoding::Cbor).size() < save.encode(moteur::SaveEncoding::Json).size());
}

TEST_CASE("Damaged, truncated and future saves are refused") {
    std::vector<std::uint8_t> bytes = sample().encode(moteur::SaveEncoding::Json);
    std::string error;
    std::vector<std::uint8_t> flipped = bytes;
    flipped[bytes.size() / 2] ^= 0x40;
    CHECK_FALSE(moteur::SaveGame::decode(flipped, error));
    CHECK(error.find("somme de contrôle") != std::string::npos);
    std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() - 10));
    CHECK_FALSE(moteur::SaveGame::decode(cut, error));
    CHECK(error.find("tronquée") != std::string::npos);
    std::vector<std::uint8_t> future = bytes;
    future[4] = 9;
    CHECK_FALSE(moteur::SaveGame::decode(future, error));
    CHECK(error.find("plus récent") != std::string::npos);
    CHECK_FALSE(moteur::SaveGame::decode(std::vector<std::uint8_t>{'n', 'o', 'p', 'e'}, error));
}

TEST_CASE("Migrations upgrade old sections step by step") {
    moteur::SaveMigrations migrations;
    migrations.add("monde", 1, [](nlohmann::json& d) { d["tick"] = d["tick"].get<int>() * 2; });          // 1 -> 2
    migrations.add("monde", 2, [](nlohmann::json& d) { d["version3"] = true; });                          // 2 -> 3
    CHECK(migrations.current("monde") == 3);
    CHECK(migrations.current("joueur") == 1);
    moteur::SaveGame old;
    old.set_section("monde", 1, {{"tick", 10}});
    old.set_section("joueur", 1, {{"vie", 1}});
    std::string error;
    const auto back = moteur::SaveGame::decode(old.encode(moteur::SaveEncoding::Json), error, &migrations);
    INFO(error);
    REQUIRE(back);
    CHECK(back->section_version("monde") == 3);
    CHECK((*back->section("monde"))["tick"] == 20);
    CHECK((*back->section("monde"))["version3"] == true);
    moteur::SaveGame newer;
    newer.set_section("joueur", 4, {});
    CHECK_FALSE(moteur::SaveGame::decode(newer.encode(moteur::SaveEncoding::Json), error, &migrations));
    CHECK(error.find("plus récente que le jeu") != std::string::npos);
}

TEST_CASE("Slots: atomic writes, backup kept, damaged file falls back to the backup") {
    const TempDir dir;
    const moteur::SaveSlots slots(dir.str());
    std::string error;
    moteur::SaveGame first = sample();
    REQUIRE(slots.write("emplacement_1", first, moteur::SaveEncoding::Json, error));
    moteur::SaveGame second = sample();
    second.header.time = 1790000500;
    second.set_section("joueur", 1, {{"vie", 0.2f}});
    REQUIRE(slots.write("emplacement_1", second, moteur::SaveEncoding::Cbor, error));
    CHECK(moteur::path_exists(slots.path("emplacement_1") + ".bak"));
    CHECK_FALSE(moteur::path_exists(slots.path("emplacement_1") + ".tmp"));
    REQUIRE(slots.write("auto", first, moteur::SaveEncoding::Json, error));

    const auto list = slots.list();
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "emplacement_1");  // the most recent first
    CHECK(list[0].readable);

    bool from_backup = true;
    auto read = slots.read("emplacement_1", error, nullptr, &from_backup);
    REQUIRE(read);
    CHECK_FALSE(from_backup);
    CHECK((*read->section("joueur"))["vie"].get<float>() == 0.2f);

    // The file damaged (a crash in the middle of something else): the backup is read.
    {
        std::ofstream damage(dir.root / "emplacement_1.sav", std::ios::binary | std::ios::trunc);
        damage << "MSAV garbage";
    }
    read = slots.read("emplacement_1", error, nullptr, &from_backup);
    REQUIRE(read);
    CHECK(from_backup);
    CHECK((*read->section("joueur"))["vie"].get<float>() == 0.7f);
    CHECK(slots.list()[0].from_backup == false);  // auto is now... the damaged one lists from its backup
    // A write that never reached the rename (only the .tmp left) does not touch the slot.
    {
        std::ofstream half(dir.root / "auto.sav.tmp", std::ios::binary);
        half << "MSAV half";
    }
    REQUIRE(slots.read("auto", error));
    slots.remove("emplacement_1");
    CHECK_FALSE(moteur::path_exists(slots.path("emplacement_1")));
    CHECK_FALSE(slots.read("absent", error));
}

TEST_CASE("write_file_atomic replaces the content and keeps a backup on request") {
    const TempDir dir;
    const std::string path = dir.str() + "réglages.txt";  // a non-ASCII name
    std::string error;
    REQUIRE(moteur::write_file_atomic(path, std::string("un"), error));
    REQUIRE(moteur::write_file_atomic(path, std::string("deux"), error, true));
    const auto bytes = moteur::read_file_bytes(path);
    REQUIRE(bytes);
    CHECK(std::string(bytes->begin(), bytes->end()) == "deux");
    const auto backup = moteur::read_file_bytes(path + ".bak");
    REQUIRE(backup);
    CHECK(std::string(backup->begin(), backup->end()) == "un");
    CHECK(moteur::file_time(path) > 0);
    CHECK(moteur::file_time(path + ".absent") == 0);
    // Finer than a second: a change right after a read is noticed (the map editor relies on it).
    const std::int64_t before = moteur::file_time(path);
    SDL_Delay(20);
    REQUIRE(moteur::write_file_atomic(path, std::string("trois"), error));
    CHECK(moteur::file_time(path) != before);
}
