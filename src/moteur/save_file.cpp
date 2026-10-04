#include "moteur/save_file.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "moteur/file_io.hpp"

namespace moteur {

namespace {

const char kMagic[4] = {'M', 'S', 'A', 'V'};

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

bool get_u32(const std::vector<std::uint8_t>& bytes, std::size_t& at, std::uint32_t& value) {
    if (at + 4 > bytes.size()) {
        return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[at + static_cast<std::size_t>(i)]) << (8 * i);
    }
    at += 4;
    return true;
}

std::vector<std::uint8_t> encode_json(const nlohmann::json& value, SaveEncoding encoding) {
    if (encoding == SaveEncoding::Cbor) {
        return nlohmann::json::to_cbor(value);
    }
    const std::string text = value.dump(1, '\t');
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

nlohmann::json decode_json(const std::uint8_t* begin, const std::uint8_t* end, SaveEncoding encoding) {
    if (encoding == SaveEncoding::Cbor) {
        return nlohmann::json::from_cbor(begin, end);
    }
    return nlohmann::json::parse(begin, end);
}

// The parts of a file, checked: encoding, header bytes, body bytes.
struct Parts {
    SaveEncoding encoding = SaveEncoding::Json;
    std::size_t header_at = 0, header_size = 0, body_at = 0, body_size = 0;
};

bool split(const std::vector<std::uint8_t>& bytes, Parts& parts, std::string& error) {
    if (bytes.size() < 4 + 4 + 1 + 4 + 4 + 4 || std::memcmp(bytes.data(), kMagic, 4) != 0) {
        error = "pas une sauvegarde";
        return false;
    }
    std::size_t at = 4;
    std::uint32_t format = 0;
    get_u32(bytes, at, format);
    if (format > SaveGame::kFormatVersion) {
        error = "sauvegarde d'un format plus récent (" + std::to_string(format) + ") : mettre le jeu à jour";
        return false;
    }
    const std::uint8_t encoding = bytes[at++];
    if (encoding > 1) {
        error = "encodage inconnu";
        return false;
    }
    parts.encoding = static_cast<SaveEncoding>(encoding);
    std::uint32_t header_size = 0, body_size = 0;
    if (!get_u32(bytes, at, header_size) || at + header_size > bytes.size()) {
        error = "sauvegarde tronquée";
        return false;
    }
    parts.header_at = at;
    parts.header_size = header_size;
    at += header_size;
    if (!get_u32(bytes, at, body_size) || at + body_size + 4 != bytes.size()) {
        error = "sauvegarde tronquée";
        return false;
    }
    parts.body_at = at;
    parts.body_size = body_size;
    at += body_size;
    std::uint32_t stored = 0;
    get_u32(bytes, at, stored);
    if (crc32(bytes.data(), bytes.size() - 4) != stored) {
        error = "sauvegarde abîmée (somme de contrôle fausse)";
        return false;
    }
    return true;
}

SaveHeader header_from(const nlohmann::json& json) {
    SaveHeader header;
    header.game_version = json.value("game_version", std::string());
    header.time = json.value("time", std::int64_t{0});
    header.play_seconds = json.value("play_seconds", 0.0);
    header.summary = json.value("summary", nlohmann::json::object());
    return header;
}

}  // namespace

void SaveMigrations::add(const std::string& section, int from, Step step) {
    steps_[section][from] = std::move(step);
}

int SaveMigrations::current(const std::string& section) const {
    const auto found = steps_.find(section);
    if (found == steps_.end() || found->second.empty()) {
        return 1;
    }
    return found->second.rbegin()->first + 1;
}

bool SaveMigrations::upgrade(const std::string& section, int& version, nlohmann::json& data, std::string& error) const {
    const int target = current(section);
    if (version > target) {
        error = "section " + section + " en version " + std::to_string(version) + ", plus récente que le jeu (" +
                std::to_string(target) + ")";
        return false;
    }
    const auto found = steps_.find(section);
    while (version < target) {
        const auto step = found->second.find(version);
        if (step == found->second.end()) {
            error = "section " + section + " : aucune migration depuis la version " + std::to_string(version);
            return false;
        }
        step->second(data);
        ++version;
    }
    return true;
}

void SaveGame::set_section(const std::string& name, int version, nlohmann::json data) {
    sections_[name] = Section{version, std::move(data)};
}

const nlohmann::json* SaveGame::section(const std::string& name) const {
    const auto found = sections_.find(name);
    return found == sections_.end() ? nullptr : &found->second.data;
}

int SaveGame::section_version(const std::string& name) const {
    const auto found = sections_.find(name);
    return found == sections_.end() ? 0 : found->second.version;
}

std::vector<std::string> SaveGame::section_names() const {
    std::vector<std::string> names;
    for (const auto& [name, section] : sections_) {
        names.push_back(name);
    }
    return names;
}

std::vector<std::uint8_t> SaveGame::encode(SaveEncoding encoding) const {
    nlohmann::json head;
    head["game_version"] = header.game_version;
    head["time"] = header.time;
    head["play_seconds"] = header.play_seconds;
    head["summary"] = header.summary;
    nlohmann::json body;
    body["sections"] = nlohmann::json::object();
    for (const auto& [name, section] : sections_) {
        body["sections"][name] = {{"version", section.version}, {"data", section.data}};
    }
    const std::vector<std::uint8_t> head_bytes = encode_json(head, encoding);
    const std::vector<std::uint8_t> body_bytes = encode_json(body, encoding);
    std::vector<std::uint8_t> out(kMagic, kMagic + 4);
    put_u32(out, kFormatVersion);
    out.push_back(static_cast<std::uint8_t>(encoding));
    put_u32(out, static_cast<std::uint32_t>(head_bytes.size()));
    out.insert(out.end(), head_bytes.begin(), head_bytes.end());
    put_u32(out, static_cast<std::uint32_t>(body_bytes.size()));
    out.insert(out.end(), body_bytes.begin(), body_bytes.end());
    put_u32(out, crc32(out.data(), out.size()));
    return out;
}

std::optional<SaveHeader> SaveGame::decode_header(const std::vector<std::uint8_t>& bytes, std::string& error) {
    Parts parts;
    if (!split(bytes, parts, error)) {
        return std::nullopt;
    }
    try {
        const std::uint8_t* begin = bytes.data() + parts.header_at;
        return header_from(decode_json(begin, begin + parts.header_size, parts.encoding));
    } catch (const nlohmann::json::exception& e) {
        error = std::string("en-tête illisible : ") + e.what();
        return std::nullopt;
    }
}

std::optional<SaveGame> SaveGame::decode(const std::vector<std::uint8_t>& bytes, std::string& error,
                                         const SaveMigrations* migrations) {
    Parts parts;
    if (!split(bytes, parts, error)) {
        return std::nullopt;
    }
    SaveGame save;
    try {
        const std::uint8_t* head = bytes.data() + parts.header_at;
        save.header = header_from(decode_json(head, head + parts.header_size, parts.encoding));
        const std::uint8_t* body_begin = bytes.data() + parts.body_at;
        const nlohmann::json body = decode_json(body_begin, body_begin + parts.body_size, parts.encoding);
        for (const auto& [name, value] : body.at("sections").items()) {
            Section section{value.at("version").get<int>(), value.at("data")};
            if (migrations != nullptr && !migrations->upgrade(name, section.version, section.data, error)) {
                return std::nullopt;
            }
            save.sections_[name] = std::move(section);
        }
    } catch (const nlohmann::json::exception& e) {
        error = std::string("contenu illisible : ") + e.what();
        return std::nullopt;
    }
    return save;
}

SaveSlots::SaveSlots(std::string directory) : directory_(std::move(directory)) {}

std::vector<SaveSlots::Slot> SaveSlots::list() const {
    std::vector<Slot> slots;
    std::error_code ec;
    const std::filesystem::path folder(std::u8string(directory_.begin(), directory_.end()));
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".sav") {
            continue;
        }
        const std::u8string stem = entry.path().stem().u8string();
        Slot slot;
        slot.name.assign(stem.begin(), stem.end());
        slot.path = path(slot.name);
        bool from_backup = false;
        std::optional<SaveHeader> header;
        if (const auto bytes = read_file_bytes(slot.path)) {
            header = SaveGame::decode_header(*bytes, slot.error);
        } else {
            slot.error = "lecture impossible";
        }
        if (!header) {
            std::string backup_error;
            if (const auto bytes = read_file_bytes(slot.path + ".bak")) {
                header = SaveGame::decode_header(*bytes, backup_error);
                from_backup = header.has_value();
            }
        }
        slot.readable = header.has_value();
        slot.from_backup = from_backup;
        if (header) {
            slot.header = *header;
        }
        if (path_exists(thumbnail_path(slot.name))) {
            slot.thumbnail = thumbnail_path(slot.name);
        }
        slots.push_back(std::move(slot));
    }
    std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
        return a.header.time != b.header.time ? a.header.time > b.header.time : a.name < b.name;
    });
    return slots;
}

