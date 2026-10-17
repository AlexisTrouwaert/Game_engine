// The player's own machine (milestone 8, part 8): a user name with accents, spaces and symbols in the
// preferences folder, a save that cannot be written (full disk, folder taken away), a preferences
// folder where nothing can be written. The game must keep the player's data intact and go on.

#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "moteur/file_io.hpp"
#include "moteur/log_file.hpp"
#include "moteur/save_file.hpp"

namespace {

// A fresh folder under the tests' preferences, named like a player's folder can be.
std::string fresh_folder(const std::string& name) {
    char* prefs = SDL_GetPrefPath("moteur", "tests");
    const std::string folder = std::string(prefs != nullptr ? prefs : "") + name + "/";
    SDL_free(prefs);
    std::error_code error;
    const std::filesystem::path path(std::u8string(folder.begin(), folder.end()));
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path, error);
    REQUIRE_MESSAGE(!error, "cannot create " << folder);
    return folder;
}

moteur::SaveGame save_with(int tick) {
    moteur::SaveGame game;
    game.header.game_version = "essai";
    game.set_section("world", 1, {{"tick", tick}});
    return game;
}

int tick_of(const moteur::SaveGame& game) {
    return game.section("world")->at("tick").get<int>();
}

}  // namespace

TEST_CASE("Saves, files and the log work under a folder with accents, spaces and symbols") {
    const std::string folder = fresh_folder("Éloïse Ça + œuvre (1)/sauvegardes à part");
    const moteur::SaveSlots slots(folder);
    std::string error;
    REQUIRE_MESSAGE(slots.write("partie é", save_with(42), moteur::SaveEncoding::Cbor, error), error);
    const std::optional<moteur::SaveGame> read = slots.read("partie é", error);
    REQUIRE_MESSAGE(read, error);
    CHECK(tick_of(*read) == 42);
    REQUIRE(slots.list().size() == 1);
    CHECK(slots.list()[0].name == "partie é");

    REQUIRE_MESSAGE(moteur::write_file_atomic(folder + "réglages.json", std::string("{}"), error), error);
    CHECK(moteur::path_exists(folder + "réglages.json"));
    CHECK(moteur::read_file_bytes(folder + "réglages.json").has_value());

    moteur::LogFile log;
    CHECK(log.open(folder));
    SDL_Log("a line in a folder with accents");
    log.close();
    const auto text = moteur::read_file_bytes(folder + "log.txt");
    REQUIRE(text.has_value());
    CHECK(std::string(text->begin(), text->end()).find("a line in a folder with accents") != std::string::npos);
}

TEST_CASE("A save that cannot be written leaves the previous one intact, and says why") {
    const std::string folder = fresh_folder("save_failure");
    const moteur::SaveSlots slots(folder);
    std::string error;
    REQUIRE(slots.write("slot", save_with(1), moteur::SaveEncoding::Json, error));

    // The temporary file cannot be created (taken by a folder: as a full disk, the write fails before
    // anything replaces the save).
    std::error_code ec;
    const std::string temporary = slots.path("slot") + ".tmp";
    std::filesystem::create_directories(std::filesystem::path(std::u8string(temporary.begin(), temporary.end())), ec);
    error.clear();
    CHECK_FALSE(slots.write("slot", save_with(2), moteur::SaveEncoding::Json, error));
    CHECK_FALSE(error.empty());
    const std::optional<moteur::SaveGame> kept = slots.read("slot", error);
    REQUIRE_MESSAGE(kept, error);
    CHECK(tick_of(*kept) == 1);  // the previous save, whole

    // Once writing works again, the save goes through; the previous one becomes the backup.
    std::filesystem::remove_all(std::filesystem::path(std::u8string(temporary.begin(), temporary.end())), ec);
    REQUIRE(slots.write("slot", save_with(3), moteur::SaveEncoding::Json, error));
    const std::string main_file = slots.path("slot");
    std::filesystem::remove(std::filesystem::path(std::u8string(main_file.begin(), main_file.end())), ec);
    bool from_backup = false;
    const std::optional<moteur::SaveGame> backup = slots.read("slot", error, nullptr, &from_backup);
    REQUIRE_MESSAGE(backup, error);
    CHECK(from_backup);
    CHECK(tick_of(*backup) == 1);
}

TEST_CASE("A preferences folder where nothing can be written: refused with a reason, never a crash") {
    // A "folder" that is a file: nothing can be created under it, as with a read-only or vanished folder.
    const std::string folder = fresh_folder("not_a_folder");
    std::string error;
    REQUIRE(moteur::write_file_atomic(folder + "file", std::string("x"), error));
    const std::string impossible = folder + "file/prefs/";

    const moteur::SaveSlots slots(impossible);
    CHECK_FALSE(slots.write("slot", save_with(1), moteur::SaveEncoding::Cbor, error));
    CHECK_FALSE(error.empty());
    CHECK(slots.list().empty());
    CHECK_FALSE(slots.read("slot", error).has_value());

    moteur::LogFile log;
    CHECK_FALSE(log.open(impossible));  // the log goes on to the terminal only
    SDL_Log("still logging");
    log.close();
}
