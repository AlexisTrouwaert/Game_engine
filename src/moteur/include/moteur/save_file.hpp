#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace moteur {

/// How a save's header and body are written (milestone 7, part 5). A save is a header (what a menu
/// shows: date, play time, the game's own summary) and named sections (one per system: "monde",
/// "joueur", "exploration"...), each with its own version. The same content is written as readable
/// JSON (development) or compact CBOR (released game); reading takes either.
///
/// File layout (integers little-endian): `MSAV`, format version (u32), encoding (u8: 0 JSON,
/// 1 CBOR), header length (u32), header, body length (u32), body, CRC-32 of everything before it
/// (u32). The header can be decoded without the body (listing the slots). Files are written
/// atomically, the previous one kept as a backup (write_file_atomic()), and a damaged file is
/// refused, never half-read.
///
/// `Json`: indented JSON text, readable and diffable (development). `Cbor`: binary CBOR, smaller and
/// faster to read (released game).
enum class SaveEncoding : std::uint8_t { Json = 0, Cbor = 1 };

/// What a save menu shows about a save, readable without its body (SaveGame::decode_header()).
/// The game fills it before writing; the engine sets nothing itself.
struct SaveHeader {
    std::string game_version;       ///< the game's, for messages ("sauvegarde de la version 0.3")
    std::int64_t time = 0;          ///< when it was written, seconds since 1970 (UTC); orders SaveSlots::list()
    double play_seconds = 0.0;      ///< time played, in seconds, for the menu
    nlohmann::json summary = nlohmann::json::object();  ///< the game's: place, level, character...
};

/// Upgrades old sections, step by step: a function from version n to n + 1 per section. Section
/// versions start at 1. Register every step at start-up, before reading saves.
class SaveMigrations {
public:
    /// Rewrites a section's data, in place, from one version to the next. It may throw (a
    /// std::exception): SaveGame::decode() then refuses the file.
    using Step = std::function<void(nlohmann::json& data)>;
    /// The function that turns version `from` of `section` into version `from + 1` (replaces the
    /// step already given for that version, if any).
    void add(const std::string& section, int from, Step step);
    /// The version the game writes now (the last step's target, or 1 if the section has no step).
    int current(const std::string& section) const;
    /// Brings `data` from `version` to the current one, updating both. False, with `error`, for a
    /// version from the future or a missing step, such as a version below 1 with no step from it
    /// (`data` may then be partly upgraded).
    bool upgrade(const std::string& section, int& version, nlohmann::json& data, std::string& error) const;

private:
    std::map<std::string, std::map<int, Step>> steps_;
};

/// The content of one save file: a header and named, versioned JSON sections, in memory. Encoding
/// and decoding are plain CPU work on byte vectors; SaveSlots does the files.
class SaveGame {
public:
    /// The file format version encode() writes. decode() reads it and any older one, and refuses a
    /// newer one ("update the game").
    static constexpr std::uint32_t kFormatVersion = 1;

    /// Written with the file, readable alone (decode_header()).
    SaveHeader header;

    /// Adds the section `name`, or replaces it, with `data` at `version` (usually
    /// SaveMigrations::current() of that section).
    void set_section(const std::string& name, int version, nlohmann::json data);
    /// Whether the save has a section of this name.
    bool has_section(const std::string& name) const { return sections_.count(name) != 0; }
    /// The data of a section, null if there is no such section. The pointer stays valid until the
    /// section is replaced or the SaveGame is destroyed or assigned.
    const nlohmann::json* section(const std::string& name) const;
    /// The version of a section's data (after decode() with migrations: the current one); 0 if
    /// there is no such section.
    int section_version(const std::string& name) const;
    /// The names of the sections, sorted.
    std::vector<std::string> section_names() const;

    /// The whole file's bytes (layout: see SaveEncoding), checksum included. Allocates; JSON text is
    /// indented with tabs. Throws nlohmann::json::type_error if a JSON string is not valid UTF-8
    /// (with SaveEncoding::Json).
    std::vector<std::uint8_t> encode(SaveEncoding encoding) const;
    /// Reads a whole file; with `migrations`, every section is brought to its current version.
    /// nullopt, with `error`, if the file is damaged, from a future format, or a section cannot be
    /// upgraded. Never throws.
    static std::optional<SaveGame> decode(const std::vector<std::uint8_t>& bytes, std::string& error,
                                          const SaveMigrations* migrations = nullptr);
    /// Reads only the header, after checking the checksum of the whole file. nullopt, with `error`,
    /// as decode(). Missing header keys take their default values.
    static std::optional<SaveHeader> decode_header(const std::vector<std::uint8_t>& bytes, std::string& error);

private:
    struct Section {
        int version = 1;
        nlohmann::json data;
    };
    std::map<std::string, Section> sections_;
};

/// The save slots of a game: a directory of `<name>.sav` files (with their `<name>.sav.bak` backups
/// and `<name>.png` thumbnails). A slot's name is the file name without extension, used as is
/// (UTF-8; the game keeps it to characters the file system accepts). Every call does blocking file
/// I/O: call from one thread at a time, outside the frame's hot path.
class SaveSlots {
public:
    /// One slot as listed by list().
    struct Slot {
        std::string name;          ///< the slot's name (file name without `.sav`)
        std::string path;          ///< the `.sav` file's path (path())
        bool readable = false;     ///< the header was read, from the file or its backup
        bool from_backup = false;  ///< the file was damaged; the backup was read instead
        std::string error;         ///< why the `.sav` file itself could not be read; empty if it was
        SaveHeader header;         ///< the header read; default values if not #readable
        std::string thumbnail;     ///< path of the thumbnail, empty if none
    };

    /// `directory` ends with a separator; it need not exist yet (write() creates it).
    explicit SaveSlots(std::string directory);
    /// The slots' directory, as given (ending with a separator).
    const std::string& directory() const { return directory_; }
    /// The file of the slot `name`: `directory() + name + ".sav"`.
    std::string path(const std::string& name) const { return directory_ + name + ".sav"; }
    /// Where the game writes the slot's thumbnail, and where list() looks for it: `<name>.png`.
    std::string thumbnail_path(const std::string& name) const { return directory_ + name + ".png"; }

    /// Every slot (each `.sav` file of the directory), the most recent first (SaveHeader::time, then
    /// the name). Unreadable slots are listed too, with readable false. Reads each file whole to
    /// check its checksum. Empty if the directory does not exist.
    std::vector<Slot> list() const;
    /// Writes atomically (creating the directory if needed), keeping the previous file as
    /// `<name>.sav.bak`. False with `error`; the previous file is then untouched.
    bool write(const std::string& name, const SaveGame& save, SaveEncoding encoding, std::string& error) const;
    /// Reads a slot (decoded as SaveGame::decode(), with `migrations`); if the file is damaged or
    /// missing, its backup, and then sets `*from_backup` (when given) to true. nullopt with `error`
    /// (both reasons) if neither can be read.
    std::optional<SaveGame> read(const std::string& name, std::string& error, const SaveMigrations* migrations = nullptr,
                                 bool* from_backup = nullptr) const;
    /// Removes a slot, its backup and its thumbnail. Files that do not exist or cannot be removed are
    /// silently skipped.
    void remove(const std::string& name) const;

private:
    std::string directory_;
};

}  // namespace moteur
