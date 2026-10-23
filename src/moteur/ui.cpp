#include "moteur/ui.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>

#include "moteur/application.hpp"
#include "moteur/assets.hpp"
#include "moteur/font_library.hpp"
#include "moteur/input.hpp"
#include "moteur/localization.hpp"
#include "moteur/paths.hpp"
#include "moteur/renderer.hpp"
#include "moteur/sprite_renderer.hpp"
#include "moteur/ui_markup.hpp"
#include "moteur/ui_style.hpp"
#include "moteur/utf8.hpp"

namespace moteur {

// --- The model --------------------------------------------------------------------------------------

void UiModel::set(std::string_view path, UiValue value) {
    auto it = values_.find(path);
    if (it != values_.end() && it->second.value == value) {
        return;  // unchanged: nothing redone
    }
    ++version_;
    if (it == values_.end()) {
        values_.emplace(std::string(path), Entry{std::move(value), version_});
    } else {
        it->second.value = std::move(value);
        it->second.version = version_;
    }
}

UiValue UiModel::get(std::string_view path, const UiValue& fallback) const {
    const auto it = values_.find(path);
    return it == values_.end() ? fallback : it->second.value;
}

double UiModel::number(std::string_view path, double fallback) const {
    const auto it = values_.find(path);
    if (it == values_.end()) return fallback;
    if (const auto* d = std::get_if<double>(&it->second.value)) return *d;
    if (const auto* b = std::get_if<bool>(&it->second.value)) return *b ? 1.0 : 0.0;
    return fallback;
}

std::string UiModel::text(std::string_view path) const {
    const auto it = values_.find(path);
    if (it == values_.end()) return {};
    if (const auto* s = std::get_if<std::string>(&it->second.value)) return *s;
    return {};
}

bool UiModel::flag(std::string_view path, bool fallback) const {
    const auto it = values_.find(path);
    if (it == values_.end()) return fallback;
    if (const auto* b = std::get_if<bool>(&it->second.value)) return *b;
    if (const auto* d = std::get_if<double>(&it->second.value)) return *d != 0.0;
    if (const auto* s = std::get_if<std::string>(&it->second.value)) return !s->empty() && *s != "false" && *s != "0";
    return fallback;
}

bool UiModel::has(std::string_view path) const {
    return values_.count(path) != 0;
}

std::uint64_t UiModel::version_of(std::string_view path) const {
    const auto it = values_.find(path);
    return it == values_.end() ? 0 : it->second.version;
}

UiInput UiInput::from_actions(const Input& input) {
    UiInput in;
    const auto pressed = [&input](const char* name) {
        const ActionId action = input.find(name);
        return action != kNoAction && input.pressed(action);
    };
    const auto down = [&input](const char* name) {
        const ActionId action = input.find(name);
        return action != kNoAction && input.down(action);
    };
    in.up = pressed("menu_up");
    in.down = pressed("menu_down");
    in.left = pressed("menu_left");
    in.right = pressed("menu_right");
    in.confirm = pressed("menu_confirm");
    in.back = pressed("pause") || pressed("menu_back");
    in.click = pressed("menu_click");
    in.held = down("menu_click");
    in.wheel = (pressed("menu_wheel_up") ? 1.0f : 0.0f) - (pressed("menu_wheel_down") ? 1.0f : 0.0f);
    in.pointer = input.pointer();
    return in;
}

// --- The tree ---------------------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kRoundedShape = 1;  // the rounded square of the panels, in the glyph atlas
constexpr float kShapeRadius = 16.0f;       // its corner radius, texels
constexpr float kShapePad = 8.0f;           // texels around it (the distance range)
constexpr float kShapeCore = 4.0f;          // texels of straight edge between the corners

struct Anim {
    glm::vec4 from{0.0f}, to{0.0f}, value{0.0f};
    float time = 0, duration = 0, delay = 0;
    ui::Easing easing = ui::Easing::Ease;
    bool running = false;
};

}  // namespace

namespace {
struct DocumentImpl;
}  // namespace

struct UiNode {
    DocumentImpl* document = nullptr;  // on the root only: the document it is the root of
    std::string tag, id;
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<ui::Declaration> inline_style;
    std::string style_override;  // UiElement::set_style()
    std::string text_template;
    std::string text;  // resolved
    UiNode* parent = nullptr;
    std::vector<std::unique_ptr<UiNode>> children;
    UiElement handle{this};

    bool visible = true;
    bool enabled = true;
    bool focusable = false;
    bool is_template = false;
    bool instance = false;
    std::uint64_t instance_frame = 0;
    std::uint64_t instance_order = 0;  // when the game last asked for it: the drawing order
    int repeat_index = -1;
    std::string item_prefix;  // "slots.3." for `$item.` in a repeat
    std::shared_ptr<ui::MarkupNode> repeat_markup;  // a <repeat>: its children, made per item
    int repeat_count = -1;

    // Control.
    std::string bind;
    double min = 0, max = 1, step = 0.1;
    std::vector<std::string> options;  // a <select>'s option templates
    std::size_t caret = 0;
    int max_length = 256;

    ui::ComputedStyle style;
    std::map<std::string, std::string, std::less<>> variables;
    // Shown values (transitions).
    bool shown = false;
    Anim opacity, background, color, border, fill, translate, scale;

    // Layout: the border box in window pixels (before transitions' translate and scale).
    glm::vec2 pos{0.0f}, size{0.0f};
    float content_extent = 0;  // a scroll's content height
    float scroll = 0;
    std::optional<glm::vec2> screen_position;

    std::string attribute(std::string_view name) const {
        for (const auto& [k, v] : attributes) {
            if (k == name) return v;
        }
        return {};
    }
    bool has_attribute(std::string_view name) const {
        for (const auto& [k, v] : attributes) {
            if (k == name) return true;
        }
        return false;
    }
};

namespace {

struct DocumentImpl {
    std::string path;
    int layer = 0;
    bool visible = true;
    bool modal = true;
    std::string back_action;
    std::uint64_t frame = 0;  // the interface's frame count at its last render (world instances)
    std::uint64_t instance_sequence = 0;  // counts the requests of instances
    std::unique_ptr<UiNode> root;
    std::vector<std::string> sheet_paths;
    std::vector<ui::StyleSheet> sheets;
    std::vector<std::string> problems;
    UiNode* focus = nullptr;
    UiNode* hover = nullptr;
    UiNode* pressed = nullptr;  // the slider being dragged
    bool restyle = true;
    bool relayout = true;
    std::uint64_t model_seen = 0;
    std::uint64_t language_seen = 0;
    std::map<std::string, std::shared_ptr<ui::MarkupNode>, std::less<>> templates;
    std::map<std::pair<std::string, std::uint64_t>, UiNode*> instances;
    glm::vec2 viewport{0.0f};
    float scale_seen = 0;
    UiDocument handle{this};
    // Tooltip.
    UiNode* tooltip_target = nullptr;
    float tooltip_time = 0;
};

UiNode* node_of(void* p) {
    return static_cast<UiNode*>(p);
}

std::vector<std::string> split_words(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == ' ' || c == '\t' || c == '\n') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool is_control_tag(std::string_view tag) {
    return tag == "button" || tag == "checkbox" || tag == "slider" || tag == "select" || tag == "input";
}

// The built-in style sheet: the defaults of each element, under the game's sheets.
constexpr const char* kBaseStyle = R"(
ui { width: 100%; height: 100%; }
row { flex-direction: row; align-items: center; }
scroll { overflow: scroll; }
button { flex-direction: row; justify-content: center; align-items: center; text-align: center; padding: 4 12; min-height: 34; }
checkbox { flex-direction: row; align-items: center; min-height: 26; padding: 0 0 0 30; }
slider { min-width: 120; height: 22; }
bar { min-width: 60; height: 10; background: rgba(255, 255, 255, 0.12); }
input { min-width: 160; min-height: 30; padding: 4 8; background: rgba(0, 0, 0, 0.45); border: 1 rgba(255, 255, 255, 0.3); }
select { flex-direction: row; align-items: center; min-height: 30; padding: 4 28; text-align: center; }
tooltip { position: absolute; padding: 6 10; background: rgba(10, 10, 14, 0.95); border-radius: 6; font-size: 14; max-width: 320; }
)";

}  // namespace

struct UiSystem {
    Application& app;
    Assets& assets;
    Localization& localization;
    std::string default_font_path;
    Asset<Font> default_font;
    std::map<std::string, Asset<Font>, std::less<>> fonts;
    std::map<std::string, Asset<Texture>, std::less<>> images;
    std::map<std::string, const Texture*, std::less<>> named_textures;  // the game's (thumbnails)
    mutable std::set<std::string, std::less<>> unknown_messages;         // `{id}` of no message
    std::vector<std::unique_ptr<DocumentImpl>> documents;  // by layer, then opening order
    ui::StyleSheet base_sheet;
    UiModel model;
    float scale = 1.0f;
    std::vector<UiEvent> events;
    Ui::Stats stats;
    Texture white;
    std::uint64_t frame = 0;
    double time = 0;
    bool text_input = false;

    UiSystem(Application& a, std::string font)
        : app(a), assets(a.assets()), localization(a.localization()), default_font_path(std::move(font)) {
        base_sheet = ui::parse_style_sheet(kBaseStyle, "<base>");
        Image image;
        image.width = 1;
        image.height = 1;
        image.pixels = {255, 255, 255, 255};
        white = a.renderer().create_texture(image, "ui.white");
        assets.add_file_listener([this](const std::string& key) { file_changed(key); });
    }

    // --- Loading ---------------------------------------------------------------------------------

    std::string read_text(const std::string& key) {
        const FileData file = read_file(assets.root() + key);
        return std::string(static_cast<const char*>(file.data()), file.size());
    }

