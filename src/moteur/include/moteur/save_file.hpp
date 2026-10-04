#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace moteur {

// Save files (milestone 7, part 5). A save is a header (what a menu shows: date, play time, the
// game's own summary) and named sections (one per system: "monde", "joueur", "exploration"...),
// each with its own version. The same content is written as readable JSON (development) or compact
// CBOR (released game); reading takes either.
//
// File layout: "MSAV", format version (u32), encoding (u8: 0 JSON, 1 CBOR), header length (u32),
// header, body length (u32), body, CRC-32 of everything before it (u32). The header can be read
// without the body (listing the slots). Files are written atomically, the previous one kept as a
// backup (write_file_atomic), and a damaged file is refused, never half-read.
enum class SaveEncoding : std::uint8_t { Json = 0, Cbor = 1 };

struct SaveHeader {
    std::string game_version;       // the game's, for messages ("sauvegarde de la version 0.3")
    std::int64_t time = 0;          // when it was written, seconds since 1970 (UTC)
    double play_seconds = 0.0;      // time played, for the menu
    nlohmann::json summary = nlohmann::json::object();  // the game's: place, level, character...
};

// Upgrades old sections, step by step: a function from version n to n + 1 per section.
class SaveMigrations {
public:
    using Step = std::function<void(nlohmann::json& data)>;
    // The function that turns version `from` of `section` into version `from + 1`.
    void add(const std::string& section, int from, Step step);
    // The version the game writes now (the last step's target, or 1).
    int current(const std::string& section) const;
    // Brings `data` from `version` to the current one. False, with `error`, for a version from the
    // future or a missing step.
    bool upgrade(const std::string& section, int& version, nlohmann::json& data, std::string& error) const;

private:
    std::map<std::string, std::map<int, Step>> steps_;
};

class SaveGame {
public:
    static constexpr std::uint32_t kFormatVersion = 1;

    SaveHeader header;

    void set_section(const std::string& name, int version, nlohmann::json data);
    bool has_section(const std::string& name) const { return sections_.count(name) != 0; }
    // Null if there is no such section.
    const nlohmann::json* section(const std::string& name) const;
    int section_version(const std::string& name) const;
    std::vector<std::string> section_names() const;

    std::vector<std::uint8_t> encode(SaveEncoding encoding) const;
    // Reads a whole file; with `migrations`, every section is brought to its current version.
    // nullopt, with `error`, if the file is damaged, from a future format, or a section cannot be
    // upgraded.
    static std::optional<SaveGame> decode(const std::vector<std::uint8_t>& bytes, std::string& error,
                                          const SaveMigrations* migrations = nullptr);
    // Reads only the header (checksum included).
    static std::optional<SaveHeader> decode_header(const std::vector<std::uint8_t>& bytes, std::string& error);

private:
    struct Section {
        int version = 1;
        nlohmann::json data;
    };
    std::map<std::string, Section> sections_;
};

// The save slots of a game: a directory of "<name>.sav" files (and "<name>.png" thumbnails).
class SaveSlots {
public:
    struct Slot {
        std::string name;
        std::string path;
        bool readable = false;
        bool from_backup = false;  // the file was damaged; the backup was read instead
        std::string error;
        SaveHeader header;
        std::string thumbnail;     // path of the thumbnail, empty if none
    };

    explicit SaveSlots(std::string directory);  // ends with a separator
    const std::string& directory() const { return directory_; }
    std::string path(const std::string& name) const { return directory_ + name + ".sav"; }
    std::string thumbnail_path(const std::string& name) const { return directory_ + name + ".png"; }

    // Every slot, the most recent first.
    std::vector<Slot> list() const;
    // Writes atomically, keeping the previous file as "<name>.sav.bak". False with `error`.
    bool write(const std::string& name, const SaveGame& save, SaveEncoding encoding, std::string& error) const;
    // Reads a slot; if the file is damaged or missing, its backup. nullopt with `error` if neither.
    std::optional<SaveGame> read(const std::string& name, std::string& error, const SaveMigrations* migrations = nullptr,
                                 bool* from_backup = nullptr) const;
    // Removes a slot, its backup and its thumbnail.
    void remove(const std::string& name) const;

private:
    std::string directory_;
};

}  // namespace moteur
