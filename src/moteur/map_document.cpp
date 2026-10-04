#include "moteur/map_document.hpp"

#include <algorithm>
#include <charconv>
#include <deque>
#include <stdexcept>

#include "moteur/nav_grid.hpp"
#include "moteur/pathfinding.hpp"

namespace moteur {

namespace {

// Characters given to new pairs (stack, point) when a map is written, in this order.
const std::string kSymbolPool =
    ".#abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!$&*+-/<>?^_~|:;=%,'()[]{}`";

std::string number(float value) {
    if (value == static_cast<float>(static_cast<long long>(value)) && value > -1e9f && value < 1e9f) {
        return std::to_string(static_cast<long long>(value));
    }
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);  // shortest that reads back
    return std::string(buffer, result.ptr);
}

std::string json_text(const std::string& text) {
    return nlohmann::json(text).dump();
}

// Compact JSON with numbers as short as they read back: a float (most values come from floats)
// gives "0.55657434", not the 17 digits of its double.
std::string compact(const nlohmann::json& value) {
    switch (value.type()) {
        case nlohmann::json::value_t::object: {
            std::string text = "{";
            bool first = true;
            for (const auto& [key, item] : value.items()) {
                text += (first ? "" : ", ") + json_text(key) + ": " + compact(item);
                first = false;
            }
            return text + "}";
        }
        case nlohmann::json::value_t::array: {
            std::string text = "[";
            for (std::size_t i = 0; i < value.size(); ++i) {
                text += (i ? ", " : "") + compact(value[i]);
            }
            return text + "]";
        }
        case nlohmann::json::value_t::number_float: {
            const double d = value.get<double>();
            const auto f = static_cast<float>(d);
            char buffer[32];
            const auto result = static_cast<double>(f) == d ? std::to_chars(buffer, buffer + sizeof(buffer), f)
                                                            : std::to_chars(buffer, buffer + sizeof(buffer), d);
            std::string text(buffer, result.ptr);
            if (text.find_first_of(".eEn") == std::string::npos) {
                text += ".0";  // still a number with a fraction when read back
            }
            return text;
        }
        default:
            return value.dump();
    }
}

std::vector<TileId> trimmed(std::vector<TileId> stack) {
    while (!stack.empty() && stack.back() == kNoTile) {
        stack.pop_back();
    }
    return stack;
}

const char* direction_name(MapConnector::Direction direction) {
    switch (direction) {
        case MapConnector::Direction::North: return "nord";
        case MapConnector::Direction::East: return "est";
        case MapConnector::Direction::South: return "sud";
        default: return "ouest";
    }
}

}  // namespace

MapDocument::MapDocument(int width, int height, int layers, const std::string& floor)
    : width_(std::max(width, 1)), height_(std::max(height, 1)), layers_(std::max(layers, 1)) {
    types_.push_back({floor, true, false, {}});
    cells_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * static_cast<std::size_t>(layers_), kNoTile);
    for (int j = 0; j < height_; ++j) {
        for (int i = 0; i < width_; ++i) {
            cells_[index(0, {i, j})] = 1;
        }
    }
    legend_.push_back({'.', {1}, {}});
}