    std::unique_ptr<UiNode> build(const ui::MarkupNode& m, UiNode* parent, DocumentImpl& doc, const std::string& prefix,
                                  int index) {
        auto node = std::make_unique<UiNode>();
        node->tag = m.tag;
        node->parent = parent;
        node->attributes = m.attributes;
        node->id = m.attribute("id");
        node->classes = split_words(m.attribute("class"));
        node->item_prefix = prefix;
        node->repeat_index = index;
        if (m.has("style")) {
            try {
                node->inline_style = ui::parse_declarations(m.attribute("style"), m.line);
            } catch (const ui::StyleError& e) {
                doc.problems.push_back(doc.path + ": " + e.what());
            }
        }
        node->text_template = m.attribute("text");
        node->bind = m.attribute("bind");
        if (!prefix.empty() && node->bind.rfind("item.", 0) == 0) {
            node->bind = prefix + node->bind.substr(5);
        }
        node->min = std::atof(m.attribute("min", "0").c_str());
        node->max = std::atof(m.attribute("max", "1").c_str());
        node->step = std::atof(m.attribute("step", "0.1").c_str());
        node->max_length = std::atoi(m.attribute("maxlength", "256").c_str());
        // Options separated by '|' ("{a}|{b}|60"), or a list of the model (`source="displays"`:
        // displays.0.name...), read when drawn.
        {
            const std::string options = m.attribute("options");
            std::string current;
            for (const char c : options) {
                if (c == '|') {
                    node->options.push_back(current);
                    current.clear();
                } else {
                    current.push_back(c);
                }
            }
            if (!current.empty()) node->options.push_back(current);
        }
        node->focusable = is_control_tag(node->tag) || m.has("action") || m.attribute("focusable") == "true";
        if (m.attribute("enabled") == "false") node->enabled = false;
        if (m.attribute("visible") == "false") node->visible = false;
        for (const ui::MarkupNode& child : m.children) {
            if (child.tag.empty()) {
                node->text_template += (node->text_template.empty() ? "" : " ") + child.text;
            } else if (child.tag == "style") {
                if (child.has("src")) doc.sheet_paths.push_back(child.attribute("src"));
            } else if (child.tag == "template") {
                const std::string id = child.attribute("id");
                if (!id.empty() && !child.children.empty()) {
                    // The template's single element (the first one).
                    for (const ui::MarkupNode& t : child.children) {
                        if (!t.tag.empty()) {
                            doc.templates[id] = std::make_shared<ui::MarkupNode>(t);
                            break;
                        }
                    }
                }
            } else if (child.tag == "repeat") {
                auto repeat = std::make_unique<UiNode>();
                repeat->tag = "repeat";
                repeat->parent = node.get();
                repeat->attributes = child.attributes;
                repeat->id = child.attribute("id");
                repeat->classes = split_words(child.attribute("class"));
                repeat->repeat_markup = std::make_shared<ui::MarkupNode>(child);
                node->children.push_back(std::move(repeat));
            } else {
                node->children.push_back(build(child, node.get(), doc, prefix, index));
            }
        }
        return node;
    }

