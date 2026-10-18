#include "moteur/credits.hpp"

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

#include <iterator>

#include "moteur/document.hpp"
#include "moteur/paths.hpp"

namespace moteur {

namespace {

#if defined(SDL_PLATFORM_WINDOWS)
constexpr const char* kPlatform = "windows";
#elif defined(SDL_PLATFORM_MACOS)
constexpr const char* kPlatform = "macos";
#else
constexpr const char* kPlatform = "other";
#endif

bool built_with(const std::string& option) {
    // The development tools (Dear ImGui, efsw...): not in the shipped game. "DEVELOPPEMENT": its French
    // name until 2026-10-17, still read.
    if (option == "DEVELOPMENT" || option == "DEVELOPPEMENT") {
#if defined(MOTEUR_SHIPPING)
        return false;
#else
        return true;
#endif
    }
#if defined(MOTEUR_TRACY)
    return option == "MOTEUR_TRACY";
#else
    return false;
#endif
}

void append(std::vector<CreditEntry>& entries, const nlohmann::json& list) {
    for (const nlohmann::json& item : list) {
        const std::string platform = item.value("platform", "");
        if (!platform.empty() && platform != kPlatform) {
            continue;
        }
        if (const std::string build = item.value("build", ""); !build.empty() && !built_with(build)) {
            continue;
        }
        CreditEntry entry;
        entry.name = item.value("name", "");
        entry.version = item.value("version", "");
        entry.license = item.value("license", "");
        entry.copyright = item.value("copyright", "");
        entry.authors = item.value("authors", "");
        entry.source = item.value("source", "");
        entry.url = item.value("url", "");
        entry.license_file = item.value("license_file", "");
        entry.file = item.value("file", "");
        entries.push_back(std::move(entry));
    }
}

}  // namespace

void Credits::add_file(const std::string& path) {
    try {
        // All or nothing: the file's lists are read aside, then appended once all of them are read.
        const nlohmann::json doc = parse_document(read_text_file(path));
        std::vector<CreditEntry> read[5];
        append(read[0], doc.value("libraries", nlohmann::json::array()));
        append(read[1], doc.value("fonts", nlohmann::json::array()));
        append(read[2], doc.value("models", nlohmann::json::array()));
        append(read[3], doc.value("environments", nlohmann::json::array()));
        append(read[4], doc.value("sounds", nlohmann::json::array()));
        std::vector<CreditEntry>* lists[5] = {&libraries, &fonts, &models, &environments, &sounds};
        for (int i = 0; i < 5; ++i) {
            lists[i]->insert(lists[i]->end(), std::make_move_iterator(read[i].begin()), std::make_move_iterator(read[i].end()));
        }
    } catch (const std::exception& e) {
        if (!error.empty()) {
            error += '\n';
        }
        error += "Cannot read the credits (" + path + "): " + e.what();
    }
}

Credits Credits::load(const std::vector<std::string>& paths) {
    Credits credits;
    for (const std::string& path : paths) {
        credits.add_file(path);
    }
    return credits;
}

}  // namespace moteur
