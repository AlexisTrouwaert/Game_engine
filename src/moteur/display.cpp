#include "moteur/display.hpp"

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace moteur {

namespace {

const char* const kModes[] = {"windowed", "fullscreen"};
const char* const kShadows[] = {"low", "medium", "high"};
// The French names of the first version of the file (until 2026-10-17), still read.
const char* const kOldModes[] = {"fenetre", "plein_ecran"};
const char* const kOldShadows[] = {"basses", "moyennes", "hautes"};

// The key, or the French key it had (see above).
const nlohmann::json* field(const nlohmann::json& doc, const char* key, const char* old_key) {
    if (const auto found = doc.find(key); found != doc.end()) {
        return &*found;
    }
    if (const auto found = doc.find(old_key); found != doc.end()) {
        return &*found;
    }
    return nullptr;
}

}  // namespace

std::string DisplaySettings::to_json() const {
    nlohmann::json doc;
    doc["version"] = kVersion;
    doc["mode"] = kModes[static_cast<int>(mode)];
    doc["display"] = display;
    doc["vsync"] = vsync;
    doc["frame_limit"] = frame_limit;
    // Rounded to the hundredth: the file shows 0.7, not the float's 0.699999988.
    doc["render_scale"] = std::round(static_cast<double>(render_scale) * 100.0) / 100.0;
    doc["anti_aliasing"] = anti_aliasing_name(anti_aliasing);
    doc["shadows"] = kShadows[static_cast<int>(shadows)];
    return doc.dump(2) + "\n";
}

DisplaySettings DisplaySettings::from_json(const std::string& text, std::vector<std::string>& warnings,
                                           const DisplaySettings& defaults) {
    DisplaySettings settings = defaults;
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        warnings.push_back(std::string("display settings unreadable, defaults used: ") + e.what());
        return settings;
    }
    if (!doc.is_object()) {
        warnings.push_back("display settings: an object was expected, defaults used");
        return settings;
    }
    // No version: the first files (until 2026-10-17) may lack it, read as version 1. A later
    // version (a newer game) may mean something else by the same keys: not guessed.
    if (const auto found = doc.find("version"); found != doc.end()) {
        if (!found->is_number_integer() || found->get<long long>() != kVersion) {
            warnings.push_back("display settings: version " + found->dump() + " is not supported (expected " +
                               std::to_string(kVersion) + "), defaults used");
            return settings;
        }
    }
    // A value of the wrong JSON type keeps the default, with a warning.
    const auto typed = [&doc, &warnings](const char* key, const char* old_key, bool (nlohmann::json::*is)() const noexcept,
                                         const char* expected) -> const nlohmann::json* {
        const nlohmann::json* value = field(doc, key, old_key);
        if (value != nullptr && !(value->*is)()) {
            warnings.push_back(std::string("display settings: '") + key + "' should be " + expected + ", not " +
                               value->dump() + "; default kept");
            return nullptr;
        }
        return value;
    };
    const auto text_of = [&typed](const char* key, const char* old_key) {
        const nlohmann::json* value = typed(key, old_key, &nlohmann::json::is_string, "a string");
        return value != nullptr ? value->get<std::string>() : std::string();
    };
    if (const std::string mode = text_of("mode", "mode"); !mode.empty()) {
        bool known = false;
        for (int i = 0; i < 2; ++i) {
            if (mode == kModes[i] || mode == kOldModes[i]) {
                settings.mode = static_cast<WindowMode>(i);
                known = true;
            }
        }
        if (!known) {
            warnings.push_back("unknown window mode '" + mode + "'");
        }
    }
    if (const std::string shadows = text_of("shadows", "ombres"); !shadows.empty()) {
        bool known = false;
        for (int i = 0; i < 3; ++i) {
            if (shadows == kShadows[i] || shadows == kOldShadows[i]) {
                settings.shadows = static_cast<ShadowQuality>(i);
                known = true;
            }
        }
        if (!known) {
            warnings.push_back("unknown shadow quality '" + shadows + "'");
        }
    }
    if (const std::string name = text_of("anti_aliasing", "anticrenelage"); !name.empty() && !parse_anti_aliasing(name, settings.anti_aliasing)) {
        warnings.push_back("unknown anti-aliasing '" + name + "'");
    }
    if (const nlohmann::json* value = typed("display", "ecran", &nlohmann::json::is_number_integer, "an integer")) {
        settings.display = std::max(0, value->get<int>());
    }
    if (const nlohmann::json* value = typed("vsync", "synchronisation", &nlohmann::json::is_boolean, "true or false")) {
        settings.vsync = value->get<bool>();
    }
    if (const nlohmann::json* value = typed("frame_limit", "limite_images", &nlohmann::json::is_number_integer, "an integer")) {
        settings.frame_limit = std::clamp(value->get<int>(), 0, 1000);
    }
    if (const nlohmann::json* value = typed("render_scale", "resolution_rendu", &nlohmann::json::is_number, "a number")) {
        settings.render_scale = std::clamp(value->get<float>(), 0.5f, 1.0f);
    }
    return settings;
}

glm::vec2 window_to_pixels(glm::vec2 point, glm::vec2 window_size_points, glm::vec2 window_size_pixels) {
    if (window_size_points.x <= 0.0f || window_size_points.y <= 0.0f) {
        return point;
    }
    return point * (window_size_pixels / window_size_points);
}

std::vector<std::string> displays() {
    std::vector<std::string> names;
    int count = 0;
    if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetDisplayName(ids[i]);
            names.emplace_back(name != nullptr ? name : "Display " + std::to_string(i + 1));
        }
        SDL_free(ids);
    }
    return names;
}

ShadowCaps shadow_caps(ShadowQuality quality) {
    switch (quality) {
        case ShadowQuality::Low:
            return {1024, 1, 256};
        case ShadowQuality::Medium:
            return {2048, 2, 512};
        case ShadowQuality::High:
        default:
            return {8192, 8, 2048};  // what the game asks for, as it is
    }
}

const char* window_mode_name(WindowMode mode) {
    return mode == WindowMode::Fullscreen ? "fullscreen" : "windowed";
}

const char* shadow_quality_name(ShadowQuality quality) {
    switch (quality) {
        case ShadowQuality::Low:
            return "low";
        case ShadowQuality::Medium:
            return "medium";
        default:
            return "high";
    }
}

}  // namespace moteur