    void load(DocumentImpl& doc) {
        doc.problems.clear();
        doc.sheet_paths.clear();
        doc.templates.clear();
        doc.instances.clear();
        const std::string focus_id = doc.focus != nullptr ? doc.focus->id : std::string();
        doc.focus = nullptr;
        doc.hover = nullptr;
        doc.pressed = nullptr;
        doc.tooltip_target = nullptr;
        const ui::MarkupNode markup = ui::parse_markup(read_text(doc.path));  // throws
        if (markup.tag != "ui") {
            throw std::runtime_error(doc.path + ": the root element must be <ui>");
        }
        doc.modal = markup.attribute("modal", "true") != "false";
        doc.back_action = markup.attribute("back-action");
        doc.root = build(markup, nullptr, doc, {}, -1);
        doc.root->document = &doc;  // a reload makes a new root: the elements find their document by it
        doc.sheets.clear();
        for (const std::string& sheet : doc.sheet_paths) {
            try {
                doc.sheets.push_back(ui::parse_style_sheet(read_text(sheet), sheet));
            } catch (const std::exception& e) {
                doc.problems.push_back(sheet + ": " + e.what());
            }
        }
        for (const std::string& p : doc.problems) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ui: %s", p.c_str());
        }
        doc.restyle = doc.relayout = true;
        doc.model_seen = 0;
        doc.language_seen = 0;
        if (!focus_id.empty()) {
            doc.focus = find(doc.root.get(), focus_id);
        }
    }

    void file_changed(const std::string& key) {
        for (auto& doc : documents) {
            bool mine = doc->path == key;
            for (const std::string& sheet : doc->sheet_paths) {
                mine = mine || sheet == key;
            }
            if (mine) {
                try {
                    load(*doc);
                    SDL_Log("Ui: '%s' reloaded", doc->path.c_str());
                } catch (const std::exception& e) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ui: '%s' not reloaded: %s", doc->path.c_str(), e.what());
                }
            }
        }
    }

    static UiNode* find(UiNode* n, std::string_view id) {
        if (n == nullptr) return nullptr;
        if (n->id == id) return n;
        for (auto& c : n->children) {
            if (UiNode* f = find(c.get(), id)) return f;
        }
        return nullptr;
    }

    // --- Texts and bindings ----------------------------------------------------------------------

    std::string value_text(const UiValue& v) const {
        if (const auto* s = std::get_if<std::string>(&v)) return *s;
        if (const auto* b = std::get_if<bool>(&v)) return localization.format(*b ? "common-yes" : "common-no");
        const double d = std::get<double>(v);
        NumberFormatOptions options;
        options.max_fraction_digits = d == std::floor(d) ? 0 : 2;
        return localization.format_number(d, options);
    }

    std::string path_of(const UiNode& n, std::string_view path) const {
        if (path.rfind("item.", 0) == 0 && !n.item_prefix.empty()) {
            return n.item_prefix + std::string(path.substr(5));
        }
        if (path == "item.index") {
            return {};
        }
        return std::string(path);
    }

    // "{id}" a message, "{$path}" a value of the model, "{{" a brace.
    std::string resolve(const UiNode& n, std::string_view t) const {
        std::string out;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '{' && i + 1 < t.size() && t[i + 1] == '{') {
                out.push_back('{');
                ++i;
                continue;
            }
            if (t[i] == '}' && i + 1 < t.size() && t[i + 1] == '}') {
                out.push_back('}');
                ++i;
                continue;
            }
            if (t[i] != '{') {
                out.push_back(t[i]);
                continue;
            }
            const std::size_t close = t.find('}', i);
            if (close == std::string_view::npos) {
                out.append(t.substr(i));
                break;
            }
            const std::string_view inside = t.substr(i + 1, close - i - 1);
            if (!inside.empty() && inside[0] == '$') {
                if (inside == "$item.index") {
                    out += std::to_string(n.repeat_index);
                } else {
                    out += value_text(model.get(path_of(n, inside.substr(1))));
                }
            } else {
                if (!localization.has(inside)) {
                    unknown_messages.emplace(inside);
                }
                out += localization.format(inside);
            }
            i = close;
        }
        return out;
    }

    // A bound attribute ("{$path}" or "true"/"false").
    bool bound_flag(const UiNode& n, std::string_view attribute, bool fallback) const {
        const std::string v = n.attribute(attribute);
        if (v.empty()) return fallback;
        if (v.size() > 3 && v[0] == '{' && v[1] == '$' && v.back() == '}') {
            return model.flag(path_of(n, std::string_view(v).substr(2, v.size() - 3)), fallback);
        }
        return v != "false";
    }
    double bound_number(const UiNode& n, std::string_view attribute, double fallback) const {
        const std::string v = n.attribute(attribute);
        if (v.empty()) return fallback;
        if (v.size() > 3 && v[0] == '{' && v[1] == '$' && v.back() == '}') {
            return model.number(path_of(n, std::string_view(v).substr(2, v.size() - 3)), fallback);
        }
        return std::atof(v.c_str());
    }

    // Repeats expanded to their item count, texts and bound states read again.
    void refresh(DocumentImpl& doc, UiNode& n) {
        if (n.repeat_markup) {
            const std::string source = n.attribute("source");
            const int count = static_cast<int>(model.number(path_of(n, source + ".count"), 0));
            if (count != n.repeat_count) {
                n.children.clear();
                for (int i = 0; i < count; ++i) {
                    const std::string prefix = path_of(n, source) + "." + std::to_string(i) + ".";
                    for (const ui::MarkupNode& child : n.repeat_markup->children) {
                        if (!child.tag.empty()) {
                            n.children.push_back(build(child, &n, doc, prefix, i));
                        }
                    }
                }
                n.repeat_count = count;
                if (doc.focus != nullptr && !contains(doc.root.get(), doc.focus)) {
                    doc.focus = nullptr;
                }
                doc.restyle = true;
            }
        }
        std::string text = resolve(n, n.text_template);
        if (n.tag == "input" && !n.bind.empty()) {
            text = model.text(n.bind);
        }
        if (text != n.text) {
            n.text = std::move(text);
            n.caret = std::min(n.caret, n.text.size());
            doc.relayout = true;
        }
        const bool visible = n.has_attribute("visible") ? bound_flag(n, "visible", true) : n.visible;
        const bool enabled = n.has_attribute("enabled") ? bound_flag(n, "enabled", true) : n.enabled;
        if (visible != n.visible || enabled != n.enabled) {
            n.visible = visible;
            n.enabled = enabled;
            doc.restyle = true;
        }
        for (auto& c : n.children) {
            refresh(doc, *c);
        }
    }

    static bool contains(const UiNode* root, const UiNode* n) {
        if (root == n) return true;
        for (const auto& c : root->children) {
            if (contains(c.get(), n)) return true;
        }
        return false;
    }

    // --- Styles ----------------------------------------------------------------------------------

    void restyle(DocumentImpl& doc, UiNode& n, const ui::MatchNode* parent_match, const UiNode* parent,
                 std::size_t index, std::size_t siblings) {
        unsigned states = 0;
        if (doc.hover == &n) states |= ui::kHover;
        if (doc.focus == &n) states |= ui::kFocus;
        if (doc.pressed == &n) states |= ui::kActive;
        if (!n.enabled) states |= ui::kDisabled;
        if ((n.tag == "checkbox" && !n.bind.empty() && model.flag(n.bind)) || bound_flag(n, "checked", false)) {
            states |= ui::kChecked;
        }
        ui::MatchNode match;
        match.type = n.tag;
        match.id = n.id;
        match.classes = &n.classes;
        match.states = states;
        match.parent = parent_match;
        match.first_child = index == 0;
        match.last_child = index + 1 == siblings;

        struct Hit {
            int specificity;
            int order;
            const ui::Declaration* declaration;
        };
        std::vector<Hit> hits;
        int order = 0;
        const auto collect = [&](const ui::StyleSheet& sheet) {
            for (const ui::Rule& rule : sheet.rules) {
                int best = -1;
                for (const ui::Selector& s : rule.selectors) {
                    if (ui::matches(s, match)) best = std::max(best, s.specificity);
                }
                ++order;
                if (best >= 0) {
                    for (const ui::Declaration& d : rule.declarations) {
                        hits.push_back({best, order, &d});
                    }
                }
            }
        };
        collect(base_sheet);
        for (const ui::StyleSheet& sheet : doc.sheets) {
            collect(sheet);
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
            return a.specificity != b.specificity ? a.specificity < b.specificity : a.order < b.order;
        });
        std::vector<const ui::Declaration*> declarations;
        declarations.reserve(hits.size() + n.inline_style.size());
        for (const Hit& h : hits) {
            declarations.push_back(h.declaration);
        }
        for (const ui::Declaration& d : n.inline_style) {
            declarations.push_back(&d);
        }
        n.variables = parent != nullptr ? parent->variables : std::map<std::string, std::string, std::less<>>{};
        std::vector<std::string> warnings;
        const ui::ComputedStyle before = n.style;
        n.style = ui::compute_style(parent != nullptr ? &parent->style : nullptr, declarations, n.variables, &warnings);
        if (!n.visible || n.is_template) {
            n.style.display = ui::ComputedStyle::Display::None;
        }
        for (const std::string& w : warnings) {
            const std::string line = doc.path + " <" + n.tag + (n.id.empty() ? "" : " id=" + n.id) + ">: " + w;
            if (std::find(doc.problems.begin(), doc.problems.end(), line) == doc.problems.end()) {
                doc.problems.push_back(line);
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ui: %s", line.c_str());
            }
        }
        start_transitions(n, before);
        for (std::size_t i = 0; i < n.children.size(); ++i) {
            restyle(doc, *n.children[i], &match, &n, i, n.children.size());
        }
    }

    // A changed animatable property starts its transition from the value shown now.
    static void start_transitions(UiNode& n, const ui::ComputedStyle& before) {
        const auto target = [](Anim& a, glm::vec4 to, const ui::Transition* t, bool first) {
            if (first || t == nullptr || t->duration <= 0.0f) {
                a.value = a.from = a.to = to;
                a.running = false;
                return;
            }
            if (to == a.to) return;
            a.from = a.value;
            a.to = to;
            a.time = 0;
            a.duration = t->duration;
            a.delay = t->delay;
            a.easing = t->easing;
            a.running = true;
        };
        const auto find = [&](std::string_view property) -> const ui::Transition* {
            for (const ui::Transition& t : n.style.transitions) {
                if (t.property == property || t.property == "all") return &t;
            }
            return nullptr;
        };
        (void)before;
        const bool first = !n.shown;
        target(n.opacity, glm::vec4(n.style.opacity), find("opacity"), first);
        target(n.background, n.style.background, find("background"), first);
        target(n.color, n.style.color, find("color"), first);
        target(n.border, n.style.border_color, find("border-color"), first);
        target(n.fill, n.style.fill_color, find("fill-color"), first);
        target(n.translate, glm::vec4(n.style.translate, 0.0f, 0.0f), find("translate"), first);
        target(n.scale, glm::vec4(n.style.scale), find("scale"), first);
        n.shown = true;
    }

    static bool advance(Anim& a, float dt) {
        if (!a.running) return false;
        a.time += dt;
        const float t = a.duration > 0.0f ? (a.time - a.delay) / a.duration : 1.0f;
        if (t >= 1.0f) {
            a.value = a.to;
            a.running = false;
        } else if (t > 0.0f) {
            a.value = glm::mix(a.from, a.to, ui::ease(a.easing, t));
        }
        return true;
    }

    bool animate(UiNode& n, float dt) {
        bool any = false;
        for (Anim* a : {&n.opacity, &n.background, &n.color, &n.border, &n.fill, &n.translate, &n.scale}) {
            any |= advance(*a, dt);
        }
        for (auto& c : n.children) {
            any |= animate(*c, dt);
        }
        return any;
    }

    // --- Layout ----------------------------------------------------------------------------------

    float px(float points) const { return points * scale; }

    const Font* font_of(const ui::ComputedStyle& s) {
        const std::string& path = s.font_family.empty() ? default_font_path : s.font_family;
        auto it = fonts.find(path);
        if (it == fonts.end()) {
            try {
                it = fonts.emplace(path, assets.font(path)).first;
            } catch (const std::exception& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ui: font '%s': %s", path.c_str(), e.what());
                if (path != default_font_path) {
                    return font_of(ui::ComputedStyle{});
                }
                return nullptr;
            }
        }
        return &*it->second;
    }

    TextStyle text_style(const UiNode& n, float max_width) {
        const ui::ComputedStyle& s = n.style;
        TextStyle t;
        t.size = px(s.font_size);
        t.weight = s.font_weight;
        t.italic = s.italic;
        t.max_width = max_width;
        t.line_spacing = s.line_spacing;
        t.language = localization.language();
        switch (s.text_align) {
            case ui::ComputedStyle::TextAlign::Start: t.align = TextAlign::Start; break;
            case ui::ComputedStyle::TextAlign::End: t.align = TextAlign::End; break;
            case ui::ComputedStyle::TextAlign::Left: t.align = TextAlign::Left; break;
            case ui::ComputedStyle::TextAlign::Center: t.align = TextAlign::Center; break;
            case ui::ComputedStyle::TextAlign::Right: t.align = TextAlign::Right; break;
        }
        return t;
    }

    // What the node's own text (or a control's) takes.
    glm::vec2 own_content(UiNode& n, float inner_width) {
        std::string shown = n.text;
        if (n.tag == "select") {
            shown = select_text(n);
        } else if (n.tag == "input" && shown.empty()) {
            shown = resolve(n, n.attribute("placeholder"));
        }
        if (shown.empty()) {
            if (n.tag == "input" || n.tag == "select") {
                const Font* font = font_of(n.style);
                return glm::vec2(0.0f, font != nullptr ? font->line_height(px(n.style.font_size)) : 0.0f);
            }
            return glm::vec2(0.0f);
        }
        const Font* font = font_of(n.style);
        if (font == nullptr) return glm::vec2(0.0f);
        return font->measure(shown, text_style(n, inner_width > 0 && std::isfinite(inner_width) ? inner_width : 0.0f));
    }

    // A select's options: its own, or the `.name` of the items of its `source` list.
    std::vector<std::string> select_options(const UiNode& n) const {
        if (!n.has_attribute("source")) return n.options;
        const std::string source = path_of(n, n.attribute("source"));
        const int count = static_cast<int>(model.number(source + ".count", 0));
        std::vector<std::string> out;
        out.reserve(static_cast<std::size_t>(std::max(count, 0)));
        for (int i = 0; i < count; ++i) {
            out.push_back("{$" + source + "." + std::to_string(i) + ".name}");
        }
        return out;
    }

    std::string select_text(const UiNode& n) const {
        const std::vector<std::string> options = select_options(n);
        if (options.empty()) return n.text;
        const auto index = static_cast<std::size_t>(std::clamp(model.number(n.bind, 0), 0.0, static_cast<double>(options.size() - 1)));
        return resolve(n, options[index]);
    }

    static bool in_flow(const UiNode& c) {
        return c.style.display != ui::ComputedStyle::Display::None && c.style.position != ui::ComputedStyle::Position::Absolute;
    }

    float edge(const ui::ComputedStyle& s, int side) const { return px(s.padding[side]) + px(s.border_width); }

    float clamp_size(float v, const ui::Length& lo, const ui::Length& hi, float basis) const {
        if (!lo.is_auto()) v = std::max(v, lo.unit == ui::Length::Unit::Points ? px(lo.value) : lo.resolve(basis));
        if (!hi.is_auto()) v = std::min(v, hi.unit == ui::Length::Unit::Points ? px(hi.value) : hi.resolve(basis));
        return v;
    }
    std::optional<float> fixed(const ui::Length& l, float basis) const {
        if (l.unit == ui::Length::Unit::Points) return px(l.value);
        if (l.unit == ui::Length::Unit::Percent && std::isfinite(basis)) return l.resolve(basis);
        return std::nullopt;
    }

    // The border-box size the node wants, given the width it may use (infinite: unconstrained).
    glm::vec2 measure(UiNode& n, float avail_w, float avail_h) {
        const ui::ComputedStyle& s = n.style;
        if (s.display == ui::ComputedStyle::Display::None) return glm::vec2(0.0f);
        const float pad_x = edge(s, 1) + edge(s, 3);
        const float pad_y = edge(s, 0) + edge(s, 2);
        const std::optional<float> fw = fixed(s.width, avail_w);
        const std::optional<float> fh = fixed(s.height, avail_h);
        float width_limit = fw ? *fw : avail_w;
        if (!s.max_width.is_auto()) width_limit = std::min(width_limit, clamp_size(width_limit, s.min_width, s.max_width, avail_w));
        const float inner_w = width_limit - pad_x;
        glm::vec2 content = own_content(n, inner_w);
        // Children.
        float main = 0, cross = 0;
        int count = 0;
        float line_main = 0, line_cross = 0, lines_cross = 0, widest_line = 0;
        // A row on one line whose width is known: its children's widths once the room is shared,
        // their heights measured at those widths (a text that wraps makes the row taller).
        std::vector<float> widths;
        if (s.row && !s.wrap && std::isfinite(inner_w)) widths = row_widths(n, inner_w);
        std::size_t index = 0;
        for (auto& cp : n.children) {
            UiNode& c = *cp;
            if (!in_flow(c)) continue;
            glm::vec2 m;
            if (!widths.empty()) {
                const float w = widths[index++];
                m = glm::vec2(w, measure_at_width(c, w).y);
            } else {
                m = measure(c, s.row ? std::numeric_limits<float>::infinity() : inner_w, std::numeric_limits<float>::infinity());
            }
            const float mx = margin_px(c, 1) + margin_px(c, 3);
            const float my = margin_px(c, 0) + margin_px(c, 2);
            const float child_main = s.row ? m.x + mx : m.y + my;
            const float child_cross = s.row ? m.y + my : m.x + mx;
            if (s.row && s.wrap && count > 0 && std::isfinite(inner_w) && line_main + px(s.gap) + child_main > inner_w) {
                widest_line = std::max(widest_line, line_main);
                lines_cross += line_cross + px(s.gap);
                line_main = 0;
                line_cross = 0;
                count = 0;
            }
            line_main += (count > 0 ? px(s.gap) : 0.0f) + child_main;
            line_cross = std::max(line_cross, child_cross);
            ++count;
        }
        widest_line = std::max(widest_line, line_main);
        main = widest_line;
        cross = lines_cross + line_cross;
        glm::vec2 children = s.row ? glm::vec2(main, cross) : glm::vec2(cross, main);
        if (n.tag == "checkbox" || n.tag == "button" || n.tag == "select" || n.tag == "input") {
            // Own text beside or with the children.
            if (s.row) children = glm::vec2(children.x + content.x + (children.x > 0 && content.x > 0 ? px(s.gap) : 0.0f), std::max(children.y, content.y));
            else children = glm::vec2(std::max(children.x, content.x), children.y + content.y);
        } else if (content.x > 0 || content.y > 0) {
            children = glm::vec2(std::max(children.x, content.x), children.y + content.y);
        }
        float w = fw ? *fw : children.x + pad_x;
        float h = fh ? *fh : children.y + pad_y;
        w = clamp_size(w, s.min_width, s.max_width, avail_w);
        h = clamp_size(h, s.min_height, s.max_height, avail_h);
        return glm::vec2(w, h);
    }

    // The width an item of a row starts from: its basis, else its width, else its content's.
    float base_width(UiNode& c) {
        const ui::ComputedStyle& cs = c.style;
        if (!cs.basis.is_auto() && cs.basis.unit == ui::Length::Unit::Points) return px(cs.basis.value);
        return measure(c, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()).x;
    }

    // The widths of the in-flow children of a one-line row `inner_w` wide: their bases, grown into
    // the free room or shrunk to fit (not in a scroll area), within their min and max widths.
    std::vector<float> row_widths(UiNode& n, float inner_w) {
        const ui::ComputedStyle& s = n.style;
        std::vector<UiNode*> items;
        std::vector<float> widths;
        float used = 0, grow = 0, shrink = 0;
        for (auto& cp : n.children) {
            UiNode& c = *cp;
            if (!in_flow(c)) continue;
            const float w = base_width(c);
            items.push_back(&c);
            widths.push_back(w);
            used += w + margin_px(c, 1) + margin_px(c, 3);
            grow += c.style.grow;
            shrink += c.style.shrink * w;
        }
        if (items.empty()) return widths;
        used += px(s.gap) * static_cast<float>(items.size() - 1);
        const float free = inner_w - used;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const ui::ComputedStyle& cs = items[i]->style;
            if (free > 0 && grow > 0) widths[i] += free * cs.grow / grow;
            else if (free < 0 && shrink > 0 && s.overflow != ui::ComputedStyle::Overflow::Scroll) {
                widths[i] = std::max(0.0f, widths[i] + free * cs.shrink * widths[i] / shrink);
            }
            widths[i] = clamp_size(widths[i], cs.min_width, cs.max_width, inner_w);
        }
        return widths;
    }

    // The size of `c` given its final width `w` (its text wrapped to it, its children laid in it).
    glm::vec2 measure_at_width(UiNode& c, float w) {
        const ui::Length saved = c.style.width;
        c.style.width = ui::Length{ui::Length::Unit::Points, w / scale};
        const glm::vec2 m = measure(c, w, std::numeric_limits<float>::infinity());
        c.style.width = saved;
        return glm::vec2(w, m.y);
    }

    float margin_px(const UiNode& c, int side) const {
        const ui::Length& m = c.style.margin[side];
        return m.unit == ui::Length::Unit::Points ? px(m.value) : 0.0f;
    }

    void arrange(UiNode& n, glm::vec2 pos, glm::vec2 size) {
        const ui::ComputedStyle& s = n.style;
        n.pos = pos;
        n.size = size;
        if (s.display == ui::ComputedStyle::Display::None) return;
        const glm::vec2 inner_pos = pos + glm::vec2(edge(s, 3), edge(s, 0));
        const glm::vec2 inner = glm::max(size - glm::vec2(edge(s, 1) + edge(s, 3), edge(s, 0) + edge(s, 2)), glm::vec2(0.0f));
        // The own text of a container that has children takes the top of the content (a label
        // before them); controls put theirs in the middle.
        float own_offset = 0;
        if (!n.children.empty() && !n.text.empty() && !is_control_tag(n.tag)) {
            own_offset = own_content(n, inner.x).y;
        }
        const float main_size = s.row ? inner.x : inner.y - own_offset;
        const float cross_size = s.row ? inner.y : inner.x;
        const glm::vec2 origin = inner_pos + glm::vec2(0.0f, own_offset);

        struct Item {
            UiNode* node;
            float main, cross, margin_main0, margin_main1, margin_cross0, margin_cross1;
            bool auto_main0, auto_main1;
        };
        std::vector<std::vector<Item>> lines(1);
        float line_used = 0;
        for (auto& cp : n.children) {
            UiNode& c = *cp;
            if (c.style.display == ui::ComputedStyle::Display::None) {
                c.size = glm::vec2(0.0f);
                continue;
            }
            if (c.style.position == ui::ComputedStyle::Position::Absolute) {
                place_absolute(c, inner_pos, inner);
                continue;
            }
            const glm::vec2 m = measure(c, s.row ? std::numeric_limits<float>::infinity() : cross_size - margin_px(c, 1) - margin_px(c, 3),
                                        s.row ? cross_size : std::numeric_limits<float>::infinity());
            Item item{&c, s.row ? m.x : m.y, s.row ? m.y : m.x, 0, 0, 0, 0, false, false};
            if (s.row && !c.style.basis.is_auto() && c.style.basis.unit == ui::Length::Unit::Points) item.main = px(c.style.basis.value);
            const int m0 = s.row ? 3 : 0, m1 = s.row ? 1 : 2, c0 = s.row ? 0 : 3, c1 = s.row ? 2 : 1;
            item.margin_main0 = margin_px(c, m0);
            item.margin_main1 = margin_px(c, m1);
            item.margin_cross0 = margin_px(c, c0);
            item.margin_cross1 = margin_px(c, c1);
            item.auto_main0 = c.style.margin[m0].is_auto();
            item.auto_main1 = c.style.margin[m1].is_auto();
            const float total = item.main + item.margin_main0 + item.margin_main1;
            if (s.wrap && !lines.back().empty() && line_used + px(s.gap) + total > main_size) {
                lines.emplace_back();
                line_used = 0;
            }
            line_used += (lines.back().empty() ? 0.0f : px(s.gap)) + total;
            lines.back().push_back(item);
        }
        float cross_offset = 0;
        float content_end = 0;
        for (std::vector<Item>& line : lines) {
            if (line.empty()) continue;
            float used = 0, grow = 0, shrink = 0;
            float line_cross = 0;
            for (const Item& it : line) {
                used += it.main + it.margin_main0 + it.margin_main1;
                grow += it.node->style.grow;
                shrink += it.node->style.shrink * it.main;
                line_cross = std::max(line_cross, it.cross + it.margin_cross0 + it.margin_cross1);
            }
            used += px(s.gap) * static_cast<float>(line.size() - 1);
            float free = main_size - used;
            const bool scrolls = s.overflow == ui::ComputedStyle::Overflow::Scroll;
            if (free > 0 && grow > 0) {
                for (Item& it : line) it.main += free * it.node->style.grow / grow;
                free = 0;
            } else if (free < 0 && shrink > 0 && !scrolls) {
                for (Item& it : line) it.main = std::max(0.0f, it.main + free * it.node->style.shrink * it.main / shrink);
                free = 0;
            }
            for (Item& it : line) {
                const ui::ComputedStyle& cs = it.node->style;
                it.main = clamp_size(it.main, s.row ? cs.min_width : cs.min_height, s.row ? cs.max_width : cs.max_height, main_size);
            }
            if (s.row) {
                // Their heights at their final widths (a text wrapped in a narrower item).
                line_cross = 0;
                for (Item& it : line) {
                    if (it.node->style.height.is_auto()) it.cross = measure_at_width(*it.node, it.main).y;
                    line_cross = std::max(line_cross, it.cross + it.margin_cross0 + it.margin_cross1);
                }
            }
            // Auto margins take the free space first.
            int autos = 0;
            for (const Item& it : line) autos += (it.auto_main0 ? 1 : 0) + (it.auto_main1 ? 1 : 0);
            float start = 0, between = px(s.gap);
            if (free > 0 && autos > 0) {
                const float share = free / static_cast<float>(autos);
                for (Item& it : line) {
                    if (it.auto_main0) it.margin_main0 += share;
                    if (it.auto_main1) it.margin_main1 += share;
                }
                free = 0;
            }
            if (free > 0) {
                const auto k = static_cast<float>(line.size());
                switch (s.justify) {
                    case ui::ComputedStyle::Justify::Center: start = free * 0.5f; break;
                    case ui::ComputedStyle::Justify::End: start = free; break;
                    case ui::ComputedStyle::Justify::SpaceBetween: between += line.size() > 1 ? free / (k - 1.0f) : 0.0f; break;
                    case ui::ComputedStyle::Justify::SpaceAround: between += free / k; start = free / k * 0.5f; break;
                    case ui::ComputedStyle::Justify::SpaceEvenly: between += free / (k + 1.0f); start = free / (k + 1.0f); break;
                    case ui::ComputedStyle::Justify::Start: break;
                }
            }
            const float this_cross = lines.size() == 1 ? cross_size : line_cross;
            float cursor = start;
            for (Item& it : line) {
                UiNode& c = *it.node;
                const ui::ComputedStyle& cs = c.style;
                ui::ComputedStyle::Align align = cs.align_self == ui::ComputedStyle::Align::Auto ? s.align_items : cs.align_self;
                const ui::Length& cross_length = s.row ? cs.height : cs.width;
                float cross = it.cross;
                const float room = this_cross - it.margin_cross0 - it.margin_cross1;
                if (align == ui::ComputedStyle::Align::Stretch && cross_length.is_auto()) {
                    cross = clamp_size(room, s.row ? cs.min_height : cs.min_width, s.row ? cs.max_height : cs.max_width, cross_size);
                }
                float cross_pos = it.margin_cross0;
                if (align == ui::ComputedStyle::Align::Center) cross_pos += (room - cross) * 0.5f;
                else if (align == ui::ComputedStyle::Align::End) cross_pos += room - cross;
                cursor += it.margin_main0;
                glm::vec2 child_pos, child_size;
                if (s.row) {
                    child_pos = origin + glm::vec2(cursor, cross_offset + cross_pos);
                    child_size = glm::vec2(it.main, cross);
                } else {
                    child_pos = origin + glm::vec2(cross_offset + cross_pos, cursor);
                    child_size = glm::vec2(cross, it.main);
                }
                if (scrolls) child_pos.y -= n.scroll;
                arrange(c, child_pos, child_size);
                cursor += it.main + it.margin_main1 + between;
            }
            content_end = std::max(content_end, cursor - between);
            cross_offset += line_cross + px(s.gap);
        }
        n.content_extent = s.row ? cross_offset : content_end;
        if (s.overflow == ui::ComputedStyle::Overflow::Scroll) {
            n.scroll = std::clamp(n.scroll, 0.0f, std::max(0.0f, n.content_extent - main_size));
        }
    }

    void place_absolute(UiNode& c, glm::vec2 inner_pos, glm::vec2 inner) {
        const ui::ComputedStyle& cs = c.style;
        glm::vec2 size = measure(c, inner.x, inner.y);
        if (auto w = fixed(cs.width, inner.x)) size.x = *w;
        if (auto h = fixed(cs.height, inner.y)) size.y = *h;
        glm::vec2 pos = inner_pos;
        if (c.screen_position) {
            pos = *c.screen_position;
        } else {
            if (!cs.left.is_auto()) pos.x = inner_pos.x + cs.left.resolve(inner.x) * (cs.left.unit == ui::Length::Unit::Points ? scale : 1.0f);
            else if (!cs.right.is_auto()) pos.x = inner_pos.x + inner.x - size.x - cs.right.resolve(inner.x) * (cs.right.unit == ui::Length::Unit::Points ? scale : 1.0f);
            if (!cs.top.is_auto()) pos.y = inner_pos.y + cs.top.resolve(inner.y) * (cs.top.unit == ui::Length::Unit::Points ? scale : 1.0f);
            else if (!cs.bottom.is_auto()) pos.y = inner_pos.y + inner.y - size.y - cs.bottom.resolve(inner.y) * (cs.bottom.unit == ui::Length::Unit::Points ? scale : 1.0f);
        }
        arrange(c, pos, size);
    }

    void layout(DocumentImpl& doc) {
        arrange(*doc.root, glm::vec2(0.0f), doc.viewport);
        doc.relayout = false;
        ++stats.layouts;
    }

    // --- Drawing ---------------------------------------------------------------------------------

    struct DrawState {
        SpriteRenderer* sprites;
        float depth;
        float opacity;
        std::optional<glm::vec4> clip;
        glm::vec2 offset;  // transitions' translate of the ancestors
    };

    static glm::vec4 faded(glm::vec4 c, float opacity) { return glm::vec4(glm::vec3(c), c.a * opacity); }

    static std::optional<glm::vec4> intersect(const std::optional<glm::vec4>& a, glm::vec4 b) {
        if (!a) return b;
        return glm::vec4(std::max(a->x, b.x), std::max(a->y, b.y), std::min(a->z, b.z), std::min(a->w, b.w));
    }

    void quad(const DrawState& st, glm::vec2 pos, glm::vec2 size, glm::vec4 color) {
        if (color.a <= 0.0f || size.x <= 0.0f || size.y <= 0.0f) return;
        SpriteOptions o;
        o.tint = color;
        o.depth = st.depth;
        o.clip = st.clip;
        st.sprites->draw(white, pos, size, o);
        ++stats.quads;
    }

    const AtlasGlyph* rounded_shape() {
        FontLibrary& library = *assets.font_library();
        const float r = kShapeRadius, pad = kShapePad, core = kShapeCore;
        const float side = 2.0f * (r + pad) + core;
        msdf::Outline o;
        // A rounded square, y up, clockwise in texel space; quarter circles as two quadratics each.
        const float lo = pad, hi = side - pad;
        const auto corner = [&](glm::dvec2 c, double a0) {
            const double step = 3.14159265358979 / 4.0;
            for (int k = 0; k < 2; ++k) {
                const double a = a0 + k * step, b = a + step, mid = (a + b) * 0.5;
                const double rc = r / std::cos(step * 0.5);
                o.quadratic_to(c + glm::dvec2(rc * std::cos(mid), rc * std::sin(mid)), c + glm::dvec2(r * std::cos(b), r * std::sin(b)));
            }
        };
        o.move_to({lo + r, lo});
        o.line_to({hi - r, lo});
        corner({hi - r, lo + r}, -3.14159265358979 / 2.0);
        o.line_to({hi, hi - r});
        corner({hi - r, hi - r}, 0.0);
        o.line_to({lo + r, hi});
        corner({lo + r, hi - r}, 3.14159265358979 / 2.0);
        o.line_to({lo, lo + r});
        corner({lo + r, lo + r}, 3.14159265358979);
        o.close();
        return library.atlas().shape(kRoundedShape, o, glm::ivec2(static_cast<int>(side)), library.frame());
    }

    // A rounded rectangle: nine pieces of the atlas' rounded square, drawn by the glyph shader
    // (fill, then the border inside the box as its outline).
    void rounded(const DrawState& st, glm::vec2 pos, glm::vec2 size, float radius, glm::vec4 fill, glm::vec4 border, float border_width) {
        const AtlasGlyph* shape = rounded_shape();
        radius = std::min(radius, std::min(size.x, size.y) * 0.5f);
        if (shape == nullptr || shape->page == nullptr || radius < 0.5f) {
            if (border_width > 0.0f && border.a > 0.0f) {
                quad(st, pos, size, border);
                quad(st, pos + border_width, size - 2.0f * border_width, fill);
            } else {
                quad(st, pos, size, fill);
            }
            return;
        }
        // Texel space of the shape: corner pieces (pad + radius) map to `pad * k + radius` pixels.
        const float k = radius / kShapeRadius;
        const float corner_tex = kShapePad + kShapeRadius;
        const float corner_px = corner_tex * k;
        const float side_tex = 2.0f * corner_tex + kShapeCore;
        const glm::vec4 uv = shape->uv;
        const auto u_at = [&](float t) { return uv.x + (uv.z - uv.x) * t / side_tex; };
        const auto v_at = [&](float t) { return uv.y + (uv.w - uv.y) * t / side_tex; };
        const float xs[4] = {pos.x - kShapePad * k, pos.x - kShapePad * k + corner_px, pos.x + size.x + kShapePad * k - corner_px, pos.x + size.x + kShapePad * k};
        const float ys[4] = {pos.y - kShapePad * k, pos.y - kShapePad * k + corner_px, pos.y + size.y + kShapePad * k - corner_px, pos.y + size.y + kShapePad * k};
        const float us[4] = {u_at(0), u_at(corner_tex), u_at(corner_tex + kShapeCore), u_at(side_tex)};
        const float vs[4] = {v_at(0), v_at(corner_tex), v_at(corner_tex + kShapeCore), v_at(side_tex)};
        GlyphLook look;
        look.color = fill;
        const float reach = kShapePad * 0.5f * 0.95f * k;  // the outline stays in the field's range
        const float width = std::min(border_width, reach * 2.0f);
        look.outline_color = border_width > 0.0f ? border : glm::vec4(0.0f);
        look.dilate = -width;
        look.outline = width;
        look.softness = 1.0f;
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) {
                const glm::vec2 p(xs[i], ys[j]);
                const glm::vec2 s(xs[i + 1] - xs[i], ys[j + 1] - ys[j]);
                if (s.x <= 0.0f || s.y <= 0.0f) continue;
                st.sprites->draw_glyph(*shape->page, p, s, glm::vec4(us[i], vs[j], us[i + 1], vs[j + 1]), look, st.depth, 0.0f, 0.0f, st.clip);
                ++stats.quads;
            }
        }
    }

    void box(const DrawState& st, glm::vec2 pos, glm::vec2 size, const UiNode& n, glm::vec4 fill, glm::vec4 border) {
        const ui::ComputedStyle& s = n.style;
        const float radius = px(s.border_radius);
        const float bw = px(s.border_width);
        if (fill.a <= 0.0f && (bw <= 0.0f || border.a <= 0.0f)) return;
        if (radius >= 0.5f) {
            rounded(st, pos, size, radius, fill, border, bw);
        } else if (bw > 0.0f && border.a > 0.0f) {
            quad(st, pos, glm::vec2(size.x, bw), border);
            quad(st, glm::vec2(pos.x, pos.y + size.y - bw), glm::vec2(size.x, bw), border);
            quad(st, glm::vec2(pos.x, pos.y + bw), glm::vec2(bw, size.y - 2 * bw), border);
            quad(st, glm::vec2(pos.x + size.x - bw, pos.y + bw), glm::vec2(bw, size.y - 2 * bw), border);
            quad(st, pos + bw, size - 2.0f * bw, fill);
        } else {
            quad(st, pos, size, fill);
        }
    }

    void image(const DrawState& st, glm::vec2 pos, glm::vec2 size, const UiNode& n, float opacity) {
        const ui::ComputedStyle& s = n.style;
        std::string path = s.background_image;
        if (n.tag == "image" && n.has_attribute("src")) path = resolve(n, n.attribute("src"));
        if (path.empty()) return;
        const Texture* named = nullptr;
        if (const auto found = named_textures.find(path); found != named_textures.end()) {
            named = found->second;
        }
        if (named != nullptr) {
            SpriteOptions o;
            o.tint = faded(s.image_color, opacity);
            o.depth = st.depth;
            o.clip = st.clip;
            st.sprites->draw(*named, pos, size, o);
            ++stats.quads;
            return;
        }
        auto it = images.find(path);
        if (it == images.end()) {
            try {
                it = images.emplace(path, assets.texture(path)).first;
            } catch (const std::exception& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ui: image '%s': %s", path.c_str(), e.what());
                images.emplace(path, Asset<Texture>{});
                return;
            }
        }
        if (!it->second) return;
        const Texture& t = *it->second;
        const glm::vec4 tint = faded(s.image_color, opacity);
        const float slice = s.image_slice;
        SpriteOptions o;
        o.tint = tint;
        o.depth = st.depth;
        o.clip = st.clip;
        if (slice <= 0.0f || t.width <= 0) {
            st.sprites->draw(t, pos, size, o);
            ++stats.quads;
            return;
        }
        const float b = px(slice);
        const float xs[4] = {pos.x, pos.x + b, pos.x + size.x - b, pos.x + size.x};
        const float ys[4] = {pos.y, pos.y + b, pos.y + size.y - b, pos.y + size.y};
        const float us[4] = {0.0f, slice / static_cast<float>(t.width), 1.0f - slice / static_cast<float>(t.width), 1.0f};
        const float vs[4] = {0.0f, slice / static_cast<float>(t.height), 1.0f - slice / static_cast<float>(t.height), 1.0f};
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) {
                o.uv_rect = glm::vec4(us[i], vs[j], us[i + 1], vs[j + 1]);
                const glm::vec2 s2(xs[i + 1] - xs[i], ys[j + 1] - ys[j]);
                if (s2.x > 0.0f && s2.y > 0.0f) {
                    st.sprites->draw(t, glm::vec2(xs[i], ys[j]), s2, o);
                    ++stats.quads;
                }
            }
        }
    }

    void text(const DrawState& st, const UiNode& n, std::string_view shown, glm::vec2 pos, glm::vec2 size, glm::vec4 color,
              bool center_vertically) {
        if (shown.empty()) return;
        const Font* font = font_of(n.style);
        if (font == nullptr) return;
        TextStyle t = text_style(n, size.x);
        t.color = color;
        t.depth = st.depth;
        t.clip = st.clip;
        if (n.style.text_outline_width > 0.0f) {
            t.outline_width = px(n.style.text_outline_width);
            t.outline_color = faded(n.style.text_outline_color, st.opacity);
        }
        if (n.style.text_shadow) {
            t.shadow = TextShadow{n.style.text_shadow_offset * scale, px(n.style.text_shadow_blur), faded(n.style.text_shadow_color, st.opacity)};
        }
        const auto layout = font->layout(shown, t);
        glm::vec2 at = pos;
        if (center_vertically) at.y += std::max(0.0f, (size.y - layout->size.y) * 0.5f);
        font->draw(*st.sprites, *layout, glm::round(at), t);
    }

    void draw(UiNode& n, DrawState st, DocumentImpl& doc) {
        const ui::ComputedStyle& s = n.style;
        if (s.display == ui::ComputedStyle::Display::None || n.size.x < 0.0f) return;
        ++stats.elements;
        st.opacity *= n.opacity.value.x;
        if (st.opacity <= 0.003f) return;
        const glm::vec2 translate = glm::vec2(n.translate.value) * scale + st.offset;
        const float k = n.scale.value.x;
        const glm::vec2 center = n.pos + n.size * 0.5f + translate;
        const glm::vec2 size = n.size * k;
        const glm::vec2 pos = center - size * 0.5f;
        st.offset = translate;

        box(st, pos, size, n, faded(n.background.value, st.opacity), faded(n.border.value, st.opacity));
        image(st, pos, size, n, st.opacity);
        const glm::vec2 inner_pos = pos + glm::vec2(edge(s, 3), edge(s, 0)) * k;
        const glm::vec2 inner = glm::max(size - glm::vec2(edge(s, 1) + edge(s, 3), edge(s, 0) + edge(s, 2)) * k, glm::vec2(0.0f));
        const glm::vec4 color = faded(n.color.value, st.opacity);
        const glm::vec4 fill = faded(n.fill.value, st.opacity);
        const float radius = px(s.border_radius) * k;

        if (n.tag == "bar") {
            const float v = static_cast<float>(std::clamp(bound_number(n, "value", 0.0), 0.0, 1.0));
            if (v > 0.0f) {
                const glm::vec2 filled(size.x * v, size.y);
                if (radius >= 0.5f) {
                    rounded(st, pos, filled, radius, fill, glm::vec4(0.0f), 0.0f);
                } else {
                    quad(st, pos, filled, fill);
                }
            }
        } else if (n.tag == "slider") {
            const double value = model.number(n.bind, n.min);
            const float t = static_cast<float>(n.max > n.min ? std::clamp((value - n.min) / (n.max - n.min), 0.0, 1.0) : 0.0);
            const float track_h = std::max(4.0f * scale, size.y * 0.25f);
            const glm::vec2 track_pos(pos.x, pos.y + (size.y - track_h) * 0.5f);
            rounded(st, track_pos, glm::vec2(size.x, track_h), track_h * 0.5f, faded(glm::vec4(1, 1, 1, 0.15f), st.opacity), glm::vec4(0), 0);
            rounded(st, track_pos, glm::vec2(size.x * t, track_h), track_h * 0.5f, fill, glm::vec4(0), 0);
            const float knob = size.y;
            rounded(st, glm::vec2(pos.x + size.x * t - knob * 0.5f, pos.y), glm::vec2(knob), knob * 0.5f, color, glm::vec4(0), 0);
        } else if (n.tag == "checkbox") {
            const float side = std::min(px(20.0f), size.y);
            const glm::vec2 b(pos.x, pos.y + (size.y - side) * 0.5f);
            rounded(st, b, glm::vec2(side), px(4.0f), faded(glm::vec4(0, 0, 0, 0.35f), st.opacity), color, px(1.5f));
            if (model.flag(n.bind) || bound_flag(n, "checked", false)) {
                rounded(st, b + side * 0.22f, glm::vec2(side * 0.56f), px(2.5f), fill, glm::vec4(0), 0);
            }
        } else if (n.tag == "select") {
            text(st, n, "‹", glm::vec2(pos.x + px(8.0f), inner_pos.y), glm::vec2(px(12.0f), inner.y), color, true);
            text(st, n, "›", glm::vec2(pos.x + size.x - px(18.0f), inner_pos.y), glm::vec2(px(12.0f), inner.y), color, true);
        }

        // The own text.
        std::string shown = n.tag == "select" ? select_text(n) : n.text;
        glm::vec4 text_color = color;
        if (n.tag == "input" && shown.empty() && doc.focus != &n) {
            shown = resolve(n, n.attribute("placeholder"));
            text_color.a *= 0.5f;
        }
        const bool centered = is_control_tag(n.tag) || n.children.empty();
        if (!shown.empty() || n.tag == "input") {
            const std::optional<glm::vec4> clip_before = st.clip;
            if (n.tag == "input") st.clip = intersect(st.clip, glm::vec4(inner_pos, inner_pos + inner));
            text(st, n, shown, inner_pos, inner, text_color, centered && n.children.empty());
            if (n.tag == "input" && doc.focus == &n && std::fmod(time, 1.0) < 0.6) {
                // The caret.
                const Font* font = font_of(s);
                if (font != nullptr) {
                    TextStyle ts = text_style(n, 0.0f);
                    const auto layout = font->layout(n.text, ts);
                    const glm::vec3 c = text_caret(*layout, std::min(n.caret, layout->text.size()));
                    const float top = inner_pos.y + std::max(0.0f, (inner.y - layout->size.y) * 0.5f);
                    quad(st, glm::vec2(inner_pos.x + c.x, top + c.y), glm::vec2(std::max(1.0f, scale), c.z - c.y), color);
                }
            }
            st.clip = clip_before;
        }
        if (s.overflow != ui::ComputedStyle::Overflow::Visible) {
            st.clip = intersect(st.clip, glm::vec4(inner_pos, inner_pos + inner));
        }
        for (auto& c : n.children) {
            draw(*c, st, doc);
        }
        if (s.overflow == ui::ComputedStyle::Overflow::Scroll && n.content_extent > inner.y + 0.5f) {
            // The scroll bar.
            const float track = inner.y;
            const float thumb = std::max(px(20.0f), track * inner.y / n.content_extent);
            const float t = n.scroll / std::max(1.0f, n.content_extent - inner.y);
            DrawState bar = st;
            quad(bar, glm::vec2(pos.x + size.x - px(4.0f), inner_pos.y + (track - thumb) * t), glm::vec2(px(3.0f), thumb),
                 faded(glm::vec4(1, 1, 1, 0.35f), st.opacity));
        }
    }

    // --- Navigation ------------------------------------------------------------------------------

    static bool shown(const UiNode& n) {
        for (const UiNode* p = &n; p != nullptr; p = p->parent) {
            if (p->style.display == ui::ComputedStyle::Display::None) return false;
        }
        return true;
    }

    void focusables(UiNode& n, std::vector<UiNode*>& out) {
        if (n.style.display == ui::ComputedStyle::Display::None || n.is_template) return;
        if (n.focusable && n.enabled) out.push_back(&n);
        for (auto& c : n.children) focusables(*c, out);
    }

    UiNode* hit(UiNode& n, glm::vec2 p) {
        if (n.style.display == ui::ComputedStyle::Display::None) return nullptr;
        const bool inside = p.x >= n.pos.x && p.y >= n.pos.y && p.x < n.pos.x + n.size.x && p.y < n.pos.y + n.size.y;
        if (n.style.overflow != ui::ComputedStyle::Overflow::Visible && !inside) return nullptr;
        for (auto it = n.children.rbegin(); it != n.children.rend(); ++it) {
            if (UiNode* h = hit(**it, p)) return h;
        }
        return inside ? &n : nullptr;
    }

    static UiNode* actionable(UiNode* n) {
        for (; n != nullptr; n = n->parent) {
            if (n->focusable) return n;
        }
        return nullptr;
    }

    // `announce`: a Focus event (the player moved it; not the focus a screen opens with).
    void set_focus(DocumentImpl& doc, UiNode* n, bool announce = true) {
        if (doc.focus == n) return;
        doc.focus = n;
        doc.restyle = true;
        if (n != nullptr) {
            if (announce) emit(UiEvent::Kind::Focus, doc, *n, {});
            if (scroll_into_view(*n)) doc.relayout = true;
            update_text_input(n);
        } else {
            update_text_input(nullptr);
        }
    }

    void update_text_input(UiNode* focused) {
        const bool want = focused != nullptr && focused->tag == "input";
        if (want != text_input) {
            if (want) SDL_StartTextInput(app.window());
            else SDL_StopTextInput(app.window());
            text_input = want;
        }
    }

    // Scrolls the scroll areas around `n` until it shows (the layout must be current). True if one
    // moved.
    bool scroll_into_view(UiNode& n) {
        bool moved = false;
        for (UiNode* p = n.parent; p != nullptr; p = p->parent) {
            if (p->style.overflow == ui::ComputedStyle::Overflow::Scroll) {
                const float before = p->scroll;
                const float top = p->pos.y + edge(p->style, 0);
                const float bottom = p->pos.y + p->size.y - edge(p->style, 2);
                if (n.pos.y < top) p->scroll -= top - n.pos.y;
                else if (n.pos.y + n.size.y > bottom) p->scroll += n.pos.y + n.size.y - bottom;
                p->scroll = std::max(0.0f, p->scroll);
                moved = moved || p->scroll != before;
            }
        }
        return moved;
    }

    UiNode* neighbour(DocumentImpl& doc, glm::vec2 direction) {
        std::vector<UiNode*> all;
        focusables(*doc.root, all);
        if (all.empty()) return nullptr;
        UiNode* from = doc.focus;
        if (from == nullptr) return all.front();
        // An explicit neighbour.
        const char* names[4] = {"nav-up", "nav-down", "nav-left", "nav-right"};
        const int which = direction.y < 0 ? 0 : direction.y > 0 ? 1 : direction.x < 0 ? 2 : 3;
        const std::string explicit_id = from->attribute(names[which]);
        if (!explicit_id.empty()) {
            if (UiNode* n = find(doc.root.get(), explicit_id[0] == '#' ? explicit_id.substr(1) : explicit_id)) return n;
        }
        const glm::vec2 c0 = from->pos + from->size * 0.5f;
        UiNode* best = nullptr;
        float best_score = std::numeric_limits<float>::max();
        for (UiNode* n : all) {
            if (n == from) continue;
            const glm::vec2 c = n->pos + n->size * 0.5f;
            const glm::vec2 d = c - c0;
            const float along = glm::dot(d, direction);
            if (along <= 1.0f) continue;
            const float across = std::abs(direction.x != 0.0f ? d.y : d.x);
            const float score = along + 2.0f * across;
            if (score < best_score) {
                best_score = score;
                best = n;
            }
        }
        if (best == nullptr) {
            // Wrap around in reading order for up and down.
            const auto it = std::find(all.begin(), all.end(), from);
            if (direction.y > 0) return it + 1 < all.end() ? *(it + 1) : all.front();
            if (direction.y < 0) return it != all.begin() ? *(it - 1) : all.back();
        }
        return best;
    }

    void emit(UiEvent::Kind kind, const DocumentImpl& doc, const UiNode& n, UiValue value) {
        UiEvent e;
        e.kind = kind;
        e.action = n.attribute("action");
        e.id = n.id;
        e.document = doc.path;
        for (const UiNode* p = &n; p != nullptr && e.index < 0; p = p->parent) e.index = p->repeat_index;
        e.value = std::move(value);
        events.push_back(std::move(e));
    }

    void activate(DocumentImpl& doc, UiNode& n, std::optional<glm::vec2> pointer) {
        if (!n.enabled) return;
        if (n.tag == "checkbox") {
            const bool v = !model.flag(n.bind);
            if (!n.bind.empty()) model.set(n.bind, v);
            emit(UiEvent::Kind::Change, doc, n, v);
        } else if (n.tag == "select") {
            change_select(doc, n, 1);
        } else if (n.tag == "slider") {
            if (pointer) slide_to(doc, n, pointer->x);
        } else if (n.tag == "input") {
            if (pointer) {
                const Font* font = font_of(n.style);
                if (font != nullptr) {
                    const auto layout = font->layout(n.text, text_style(n, 0.0f));
                    n.caret = text_hit(*layout, *pointer - (n.pos + glm::vec2(edge(n.style, 3), edge(n.style, 0))));
                }
            }
        } else {
            emit(UiEvent::Kind::Activate, doc, n, {});
        }
    }

    void change_select(DocumentImpl& doc, UiNode& n, int step) {
        const int count = static_cast<int>(select_options(n).size());
        if (count == 0) return;
        const int index = (static_cast<int>(model.number(n.bind, 0)) + step % count + count) % count;
        if (!n.bind.empty()) model.set(n.bind, index);
        emit(UiEvent::Kind::Change, doc, n, static_cast<double>(index));
    }

    void change_slider(DocumentImpl& doc, UiNode& n, int step) {
        const double v = std::clamp(model.number(n.bind, n.min) + n.step * step, n.min, n.max);
        const double snapped = n.step > 0 ? n.min + std::round((v - n.min) / n.step) * n.step : v;
        if (!n.bind.empty()) model.set(n.bind, snapped);
        emit(UiEvent::Kind::Change, doc, n, snapped);
    }

    void slide_to(DocumentImpl& doc, UiNode& n, float x) {
        const double t = n.size.x > 0 ? std::clamp(static_cast<double>((x - n.pos.x) / n.size.x), 0.0, 1.0) : 0.0;
        double v = n.min + t * (n.max - n.min);
        if (n.step > 0) v = n.min + std::round((v - n.min) / n.step) * n.step;
        if (v != model.number(n.bind, n.min)) {
            if (!n.bind.empty()) model.set(n.bind, v);
            emit(UiEvent::Kind::Change, doc, n, v);
        }
    }

    DocumentImpl* input_document() {
        for (auto it = documents.rbegin(); it != documents.rend(); ++it) {
            if ((*it)->visible && (*it)->modal) return it->get();
        }
        return nullptr;
    }

    void tick(const UiInput& in) {
        DocumentImpl* doc = input_document();
        if (doc == nullptr) return;
        // The layout as it is now (a model changed since the last frame, the document just opened):
        // the pointer, the navigation and the scrolling read it.
        prepare(*doc);
        UiNode* under = in.pointer.x >= 0.0f ? hit(*doc->root, in.pointer) : nullptr;
        // Wheel: the scroll under the pointer.
        if (in.wheel != 0.0f) {
            for (UiNode* p = under; p != nullptr; p = p->parent) {
                if (p->style.overflow == ui::ComputedStyle::Overflow::Scroll) {
                    p->scroll = std::max(0.0f, p->scroll - in.wheel * px(40.0f));
                    doc->relayout = true;
                    break;
                }
            }
        }
        if (in.click) {
            if (UiNode* target = actionable(under)) {
                set_focus(*doc, target);
                doc->pressed = target;
                activate(*doc, *target, in.pointer);
            }
        }
        if (in.held && doc->pressed != nullptr && doc->pressed->tag == "slider") {
            slide_to(*doc, *doc->pressed, in.pointer.x);
        }
        if (!in.held && doc->pressed != nullptr) {
            doc->pressed = nullptr;
            doc->restyle = true;
        }
        UiNode* f = doc->focus;
        const bool horizontal_control = f != nullptr && (f->tag == "slider" || f->tag == "select");
        if (horizontal_control && (in.left || in.right)) {
            const int step = in.right ? 1 : -1;
            if (f->tag == "slider") change_slider(*doc, *f, step);
            else change_select(*doc, *f, step);
        } else if (in.up || in.down || in.left || in.right) {
            const glm::vec2 dir(in.left ? -1.0f : in.right ? 1.0f : 0.0f, in.up ? -1.0f : in.down ? 1.0f : 0.0f);
            if (UiNode* next = neighbour(*doc, dir)) set_focus(*doc, next);
        }
        if (in.confirm) {
            if (doc->focus == nullptr) {
                if (UiNode* first = neighbour(*doc, glm::vec2(0.0f, 1.0f))) set_focus(*doc, first);
            } else {
                activate(*doc, *doc->focus, std::nullopt);
            }
        }
        if (in.back) {
            UiEvent e;
            e.kind = UiEvent::Kind::Back;
            e.action = doc->back_action;
            e.document = doc->path;
            events.push_back(std::move(e));
        }
        if (doc->focus == nullptr && !in.click) {
            // Something is always focused for the keyboard and the pad: the element marked
            // `autofocus`, else the first one.
            std::vector<UiNode*> all;
            focusables(*doc->root, all);
            if (!all.empty()) {
                UiNode* autofocus = find_autofocus(*doc->root);
                set_focus(*doc, autofocus != nullptr ? autofocus : all.front(), false);
            }
        }
    }

    UiNode* find_autofocus(UiNode& n) {
        if (n.has_attribute("autofocus") && n.focusable && n.enabled && shown(n)) return &n;
        for (auto& c : n.children) {
            if (UiNode* f = find_autofocus(*c)) return f;
        }
        return nullptr;
    }

    void on_event(const SDL_Event& event) {
        DocumentImpl* doc = input_document();
        if (doc == nullptr || doc->focus == nullptr || doc->focus->tag != "input") return;
        UiNode& n = *doc->focus;
        std::string value = model.text(n.bind);
        bool changed = false;
        if (event.type == SDL_EVENT_TEXT_INPUT) {
            const std::string typed = event.text.text;
            if (decode_utf8(value).size() + decode_utf8(typed).size() <= static_cast<std::size_t>(n.max_length)) {
                value.insert(std::min(n.caret, value.size()), typed);
                n.caret += typed.size();
                changed = true;
            }
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            // Caret moves by whole characters (UTF-8 lead bytes).
            const auto prev = [&](std::size_t i) {
                do { --i; } while (i > 0 && (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80);
                return i;
            };
            const auto next = [&](std::size_t i) {
                do { ++i; } while (i < value.size() && (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80);
                return i;
            };
            switch (event.key.key) {
                case SDLK_BACKSPACE:
                    if (n.caret > 0) {
                        const std::size_t p = prev(n.caret);
                        value.erase(p, n.caret - p);
                        n.caret = p;
                        changed = true;
                    }
                    break;
                case SDLK_DELETE:
                    if (n.caret < value.size()) {
                        value.erase(n.caret, next(n.caret) - n.caret);
                        changed = true;
                    }
                    break;
                case SDLK_LEFT: if (n.caret > 0) n.caret = prev(n.caret); break;
                case SDLK_RIGHT: if (n.caret < value.size()) n.caret = next(n.caret); break;
                case SDLK_HOME: n.caret = 0; break;
                case SDLK_END: n.caret = value.size(); break;
                default: break;
            }
        }
        if (changed) {
            model.set(n.bind, value);
            emit(UiEvent::Kind::Change, *doc, n, value);
        }
    }

    // --- Frame -----------------------------------------------------------------------------------

    void prepare(DocumentImpl& doc) {
        const std::uint64_t language = localization.generation();
        if (doc.model_seen != model.version() || doc.language_seen != language) {
            refresh(doc, *doc.root);
            doc.model_seen = model.version();
            if (doc.language_seen != language) doc.relayout = true;
            doc.language_seen = language;
        }
        if (doc.restyle) {
            restyle(doc, *doc.root, nullptr, nullptr, 0, 1);
            doc.restyle = false;
            doc.relayout = true;
        }
        if (doc.relayout) {
            layout(doc);
        }
    }

    void render(Renderer& renderer, double dt, float base_depth) {
        stats = {};
        time += dt;
        ++frame;
        const glm::vec2 viewport(static_cast<float>(renderer.width()), static_cast<float>(renderer.height()));
        SpriteRenderer& sprites = renderer.screen_sprites();
        float depth = base_depth;
        for (auto& doc_ptr : documents) {
            DocumentImpl& doc = *doc_ptr;
            doc.frame = frame;
            // The world instances nobody asked for since the previous frame go.
            for (auto it = doc.instances.begin(); it != doc.instances.end();) {
                if (it->second->instance_frame + 1 < frame) {
                    remove(doc, it->second);
                    it = doc.instances.erase(it);
                    doc.relayout = true;
                } else {
                    ++it;
                }
            }
            if (!doc.instances.empty()) {
                // Drawn in the order the game asked for them this frame, under the rest: what
                // overlaps is decided by the game's state, not by when each one first appeared (a
                // loaded game draws exactly as the one that was saved).
                auto& children = doc.root->children;
                const auto split = std::stable_partition(children.begin(), children.end(), [](const auto& c) { return c->instance; });
                std::stable_sort(children.begin(), split, [](const auto& a, const auto& b) { return a->instance_order < b->instance_order; });
            }
            if (!doc.visible) continue;
            if (doc.viewport != viewport || doc.scale_seen != scale) {
                doc.viewport = viewport;
                doc.scale_seen = scale;
                doc.relayout = true;
            }
            // Hover (visual only).
            const glm::vec2 pointer = app.input().pointer();
            UiNode* hovered = pointer.x >= 0.0f && &doc == input_document() ? actionable(hit(*doc.root, pointer)) : nullptr;
            if (hovered != doc.hover) {
                doc.hover = hovered;
                doc.restyle = true;
                doc.tooltip_time = 0;
            }
            prepare(doc);
            if (animate(*doc.root, static_cast<float>(dt))) {
                // Transitions change only what is drawn.
            }
            DrawState st{&sprites, depth, 1.0f, std::nullopt, glm::vec2(0.0f)};
            draw(*doc.root, st, doc);
            // Tooltip.
            if (doc.hover != nullptr && doc.hover->has_attribute("tooltip")) {
                doc.tooltip_time += static_cast<float>(dt);
                if (doc.tooltip_time > 0.45f) {
                    draw_tooltip(doc, *doc.hover, pointer, DrawState{&sprites, depth + 0.5f, 1.0f, std::nullopt, glm::vec2(0.0f)});
                }
            }
            depth += 1.0f;
        }
    }

    void draw_tooltip(DocumentImpl& doc, const UiNode& n, glm::vec2 pointer, const DrawState& st) {
        UiNode tip;
        tip.tag = "tooltip";
        tip.text = resolve(n, n.attribute("tooltip"));
        ui::MatchNode m;
        m.type = "tooltip";
        std::vector<const ui::Declaration*> decls;
        for (const ui::StyleSheet* sheet : {&base_sheet}) {
            for (const ui::Rule& r : sheet->rules) {
                for (const ui::Selector& s : r.selectors) {
                    if (ui::matches(s, m)) {
                        for (const ui::Declaration& d : r.declarations) decls.push_back(&d);
                    }
                }
            }
        }
        for (const ui::StyleSheet& sheet : doc.sheets) {
            for (const ui::Rule& r : sheet.rules) {
                for (const ui::Selector& s : r.selectors) {
                    if (ui::matches(s, m)) {
                        for (const ui::Declaration& d : r.declarations) decls.push_back(&d);
                    }
                }
            }
        }
        std::map<std::string, std::string, std::less<>> vars = doc.root->variables;
        tip.style = ui::compute_style(&doc.root->style, decls, vars);
        tip.opacity.value = glm::vec4(1.0f);
        tip.background.value = tip.style.background;
        tip.color.value = tip.style.color;
        tip.border.value = tip.style.border_color;
        tip.scale.value = glm::vec4(1.0f);
        const glm::vec2 size = measure(tip, px(tip.style.max_width.is_auto() ? 320.0f : tip.style.max_width.value), 1e6f);
        glm::vec2 pos = pointer + glm::vec2(px(14.0f), px(18.0f));
        pos = glm::min(pos, doc.viewport - size);
        arrange(tip, pos, size);
        draw(tip, st, doc);
    }

    void remove(DocumentImpl& doc, UiNode* n) {
        UiNode* parent = n->parent;
        if (doc.focus != nullptr && contains(n, doc.focus)) doc.focus = nullptr;
        if (doc.hover != nullptr && contains(n, doc.hover)) doc.hover = nullptr;
        if (doc.pressed != nullptr && contains(n, doc.pressed)) doc.pressed = nullptr;
        if (parent == nullptr) return;
        auto& siblings = parent->children;
        siblings.erase(std::remove_if(siblings.begin(), siblings.end(), [n](const auto& c) { return c.get() == n; }), siblings.end());
    }
};

// --- Elements ---------------------------------------------------------------------------------------

const std::string& UiElement::tag() const { return node_of(node_)->tag; }
const std::string& UiElement::id() const { return node_of(node_)->id; }

namespace {
DocumentImpl* document_of(UiNode* n);
}

void UiElement::set_text(std::string text) {
    UiNode* n = node_of(node_);
    if (n->text_template != text) {
        n->text_template = std::move(text);
        if (DocumentImpl* d = document_of(n)) d->model_seen = 0;
    }
}

void UiElement::set_class(std::string_view name, bool on) {
    UiNode* n = node_of(node_);
    const auto it = std::find(n->classes.begin(), n->classes.end(), name);
    if (on == (it != n->classes.end())) return;
    if (on) n->classes.emplace_back(name);
    else n->classes.erase(it);
    if (DocumentImpl* d = document_of(n)) d->restyle = true;
}

bool UiElement::has_class(std::string_view name) const {
    const UiNode* n = node_of(node_);
    return std::find(n->classes.begin(), n->classes.end(), name) != n->classes.end();
}

void UiElement::set_visible(bool visible) {
    UiNode* n = node_of(node_);
    if (n->visible != visible) {
        n->visible = visible;
        if (DocumentImpl* d = document_of(n)) d->restyle = true;
    }
}

bool UiElement::visible() const { return node_of(node_)->visible; }

void UiElement::set_enabled(bool enabled) {
    UiNode* n = node_of(node_);
    if (n->enabled != enabled) {
        n->enabled = enabled;
        if (DocumentImpl* d = document_of(n)) d->restyle = true;
    }
}

std::string UiElement::attribute(std::string_view name) const { return node_of(node_)->attribute(name); }

glm::vec4 UiElement::rect() const {
    const UiNode* n = node_of(node_);
    return glm::vec4(n->pos, n->size);
}

void UiElement::set_screen_position(glm::vec2 pixels) {
    UiNode* n = node_of(node_);
    if (!n->screen_position || *n->screen_position != pixels) {
        n->screen_position = pixels;
        if (DocumentImpl* d = document_of(n)) d->relayout = true;
    }
}

void UiElement::set_style(std::string_view declarations) {
    UiNode* n = node_of(node_);
    if (n->style_override == declarations) return;
    n->style_override = std::string(declarations);
    n->inline_style = ui::parse_declarations(n->attribute("style") + ";" + n->style_override);
    if (DocumentImpl* d = document_of(n)) d->restyle = true;
}

// --- Documents --------------------------------------------------------------------------------------

namespace {


DocumentImpl* document_of(UiNode* n) {
    while (n->parent != nullptr) n = n->parent;
    return n->document;
}

DocumentImpl* impl_of(void* p) {
    return static_cast<DocumentImpl*>(p);
}

}  // namespace

const std::string& UiDocument::path() const { return impl_of(impl_)->path; }

UiElement* UiDocument::find(std::string_view id) {
    UiNode* n = UiSystem::find(impl_of(impl_)->root.get(), id);
    return n != nullptr ? &n->handle : nullptr;
}

UiElement* UiDocument::root() { return &impl_of(impl_)->root->handle; }

UiElement* UiDocument::focused() {
    UiNode* n = impl_of(impl_)->focus;
    return n != nullptr ? &n->handle : nullptr;
}

void UiDocument::focus(UiElement* element) {
    DocumentImpl* d = impl_of(impl_);
    d->focus = element != nullptr ? node_of(element->node_) : nullptr;
    d->restyle = true;
}

void UiDocument::set_visible(bool visible) {
    DocumentImpl* d = impl_of(impl_);
    if (d->visible != visible) {
        d->visible = visible;
        d->restyle = true;
    }
}

bool UiDocument::visible() const { return impl_of(impl_)->visible; }
int UiDocument::layer() const { return impl_of(impl_)->layer; }
const std::vector<std::string>& UiDocument::problems() const { return impl_of(impl_)->problems; }

UiElement* UiDocument::instance(std::string_view template_id, std::uint64_t key) {
    DocumentImpl* d = impl_of(impl_);
    const auto found = d->instances.find({std::string(template_id), key});
    if (found != d->instances.end()) {
        found->second->instance_frame = d->frame;  // asked for: kept at the next render
        found->second->instance_order = d->instance_sequence++;
        return &found->second->handle;
    }
    const auto t = d->templates.find(template_id);
    if (t == d->templates.end()) {
        return nullptr;
    }
    // Built like the document's other elements; absolute, added to the root.
    std::unique_ptr<UiNode> node;
    {
        // A small builder: the system's is not reachable from here, the template is plain.
        struct Local {
            static std::unique_ptr<UiNode> build(const ui::MarkupNode& m, UiNode* parent) {
                auto n = std::make_unique<UiNode>();
                n->tag = m.tag;
                n->parent = parent;
                n->attributes = m.attributes;
                n->id = m.attribute("id");
                n->classes = split_words(m.attribute("class"));
                n->text_template = m.attribute("text");
                if (m.has("style")) {
                    try {
                        n->inline_style = ui::parse_declarations(m.attribute("style"), m.line);
                    } catch (const ui::StyleError& e) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ui: template: %s", e.what());
                    }
                }
                for (const ui::MarkupNode& c : m.children) {
                    if (c.tag.empty()) n->text_template += c.text;
                    else n->children.push_back(build(c, n.get()));
                }
                return n;
            }
        };
        node = Local::build(*t->second, d->root.get());
    }
    node->instance = true;
    node->instance_frame = d->frame;
    node->instance_order = d->instance_sequence++;
    node->item_prefix = std::string(template_id) + "." + std::to_string(key) + ".";
    UiNode* raw = node.get();
    // First among the root's children: drawn under the document's own elements (a health bar
    // passes under the figures of the HUD, not over them).
    d->root->children.insert(d->root->children.begin(), std::move(node));
    d->instances[{std::string(template_id), key}] = raw;
    d->restyle = true;
    d->model_seen = 0;
    return &raw->handle;
}