MapDocument MapDocument::from(const MapData& map) {
    MapDocument doc;
    doc.width_ = map.width();
    doc.height_ = map.height();
    doc.layers_ = map.tiles().layer_count();
    doc.description = map.description();
    doc.chunk = map.chunk();
    for (std::size_t i = 0; i < map.tile_names().size(); ++i) {
        const moteur::TileType& t = map.tileset().type(static_cast<TileId>(i + 1));
        doc.types_.push_back({map.tile_names()[i], t.walkable, t.opaque, t.region});
    }
    doc.cells_.assign(static_cast<std::size_t>(doc.width_) * static_cast<std::size_t>(doc.height_) * static_cast<std::size_t>(doc.layers_), kNoTile);
    for (int layer = 0; layer < doc.layers_; ++layer) {
        for (int j = 0; j < doc.height_; ++j) {
            for (int i = 0; i < doc.width_; ++i) {
                doc.cells_[doc.index(layer, {i, j})] = map.tiles().at(layer, {i, j});
            }
        }
    }
    doc.legend_ = map.symbols();
    for (const auto& [name, cells] : map.points()) {
        for (const glm::ivec2 cell : cells) {
            doc.points_[{cell.x, cell.y}] = name;
        }
    }
    doc.objects_ = map.objects();
    doc.connectors_ = map.connectors();
    for (const MapObject& object : doc.objects_) {
        if (object.id.size() > 1 && object.id[0] == 'o') {
            std::uint32_t n = 0;
            const auto result = std::from_chars(object.id.data() + 1, object.id.data() + object.id.size(), n);
            if (result.ec == std::errc() && result.ptr == object.id.data() + object.id.size()) {
                doc.next_object_ = std::max(doc.next_object_, n + 1);
            }
        }
    }
    return doc;
}

TileId MapDocument::tile(int layer, glm::ivec2 cell) const {
    if (layer < 0 || layer >= layers_ || !contains(cell)) {
        return kNoTile;
    }
    return cells_[index(layer, cell)];
}

void MapDocument::changed() {
    ++revision_;
}

bool MapDocument::set_tile(int layer, glm::ivec2 cell, TileId id) {
    if (layer < 0 || layer >= layers_ || !contains(cell) || id > types_.size()) {
        return false;
    }
    TileId& slot = cells_[index(layer, cell)];
    if (slot == id) {
        return false;
    }
    const bool own_action = !current_;
    if (own_action) {
        begin_action("modification");
    }
    current_->tiles.push_back({layer, cell, slot, id});
    slot = id;
    changed();
    if (own_action) {
        end_action();
    }
    return true;
}

std::vector<TileId> MapDocument::stack(glm::ivec2 cell) const {
    std::vector<TileId> result(static_cast<std::size_t>(layers_), kNoTile);
    if (contains(cell)) {
        for (int layer = 0; layer < layers_; ++layer) {
            result[static_cast<std::size_t>(layer)] = cells_[index(layer, cell)];
        }
    }
    return result;
}

void MapDocument::set_stack(glm::ivec2 cell, const std::vector<TileId>& stack) {
    for (int layer = 0; layer < layers_; ++layer) {
        set_tile(layer, cell, static_cast<std::size_t>(layer) < stack.size() ? stack[static_cast<std::size_t>(layer)] : kNoTile);
    }
}

TileId MapDocument::tile_id(const std::string& name) const {
    for (std::size_t i = 0; i < types_.size(); ++i) {
        if (types_[i].name == name) {
            return static_cast<TileId>(i + 1);
        }
    }
    return kNoTile;
}

MapDocument::Extras MapDocument::extras(bool with_cells) const {
    Extras e{types_, legend_, points_, objects_, connectors_, description, chunk, width_, height_, layers_, {}, next_object_};
    if (with_cells) {
        e.cells = cells_;
    }
    return e;
}

void MapDocument::restore(const Extras& e) {
    types_ = e.types;
    legend_ = e.legend;
    points_ = e.points;
    objects_ = e.objects;
    connectors_ = e.connectors;
    description = e.description;
    chunk = e.chunk;
    next_object_ = e.next_object;
    if (!e.cells.empty()) {
        width_ = e.width;
        height_ = e.height;
        layers_ = e.layers;
        cells_ = e.cells;
    }
    changed();
}

void MapDocument::touch_extras(bool with_cells) {
    if (!current_) {
        return;
    }
    if (!current_->extras_before) {
        current_->extras_before = extras(with_cells);
    } else if (with_cells && current_->extras_before->cells.empty()) {
        current_->extras_before->cells = cells_;
        current_->extras_before->width = width_;
        current_->extras_before->height = height_;
        current_->extras_before->layers = layers_;
    }
}

