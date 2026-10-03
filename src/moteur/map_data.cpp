#include "moteur/map_data.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace moteur {

namespace {

[[noreturn]] void fail(const std::string& source, const std::string& message) {
    throw std::runtime_error("Map '" + source + "': " + message);
}

std::string quoted(char c) {
    return std::string("'") + c + "'";
}

}  // namespace

MapData MapData::parse(std::string_view json_text, const std::string& source) {
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::exception& e) {
        fail(source, std::string("invalid JSON: ") + e.what());
    }
    MapData map;
    map.source_ = source;
    try {
        if (!json.is_object() || json.value("version", 0) != kVersion) {
            fail(source, "expected an object with \"version\": " + std::to_string(kVersion));
        }

        map.description_ = json.value("description", std::string());

        // Tiles: nlohmann's objects are sorted by key, so ids follow the names' order.
        for (const auto& [name, settings] : json.at("tiles").items()) {
            if (name.empty()) {
                fail(source, "a tile has an empty name");
            }
            TileType type;
            type.region = settings.value("region", std::string());
            type.walkable = settings.value("walkable", true);
            type.opaque = settings.value("opaque", false);
            map.tileset_.add(std::move(type));
            map.names_.push_back(name);
        }

        // Legend.
        std::size_t layers = 1;
        int by_character[256];
        std::fill(std::begin(by_character), std::end(by_character), -1);
        for (const auto& [key, value] : json.at("legend").items()) {
            if (key.size() != 1) {
                fail(source, "legend key \"" + key + "\" is not one character");
            }
            Symbol symbol;
            symbol.character = key[0];
            const nlohmann::json& tiles = value.is_object() ? value.at("tiles") : value;
            if (value.is_object()) {
                symbol.point = value.value("point", std::string());
            }
            for (const nlohmann::json& tile : tiles) {
                const std::string name = tile.get<std::string>();
                const TileId id = map.tile_id(name);
                if (id == kNoTile) {
                    fail(source, "legend " + quoted(symbol.character) + ": unknown tile \"" + name + "\"");
                }
                symbol.tiles.push_back(id);
            }
            for (const Symbol& other : map.symbols_) {
                if (other.tiles == symbol.tiles && other.point == symbol.point) {
                    fail(source, "legend " + quoted(other.character) + " and " + quoted(symbol.character) +
                                     " mean the same");
                }
            }
            layers = std::max(layers, symbol.tiles.size());
            by_character[static_cast<unsigned char>(symbol.character)] = static_cast<int>(map.symbols_.size());
            map.symbols_.push_back(std::move(symbol));
        }
        if (map.symbols_.size() > 255) {
            fail(source, "more than 255 characters in the legend");
        }

        // Rows.
        std::vector<std::string> rows;
        for (const nlohmann::json& row : json.at("rows")) {
            std::string text = row.get<std::string>();
            if (!text.empty() && text.back() == '\r') {
                text.pop_back();  // a file edited with Windows line ends pasted into strings
            }
            rows.push_back(std::move(text));
        }
        if (rows.empty() || rows[0].empty()) {
            fail(source, "the map is empty");
        }
        const auto width = static_cast<int>(rows[0].size());
        const auto height = static_cast<int>(rows.size());
        map.tiles_ = TileMap(width, height, static_cast<int>(layers));
        map.cell_symbols_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        for (int j = 0; j < height; ++j) {
            const std::string& row = rows[static_cast<std::size_t>(j)];
            if (static_cast<int>(row.size()) != width) {
                fail(source, "row " + std::to_string(j) + " has " + std::to_string(row.size()) +
                                 " characters instead of " + std::to_string(width));
            }
            for (int i = 0; i < width; ++i) {
                const char c = row[static_cast<std::size_t>(i)];
                const int found = by_character[static_cast<unsigned char>(c)];
                if (found < 0) {
                    fail(source, "column " + std::to_string(i) + ", row " + std::to_string(j) + ": " + quoted(c) +
                                     " is not in the legend");
                }
                const Symbol& symbol = map.symbols_[static_cast<std::size_t>(found)];
                for (std::size_t layer = 0; layer < symbol.tiles.size(); ++layer) {
                    map.tiles_.set(static_cast<int>(layer), {i, j}, symbol.tiles[layer]);
                }
                if (!symbol.point.empty()) {
                    map.points_[symbol.point].push_back({i, j});
                }
                map.cell_symbols_[static_cast<std::size_t>(j * width + i)] = static_cast<std::uint8_t>(found);
            }
        }
    } catch (const nlohmann::json::exception& e) {
        fail(source, e.what());
    }
    return map;
}

TileId MapData::tile_id(const std::string& name) const {
    const auto found = std::find(names_.begin(), names_.end(), name);
    return found == names_.end() ? kNoTile : static_cast<TileId>(found - names_.begin() + 1);
}

const std::string& MapData::tile_name(TileId id) const {
    static const std::string kNone;
    return id == kNoTile || id > names_.size() ? kNone : names_[id - 1];
}

const std::vector<glm::ivec2>& MapData::points(const std::string& name) const {
    static const std::vector<glm::ivec2> kNone;
    const auto found = points_.find(name);
    return found == points_.end() ? kNone : found->second;
}

glm::ivec2 MapData::point(const std::string& name) const {
    const std::vector<glm::ivec2>& cells = points(name);
    if (cells.empty()) {
        throw std::out_of_range("Map '" + source_ + "': no point \"" + name + "\"");
    }
    return cells.front();
}

std::string MapData::to_ascii() const {
    std::string text;
    const int width = tiles_.width();
    text.reserve(static_cast<std::size_t>((width + 1) * tiles_.height()));
    for (int j = 0; j < tiles_.height(); ++j) {
        for (int i = 0; i < width; ++i) {
            text += symbols_[cell_symbols_[static_cast<std::size_t>(j * width + i)]].character;
        }
        text += '\n';
    }
    return text;
}

}  // namespace moteur