// --- Ui ---------------------------------------------------------------------------------------------

Ui::Ui(Application& app, std::string default_font) : system_(std::make_unique<UiSystem>(app, std::move(default_font))) {}

Ui::~Ui() {
    if (system_->text_input) {
        SDL_StopTextInput(system_->app.window());
    }
}

UiDocument& Ui::open(const std::string& path, int layer) {
    for (const auto& doc : system_->documents) {
        if (doc->path == path) return doc->handle;
    }
    auto doc = std::make_unique<DocumentImpl>();
    doc->path = path;
    doc->layer = layer;
    // Laid out for the screen from the start: the first tick may come before the first render.
    doc->viewport = glm::vec2(static_cast<float>(system_->app.renderer().width()), static_cast<float>(system_->app.renderer().height()));
    doc->scale_seen = system_->scale;
    system_->load(*doc);  // throws
    DocumentImpl* raw = doc.get();
    auto& list = system_->documents;
    const auto at = std::upper_bound(list.begin(), list.end(), layer, [](int l, const auto& d) { return l < d->layer; });
    list.insert(at, std::move(doc));
    return raw->handle;
}

void Ui::close(UiDocument& document) {
    close(document.path());
}

void Ui::close(const std::string& path) {
    auto& list = system_->documents;
    for (auto it = list.begin(); it != list.end(); ++it) {
        if ((*it)->path == path) {
            list.erase(it);
            system_->update_text_input(nullptr);
            return;
        }
    }
}