TileId MapDocument::add_tile_type(const TileType& type) {
    if (const TileId existing = tile_id(type.name); existing != kNoTile) {
        return existing;
    }
    const bool own = !current_;
    if (own) begin_action("type de tuile");
    touch_extras();
    types_.push_back(type);
    changed();
    if (own) end_action();
    return static_cast<TileId>(types_.size());
}

void MapDocument::set_tile_type(TileId id, const TileType& type) {
    if (id == kNoTile || id > types_.size()) {
        return;
    }
    const bool own = !current_;
    if (own) begin_action("type de tuile");
    touch_extras();
    types_[id - 1] = type;
    changed();
    if (own) end_action();
}

void MapDocument::set_symbol(const MapSymbol& symbol) {
    const bool own = !current_;
    if (own) begin_action("légende");
    touch_extras();
    MapSymbol clean = symbol;
    clean.tiles = trimmed(clean.tiles);
    auto found = std::find_if(legend_.begin(), legend_.end(), [&](const MapSymbol& s) { return s.character == symbol.character; });
    if (found != legend_.end()) {
        *found = clean;
    } else {
        legend_.push_back(clean);
    }
    changed();
    if (own) end_action();
}

std::vector<MapSymbol> MapDocument::palette() const {
    std::vector<MapSymbol> result;
    for (const MapSymbol& symbol : legend_) {
        if (!symbol.point.empty()) {
            continue;
        }
        const bool seen = std::any_of(result.begin(), result.end(), [&](const MapSymbol& s) { return s.tiles == symbol.tiles; });
        if (!seen) {
            result.push_back(symbol);
        }
    }
    return result;
}