bool SaveSlots::write(const std::string& name, const SaveGame& save, SaveEncoding encoding, std::string& error) const {
    const std::vector<std::uint8_t> bytes = save.encode(encoding);
    return write_file_atomic(path(name), bytes.data(), bytes.size(), error, true);
}

std::optional<SaveGame> SaveSlots::read(const std::string& name, std::string& error, const SaveMigrations* migrations,
                                        bool* from_backup) const {
    if (from_backup != nullptr) {
        *from_backup = false;
    }
    std::string main_error = "fichier absent";
    if (const auto bytes = read_file_bytes(path(name))) {
        if (auto save = SaveGame::decode(*bytes, main_error, migrations)) {
            return save;
        }
    }
    std::string backup_error = "pas de copie de secours";
    if (const auto bytes = read_file_bytes(path(name) + ".bak")) {
        if (auto save = SaveGame::decode(*bytes, backup_error, migrations)) {
            if (from_backup != nullptr) {
                *from_backup = true;
            }
            return save;
        }
    }
    error = main_error + " ; copie de secours : " + backup_error;
    return std::nullopt;
}

void SaveSlots::remove(const std::string& name) const {
    std::error_code ec;
    for (const std::string& file : {path(name), path(name) + ".bak", thumbnail_path(name)}) {
        std::filesystem::remove(std::filesystem::path(std::u8string(file.begin(), file.end())), ec);
    }
}

}  // namespace moteur