UiDocument* Ui::document(const std::string& path) {
    for (const auto& doc : system_->documents) {
        if (doc->path == path) return &doc->handle;
    }
    return nullptr;
}

UiModel& Ui::model() { return system_->model; }

void Ui::set_texture(const std::string& name, const Texture* texture) {
    if (texture == nullptr) {
        system_->named_textures.erase(name);
    } else {
        system_->named_textures[name] = texture;
    }
}

void Ui::set_scale(float scale) {
    system_->scale = std::clamp(scale, 0.25f, 8.0f);
}

float Ui::scale() const { return system_->scale; }

void Ui::tick(const UiInput& input) { system_->tick(input); }

std::vector<UiEvent> Ui::take_events() {
    std::vector<UiEvent> out;
    out.swap(system_->events);
    return out;
}

void Ui::on_event(const SDL_Event& event) { system_->on_event(event); }

void Ui::render(Renderer& renderer, double dt, float base_depth) { system_->render(renderer, dt, base_depth); }

std::vector<std::string> Ui::problems() const {
    std::vector<std::string> out;
    for (const auto& doc : system_->documents) {
        out.insert(out.end(), doc->problems.begin(), doc->problems.end());
    }
    for (const std::string& id : system_->unknown_messages) {
        out.push_back("unknown message '" + id + "'");
    }
    return out;
}

Ui::Stats Ui::stats() const { return system_->stats; }

}  // namespace moteur