std::optional<std::string> MapDocument::point_at(glm::ivec2 cell) const {
    const auto found = points_.find({cell.x, cell.y});
    if (found == points_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void MapDocument::set_point(glm::ivec2 cell, const std::string& name) {
    if (!contains(cell)) {
        return;
    }
    const auto current = point_at(cell);
    if ((name.empty() && !current) || (current && *current == name)) {
        return;
    }
    const bool own = !current_;
    if (own) begin_action("point");
    touch_extras();
    if (name.empty()) {
        points_.erase({cell.x, cell.y});
    } else {
        points_[{cell.x, cell.y}] = name;
    }
    changed();
    if (own) end_action();
}

std::map<std::string, std::vector<glm::ivec2>> MapDocument::points() const {
    std::map<std::string, std::vector<glm::ivec2>> result;
    // Reading order (row after row), as MapData gives them.
    std::vector<std::pair<std::pair<int, int>, std::string>> sorted(points_.begin(), points_.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first.second != b.first.second ? a.first.second < b.first.second : a.first.first < b.first.first;
    });
    for (const auto& [cell, name] : sorted) {
        result[name].push_back({cell.first, cell.second});
    }
    return result;
}

const MapObject* MapDocument::object(const std::string& id) const {
    for (const MapObject& object : objects_) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

std::string MapDocument::add_object(MapObject object) {
    const bool own = !current_;
    if (own) begin_action("objet");
    touch_extras();
    if (object.id.empty() || this->object(object.id) != nullptr) {
        do {
            object.id = "o" + std::to_string(next_object_++);
        } while (this->object(object.id) != nullptr);
    }
    const std::string id = object.id;
    objects_.push_back(std::move(object));
    changed();
    if (own) end_action();
    return id;
}

void MapDocument::update_object(const MapObject& object) {
    auto found = std::find_if(objects_.begin(), objects_.end(), [&](const MapObject& o) { return o.id == object.id; });
    if (found == objects_.end()) {
        return;
    }
    const bool own = !current_;
    if (own) begin_action("objet");
    touch_extras();
    *found = object;
    changed();
    if (own) end_action();
}

void MapDocument::remove_object(const std::string& id) {
    if (object(id) == nullptr) {
        return;
    }
    const bool own = !current_;
    if (own) begin_action("suppression");
    touch_extras();
    std::erase_if(objects_, [&](const MapObject& o) { return o.id == id; });
    changed();
    if (own) end_action();
}

void MapDocument::set_connectors(std::vector<MapConnector> connectors) {
    const bool own = !current_;
    if (own) begin_action("connecteurs");
    touch_extras();
    connectors_ = std::move(connectors);
    changed();
    if (own) end_action();
}

void MapDocument::set_description(std::string text) {
    if (text == description) {
        return;
    }
    const bool own = !current_;
    if (own) begin_action("description");
    touch_extras();
    description = std::move(text);
    changed();
    if (own) end_action();
}

void MapDocument::set_chunk(bool value) {
    if (value == chunk) {
        return;
    }
    const bool own = !current_;
    if (own) begin_action("morceau");
    touch_extras();
    chunk = value;
    changed();
    if (own) end_action();
}

std::vector<glm::ivec2> MapDocument::line(glm::ivec2 a, glm::ivec2 b) {
    std::vector<glm::ivec2> cells;
    const int dx = std::abs(b.x - a.x), sx = a.x < b.x ? 1 : -1;
    const int dy = -std::abs(b.y - a.y), sy = a.y < b.y ? 1 : -1;
    int error = dx + dy;
    glm::ivec2 p = a;
    for (;;) {
        cells.push_back(p);
        if (p == b) {
            break;
        }
        const int e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            p.x += sx;
        }
        if (e2 <= dx) {
            error += dx;
            p.y += sy;
        }
    }
    return cells;
}

int MapDocument::rect(glm::ivec2 a, glm::ivec2 b, const std::vector<TileId>& stack, bool hollow) {
    const glm::ivec2 low = glm::max(glm::min(a, b), glm::ivec2(0));
    const glm::ivec2 high = glm::min(glm::max(a, b), glm::ivec2(width_ - 1, height_ - 1));
    int changed_cells = 0;
    for (int j = low.y; j <= high.y; ++j) {
        for (int i = low.x; i <= high.x; ++i) {
            const glm::ivec2 corner_a = glm::min(a, b), corner_b = glm::max(a, b);
            if (hollow && i != corner_a.x && i != corner_b.x && j != corner_a.y && j != corner_b.y) {
                continue;
            }
            if (this->stack({i, j}) != stack) {
                set_stack({i, j}, stack);
                ++changed_cells;
            }
        }
    }
    return changed_cells;
}

int MapDocument::fill(glm::ivec2 start, const std::vector<TileId>& stack) {
    if (!contains(start)) {
        return 0;
    }
    std::vector<TileId> wanted = stack;
    wanted.resize(static_cast<std::size_t>(layers_), kNoTile);
    const std::vector<TileId> from = this->stack(start);
    if (from == wanted) {
        return 0;
    }
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0);
    std::deque<glm::ivec2> open{start};
    seen[static_cast<std::size_t>(start.y * width_ + start.x)] = 1;
    int count = 0;
    while (!open.empty()) {
        const glm::ivec2 cell = open.front();
        open.pop_front();
        set_stack(cell, wanted);
        ++count;
        for (const glm::ivec2 d : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)}) {
            const glm::ivec2 n = cell + d;
            if (contains(n) && !seen[static_cast<std::size_t>(n.y * width_ + n.x)] && this->stack(n) == from) {
                seen[static_cast<std::size_t>(n.y * width_ + n.x)] = 1;
                open.push_back(n);
            }
        }
    }
    return count;
}

