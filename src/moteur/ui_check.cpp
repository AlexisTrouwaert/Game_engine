// The check of a game's screens without showing them (milestone 9 bis, part 6): what the cook and
// --check-data run, so that a broken screen stops the delivery instead of the player.

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "moteur/localization.hpp"
#include "moteur/ui.hpp"
#include "moteur/ui_markup.hpp"
#include "moteur/ui_style.hpp"

namespace moteur {

namespace {

namespace fs = std::filesystem;

fs::path from_utf8(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string to_utf8(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

bool read(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    out = text.str();
    return true;
}

struct Checker {
    const std::vector<std::string>& roots;
    const Localization* strings;
    std::vector<std::string>& problems;

    // The declarations, applied one by one: unknown properties and bad values. A value made of
    // variables is only known in its document; it is not judged here.
    void declarations(const std::vector<ui::Declaration>& list, const std::string& where) {
        for (const ui::Declaration& d : list) {
            if (d.property.starts_with("--") || d.value.find("var(") != std::string::npos) {
                continue;
            }
            std::map<std::string, std::string, std::less<>> variables;
            std::vector<std::string> warnings;
            const std::vector<const ui::Declaration*> one = {&d};
            ui::compute_style(nullptr, one, variables, &warnings);
            for (const std::string& w : warnings) {
                std::string line = where;
                line += ": ";
                line += w;
                problems.push_back(std::move(line));
            }
        }
    }

    bool exists(const std::string& key) const {
        std::error_code error;
        for (const std::string& root : roots) {
            if (fs::is_regular_file(from_utf8(root) / from_utf8(key), error)) {
                return true;
            }
        }
        return false;
    }

    // The `{id}` of a text that name no message.
    void texts(std::string_view text, const std::string& where) {
        if (strings == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '{') {
                continue;
            }
            if (i + 1 < text.size() && text[i + 1] == '{') {
                ++i;  // "{{": a brace
                continue;
            }
            const std::size_t close = text.find('}', i);
            if (close == std::string_view::npos) {
                break;
            }
            const std::string_view id = text.substr(i + 1, close - i - 1);
            if (!id.empty() && id[0] != '$' && !strings->has(id)) {
                problems.push_back(where + ": unknown message '" + std::string(id) + "'");
            }
            i = close;
        }
    }

    void element(const ui::MarkupNode& n, const std::string& file) {
        const std::string where = file + ":" + std::to_string(n.line);
        if (n.tag.empty()) {
            texts(n.text, where);
            return;
        }
        if (n.tag == "style" && n.has("src") && !exists(n.attribute("src"))) {
            problems.push_back(where + ": style sheet '" + n.attribute("src") + "' not found");
        }
        if (n.has("style")) {
            try {
                declarations(ui::parse_declarations(n.attribute("style"), n.line), where);
            } catch (const ui::StyleError& e) {
                problems.push_back(where + ": " + e.what());
            }
        }
        for (const char* attribute : {"text", "options", "tooltip", "placeholder"}) {
            if (n.has(attribute)) {
                texts(n.attribute(attribute), where);
            }
        }
        for (const ui::MarkupNode& child : n.children) {
            element(child, file);
        }
    }

    void file(const fs::path& path, const std::string& key) {
        std::string text;
        if (!read(path, text)) {
            problems.push_back(key + ": cannot be read");
            return;
        }
        if (path.extension() == ".uss") {
            try {
                const ui::StyleSheet sheet = ui::parse_style_sheet(text, key);
                for (const ui::Rule& rule : sheet.rules) {
                    declarations(rule.declarations, key + ":" + std::to_string(rule.line));
                }
            } catch (const ui::StyleError& e) {
                problems.push_back(key + ": " + e.what());
            }
        } else {
            try {
                const ui::MarkupNode root = ui::parse_markup(text);
                if (root.tag != "ui") {
                    problems.push_back(key + ": the root element is <" + root.tag + ">, not <ui>");
                }
                element(root, key);
            } catch (const ui::MarkupError& e) {
                problems.push_back(key + ": " + e.what());
            }
        }
    }
};

}  // namespace

std::vector<std::string> check_ui_files(const std::vector<std::string>& roots, const Localization* strings) {
    std::vector<std::string> problems;
    Checker checker{roots, strings, problems};
    std::error_code error;
    for (const std::string& root : roots) {
        const fs::path base = from_utf8(root);
        const fs::path folder = base / "ui";
        if (!fs::is_directory(folder, error)) {
            continue;
        }
        for (fs::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            const fs::path& path = it->path();
            if (it->is_regular_file(error) && (path.extension() == ".ui" || path.extension() == ".uss")) {
                checker.file(path, to_utf8(path.lexically_relative(base)));
            }
        }
    }
    return problems;
}

}  // namespace moteur