void MapDocument::resize(int left, int top, int right, int bottom, const std::vector<TileId>& stack) {
    const int width = std::max(1, width_ + left + right);
    const int height = std::max(1, height_ + top + bottom);
    const bool own = !current_;
    if (own) begin_action("redimensionnement");
    touch_extras(true);
    current_->resized = true;
    std::vector<TileId> cells(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(layers_), kNoTile);
    const auto at = [&](int layer, int i, int j) {
        return (static_cast<std::size_t>(layer) * static_cast<std::size_t>(height) + static_cast<std::size_t>(j)) *
                   static_cast<std::size_t>(width) + static_cast<std::size_t>(i);
    };
    for (int layer = 0; layer < layers_; ++layer) {
        for (int j = 0; j < height; ++j) {
            for (int i = 0; i < width; ++i) {
                const glm::ivec2 old(i - left, j - top);
                cells[at(layer, i, j)] = contains(old) ? cells_[index(layer, old)]
                                         : static_cast<std::size_t>(layer) < stack.size() ? stack[static_cast<std::size_t>(layer)]
                                                                                          : kNoTile;
            }
        }
    }
    std::map<std::pair<int, int>, std::string> points;
    for (const auto& [cell, name] : points_) {
        const glm::ivec2 moved(cell.first + left, cell.second + top);
        if (moved.x >= 0 && moved.y >= 0 && moved.x < width && moved.y < height) {
            points[{moved.x, moved.y}] = name;
        }
    }
    std::vector<MapObject> objects;
    for (MapObject object : objects_) {
        object.position.x += static_cast<float>(left);
        object.position.z += static_cast<float>(top);
        if (object.position.x >= 0.0f && object.position.z >= 0.0f && object.position.x <= static_cast<float>(width) &&
            object.position.z <= static_cast<float>(height)) {
            objects.push_back(std::move(object));
        }
    }
    std::vector<MapConnector> connectors;
    for (MapConnector connector : connectors_) {
        connector.cell += glm::ivec2(left, top);
        if (connector.cell.x >= 0 && connector.cell.y >= 0 && connector.cell.x < width && connector.cell.y < height) {
            connectors.push_back(std::move(connector));
        }
    }
    width_ = width;
    height_ = height;
    cells_ = std::move(cells);
    points_ = std::move(points);
    objects_ = std::move(objects);
    connectors_ = std::move(connectors);
    changed();
    if (own) end_action();
}

void MapDocument::begin_action(std::string name) {
    if (current_) {
        end_action();
    }
    current_ = Action{std::move(name), {}, std::nullopt, std::nullopt, false};
}

void MapDocument::end_action() {
    if (!current_) {
        return;
    }
    Action action = std::move(*current_);
    current_.reset();
    if (action.tiles.empty() && !action.extras_before) {
        return;  // nothing changed
    }
    if (action.extras_before) {
        action.extras_after = extras(!action.extras_before->cells.empty());
    }
    undo_.push_back(std::move(action));
    redo_.clear();
    if (undo_.size() > 500) {
        undo_.erase(undo_.begin());
    }
}

bool MapDocument::undo() {
    end_action();
    if (undo_.empty()) {
        return false;
    }
    Action action = std::move(undo_.back());
    undo_.pop_back();
    if (action.extras_before) {
        restore(*action.extras_before);
    }
    for (auto it = action.tiles.rbegin(); it != action.tiles.rend(); ++it) {
        cells_[index(it->layer, it->cell)] = it->before;
    }
    changed();
    redo_.push_back(std::move(action));
    return true;
}

bool MapDocument::redo() {
    end_action();
    if (redo_.empty()) {
        return false;
    }
    Action action = std::move(redo_.back());
    redo_.pop_back();
    for (const TileEdit& edit : action.tiles) {
        cells_[index(edit.layer, edit.cell)] = edit.after;
    }
    if (action.extras_after) {
        restore(*action.extras_after);
    }
    changed();
    undo_.push_back(std::move(action));
    return true;
}

const std::string& MapDocument::undo_name() const {
    static const std::string kNone;
    return undo_.empty() ? kNone : undo_.back().name;
}

std::vector<MapDocument::Issue> MapDocument::validate() const {
    std::vector<Issue> issues;
    const auto all_points = points();
    const auto start = all_points.find("depart");
    if (!chunk && start == all_points.end()) {
        issues.push_back({Issue::Level::Error, "pas de point « depart »", {}});
    }
    MapData map;
    try {
        map = to_map_data();
    } catch (const std::exception& e) {
        issues.push_back({Issue::Level::Error, std::string("carte illisible : ") + e.what(), {}});
        return issues;
    }
    const NavGrid grid(map.tiles(), map.tileset());
    if (start != all_points.end()) {
        const glm::ivec2 from = start->second.front();
        if (!grid.walkable(from)) {
            issues.push_back({Issue::Level::Error, "le départ est sur une case bloquée", {from}});
        } else {
            const ClearanceMap clearance(grid);
            FlowField field;
            field.compute(grid, clearance, cell_centre(from), 0.3f, 1 << 30);
            Issue unreachable{Issue::Level::Warning, "", {}};
            for (int j = 0; j < height_; ++j) {
                for (int i = 0; i < width_; ++i) {
                    if (grid.walkable({i, j}) && clearance.fits({i, j}, 0.3f) && field.cost({i, j}) == FlowField::kUnreached) {
                        unreachable.cells.push_back({i, j});
                    }
                }
            }
            if (!unreachable.cells.empty()) {
                unreachable.message = std::to_string(unreachable.cells.size()) + " case(s) praticable(s) inaccessible(s) depuis le départ";
                issues.push_back(std::move(unreachable));
            }
        }
    }
    for (const MapObject& object : objects_) {
        if (object.position.x < 0.0f || object.position.z < 0.0f || object.position.x > static_cast<float>(width_) ||
            object.position.z > static_cast<float>(height_)) {
            issues.push_back({Issue::Level::Warning, "objet " + object.id + " hors de la carte", {}});
        }
    }
    for (const MapConnector& connector : connectors_) {
        const bool edge = connector.cell.x == 0 || connector.cell.y == 0 || connector.cell.x == width_ - 1 ||
                          connector.cell.y == height_ - 1;
        if (!edge || !grid.walkable(connector.cell)) {
            issues.push_back({Issue::Level::Warning, "connecteur " + connector.name + " pas sur une case praticable du bord",
                              {connector.cell}});
        }
    }
    return issues;
}

std::string MapDocument::to_json() const {
    // The character of every pair (stack, point): the legend's first, then new ones.
    std::vector<MapSymbol> symbols = legend_;
    const auto find_symbol = [&symbols](const std::vector<TileId>& stack, const std::string& point) -> int {
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i].tiles == stack && symbols[i].point == point) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    std::vector<int> cell_symbol(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0);
    for (int j = 0; j < height_; ++j) {
        for (int i = 0; i < width_; ++i) {
            const std::vector<TileId> s = trimmed(stack({i, j}));
            const std::string point = point_at({i, j}).value_or(std::string());
            int found = find_symbol(s, point);
            if (found < 0) {
                char character = 0;
                for (const char c : kSymbolPool) {
                    if (std::none_of(symbols.begin(), symbols.end(), [c](const MapSymbol& m) { return m.character == c; })) {
                        character = c;
                        break;
                    }
                }
                if (character == 0) {
                    throw std::runtime_error("MapDocument: too many different cells for the legend");
                }
                symbols.push_back({character, s, point});
                found = static_cast<int>(symbols.size()) - 1;
            }
            cell_symbol[static_cast<std::size_t>(j * width_ + i)] = found;
        }
    }
    const auto tile_names = [this](const std::vector<TileId>& stack) {
        std::string text = "[";
        for (std::size_t i = 0; i < stack.size(); ++i) {
            text += (i ? ", " : "") + (stack[i] == kNoTile ? std::string("\"-\"") : json_text(types_[stack[i] - 1].name));
        }
        return text + "]";
    };

    std::string out = "{\n  \"version\": 2,\n  \"description\": " + json_text(description) + ",\n";
    if (chunk) {
        out += "  \"chunk\": true,\n";
    }
    std::vector<TileType> types = types_;
    std::sort(types.begin(), types.end(), [](const TileType& a, const TileType& b) { return a.name < b.name; });
    out += "  \"tiles\": {\n";
    for (std::size_t i = 0; i < types.size(); ++i) {
        const TileType& t = types[i];
        std::string fields;
        if (!t.walkable) fields += "\"walkable\": false";
        if (t.opaque) fields += std::string(fields.empty() ? "" : ", ") + "\"opaque\": true";
        if (!t.region.empty()) fields += std::string(fields.empty() ? "" : ", ") + "\"region\": " + json_text(t.region);
        out += "    " + json_text(t.name) + ": {" + fields + "}" + (i + 1 < types.size() ? "," : "") + "\n";
    }
    out += "  },\n  \"legend\": {\n";
    // Sorted by character: the same order however the legend was built (a file read back sorts it so).
    std::vector<std::size_t> order(symbols.size());
    for (std::size_t k = 0; k < order.size(); ++k) {
        order[k] = k;
    }
    std::sort(order.begin(), order.end(), [&symbols](std::size_t a, std::size_t b) {
        return static_cast<unsigned char>(symbols[a].character) < static_cast<unsigned char>(symbols[b].character);
    });
    for (std::size_t i = 0; i < order.size(); ++i) {
        const MapSymbol& s = symbols[order[i]];
        out += "    " + json_text(std::string(1, s.character)) + ": ";
        out += s.point.empty() ? tile_names(s.tiles) : "{\"tiles\": " + tile_names(s.tiles) + ", \"point\": " + json_text(s.point) + "}";
        out += (i + 1 < symbols.size() ? ",\n" : "\n");
    }
    out += "  },\n  \"rows\": [\n";
    for (int j = 0; j < height_; ++j) {
        std::string row;
        for (int i = 0; i < width_; ++i) {
            row += symbols[static_cast<std::size_t>(cell_symbol[static_cast<std::size_t>(j * width_ + i)])].character;
        }
        out += "    " + json_text(row) + (j + 1 < height_ ? ",\n" : "\n");
    }
    out += "  ]";
    if (!objects_.empty()) {
        out += ",\n  \"objects\": [\n";
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            const MapObject& o = objects_[i];
            out += "    {\"id\": " + json_text(o.id) + ", \"type\": " + json_text(o.type) + ", \"x\": " + number(o.position.x) +
                   ", \"z\": " + number(o.position.z);
            if (o.position.y != 0.0f) out += ", \"y\": " + number(o.position.y);
            if (o.rotation != 0.0f) out += ", \"rotation\": " + number(o.rotation);
            if (o.scale != 1.0f) out += ", \"scale\": " + number(o.scale);
            if (!o.props.empty()) out += ", \"props\": " + compact(o.props);
            out += std::string("}") + (i + 1 < objects_.size() ? ",\n" : "\n");
        }
        out += "  ]";
    }
    if (!connectors_.empty()) {
        out += ",\n  \"connectors\": [\n";
        for (std::size_t i = 0; i < connectors_.size(); ++i) {
            const MapConnector& c = connectors_[i];
            out += "    {\"name\": " + json_text(c.name) + ", \"x\": " + std::to_string(c.cell.x) + ", \"z\": " +
                   std::to_string(c.cell.y) + ", \"direction\": \"" + direction_name(c.direction) + "\"" +
                   (c.kind.empty() ? "" : ", \"kind\": " + json_text(c.kind)) + "}" + (i + 1 < connectors_.size() ? ",\n" : "\n");
        }
        out += "  ]";
    }
    out += "\n}\n";
    return out;
}

MapData MapDocument::to_map_data(const std::string& source) const {
    return MapData::parse(to_json(), source);
}

}  // namespace moteur
