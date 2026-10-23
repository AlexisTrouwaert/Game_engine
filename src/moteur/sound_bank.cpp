#include "moteur/sound_bank.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <stdexcept>

#include "moteur/document.hpp"

namespace moteur {

namespace {

constexpr const char* kBaseBuses[] = {"music", "effects", "ambience", "interface"};

// The pieces one after the other, in one string (a place in the bank, a message).
template <class... Pieces>
std::string join(std::string first, const Pieces&... rest) {
    (first.append(rest), ...);
    return first;
}

[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw std::runtime_error(where + ": " + what);
}

void check_keys(const nlohmann::json& object, std::initializer_list<const char*> keys, const std::string& where) {
    if (!object.is_object()) {
        fail(where, "an object expected");
    }
    for (const auto& [key, value] : object.items()) {
        if (std::find_if(keys.begin(), keys.end(), [&](const char* k) { return key == k; }) == keys.end()) {
            fail(where, "unknown key '" + key + "'");
        }
    }
}

float number(const nlohmann::json& value, const std::string& where) {
    if (!value.is_number()) {
        fail(where, "a number expected");
    }
    return value.get<float>();
}

// A number, or a range [low, high].
glm::vec2 range(const nlohmann::json& value, const std::string& where) {
    if (value.is_number()) {
        const float v = value.get<float>();
        return {v, v};
    }
    if (!value.is_array() || value.size() != 2) {
        fail(where, "a number or [low, high] expected");
    }
    const glm::vec2 r(number(value[0], where), number(value[1], where));
    if (r.x > r.y) {
        fail(where, "the range is upside down");
    }
    return r;
}

std::vector<std::string> file_list(const nlohmann::json& value, const std::string& where) {
    if (!value.is_array() || value.empty()) {
        fail(where, "a list of sound files expected");
    }
    std::vector<std::string> out;
    for (const nlohmann::json& f : value) {
        if (!f.is_string() || f.get<std::string>().empty()) {
            fail(where, "a sound file is a path");
        }
        out.push_back(f.get<std::string>());
    }
    return out;
}

AudioParameter parameter(const nlohmann::json& value, const std::string& where) {
    check_keys(value, {"min", "max", "default", "labels"}, where);
    AudioParameter p;
    if (value.contains("labels")) {
        for (const nlohmann::json& label : value.at("labels")) {
            p.labels.push_back(label.get<std::string>());
        }
        if (p.labels.empty()) {
            fail(where, "labels: at least one");
        }
        p.min = 0.0f;
        p.max = static_cast<float>(p.labels.size() - 1);
        if (value.contains("default")) {
            const nlohmann::json& d = value.at("default");
            const int index = d.is_string() ? p.label(d.get<std::string>()) : -1;
            if (index < 0) {
                fail(where, "default: one of its labels");
            }
            p.initial = static_cast<float>(index);
        }
        return p;
    }
    p.min = value.contains("min") ? number(value.at("min"), where + " > min") : 0.0f;
    p.max = value.contains("max") ? number(value.at("max"), where + " > max") : 1.0f;
    if (p.min > p.max) {
        fail(where, "min above max");
    }
    p.initial = value.contains("default") ? number(value.at("default"), where + " > default") : p.min;
    p.initial = std::clamp(p.initial, p.min, p.max);
    return p;
}

AudioAutomation automation(const nlohmann::json& value, const std::string& where) {
    check_keys(value, {"parameter", "property", "points"}, where);
    AudioAutomation a;
    a.parameter = value.at("parameter").get<std::string>();
    const std::string property = value.value("property", std::string("volume"));
    if (property == "volume") {
        a.property = AudioAutomation::Property::Volume;
    } else if (property == "pitch") {
        a.property = AudioAutomation::Property::Pitch;
    } else if (property == "lowpass") {
        a.property = AudioAutomation::Property::Lowpass;
    } else {
        fail(where, "property: volume, pitch or lowpass");
    }
    for (const nlohmann::json& point : value.at("points")) {
        if (!point.is_array() || point.size() != 2) {
            fail(where, "points: [x, y] pairs");
        }
        const glm::vec2 p(number(point[0], where), number(point[1], where));
        if (!a.curve.points.empty() && p.x <= a.curve.points.back().x) {
            fail(where, "points: x increasing");
        }
        a.curve.points.push_back(p);
    }
    if (a.curve.points.empty()) {
        fail(where, "points: at least one");
    }
    return a;
}

}  // namespace

float AudioCurve::at(float x) const {
    if (points.empty()) {
        return 1.0f;
    }
    if (x <= points.front().x) {
        return points.front().y;
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (x <= points[i].x) {
            const glm::vec2 a = points[i - 1];
            const glm::vec2 b = points[i];
            return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
        }
    }
    return points.back().y;
}

int AudioParameter::label(std::string_view name) const {
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (labels[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool SoundBank::is_base_bus(std::string_view name) {
    return std::find_if(std::begin(kBaseBuses), std::end(kBaseBuses), [&](const char* b) { return name == b; }) != std::end(kBaseBuses);
}

SoundBank SoundBank::parse(std::string_view text, const std::string& source) {
    static std::atomic<std::uint64_t> next_id{1};
    SoundBank bank;
    bank.source_ = source;
    bank.id_ = next_id.fetch_add(1);
    nlohmann::json doc;
    try {
        doc = parse_document(text);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Sound bank '" + source + "' is not valid: " + e.what());
    }
    try {
        check_keys(doc, {"version", "buses", "reverbs", "reverb_buses", "snapshots", "vcas", "parameters", "events"}, "bank");
        if (doc.value("version", 0) != kVersion) {
            fail("bank", "version " + std::to_string(doc.value("version", 0)) + " (expected 1)");
        }
        std::set<std::string> buses(std::begin(kBaseBuses), std::end(kBaseBuses));
        if (doc.contains("buses")) {
            // Sorted by name: a parent ("effects/pas") before its children ("effects/pas/pierre").
            for (const auto& [name, value] : doc.at("buses").items()) {
                const std::string where = "buses > " + name;
                check_keys(value, {"volume"}, where);
                if (is_base_bus(name)) {
                    fail(where, "a base bus is not declared");
                }
                const std::size_t slash = name.rfind('/');
                AudioBusDescription bus;
                bus.name = name;
                bus.parent = slash == std::string::npos ? std::string() : name.substr(0, slash);
                if (bus.parent.empty() || buses.count(bus.parent) == 0) {
                    fail(where, "its parent '" + bus.parent + "' is not a bus (\"effects/pas\" is under \"effects\")");
                }
                bus.volume = value.contains("volume") ? number(value.at("volume"), where) : 1.0f;
                buses.insert(name);
                bank.buses_.push_back(bus);
            }
        }
        if (doc.contains("reverbs")) {
            for (const auto& [name, value] : doc.at("reverbs").items()) {
                const std::string where = "reverbs > " + name;
                check_keys(value, {"room", "damping", "wet"}, where);
                ReverbDescription r;
                r.room = std::clamp(value.contains("room") ? number(value.at("room"), where) : r.room, 0.0f, 1.0f);
                r.damping = std::clamp(value.contains("damping") ? number(value.at("damping"), where) : r.damping, 0.0f, 1.0f);
                r.wet = std::clamp(value.contains("wet") ? number(value.at("wet"), where) : r.wet, 0.0f, 1.0f);
                bank.reverbs_[name] = r;
            }
        }
        if (doc.contains("reverb_buses")) {
            for (const nlohmann::json& b : doc.at("reverb_buses")) {
                const std::string name = b.get<std::string>();
                if (!is_base_bus(name)) {
                    fail("reverb_buses", "'" + name + "' is not a base bus");
                }
                bank.reverb_buses_.push_back(name);
            }
        }
        if (doc.contains("vcas")) {
            for (const auto& [name, value] : doc.at("vcas").items()) {
                std::vector<std::string> list = value.get<std::vector<std::string>>();
                for (const std::string& b : list) {
                    if (buses.count(b) == 0) {
                        fail("vcas > " + name, "no bus '" + b + "'");
                    }
                }
                bank.vcas_[name] = std::move(list);
            }
        }
        if (doc.contains("snapshots")) {
            for (const auto& [name, value] : doc.at("snapshots").items()) {
                const std::string where = "snapshots > " + name;
                check_keys(value, {"fade", "buses"}, where);
                SnapshotDescription s;
                s.fade = value.contains("fade") ? std::max(0.0f, number(value.at("fade"), where)) : s.fade;
                for (const auto& [bus, settings] : value.at("buses").items()) {
                    if (buses.count(bus) == 0) {
                        fail(where, "no bus '" + bus + "'");
                    }
                    check_keys(settings, {"volume", "paused"}, join(where, " > ", bus));
                    SnapshotDescription::Bus b;
                    b.volume = settings.contains("volume") ? std::max(0.0f, number(settings.at("volume"), where)) : 1.0f;
                    b.paused = settings.value("paused", false);
                    s.buses[bus] = b;
                }
                bank.snapshots_[name] = std::move(s);
            }
        }
        if (doc.contains("parameters")) {
            for (const auto& [name, value] : doc.at("parameters").items()) {
                bank.parameters_[name] = parameter(value, "parameters > " + name);
            }
        }
        if (!doc.contains("events") || !doc.at("events").is_object() || doc.at("events").empty()) {
            fail("bank", "no events");
        }
        for (const auto& [name, value] : doc.at("events").items()) {
            const std::string where = "events > " + name;
            check_keys(value, {"bus", "sounds", "order", "volume", "pitch", "loop", "stream", "fade_in", "fade_out", "spatial",
                               "max_instances", "steal", "priority", "parameters", "automation", "music"},
                       where);
            AudioEventDescription e;
            e.name = name;
            e.bus = value.value("bus", std::string(e.bus));
            if (buses.count(e.bus) == 0) {
                fail(where, "no bus '" + e.bus + "'");
            }
            if (value.contains("parameters")) {
                for (const auto& [p, v] : value.at("parameters").items()) {
                    e.parameters[p] = parameter(v, join(where, " > parameters > ", p));
                }
            }
            const auto known = [&](const std::string& p) {
                return e.parameters.count(p) != 0 || bank.parameters_.count(p) != 0;
            };
            const auto labelled = [&](const std::string& p) -> const AudioParameter* {
                const auto local = e.parameters.find(p);
                if (local != e.parameters.end()) {
                    return &local->second;
                }
                const auto global = bank.parameters_.find(p);
                return global != bank.parameters_.end() ? &global->second : nullptr;
            };
            if (value.contains("sounds")) {
                const nlohmann::json& sounds = value.at("sounds");
                if (sounds.is_object()) {
                    check_keys(sounds, {"parameter", "cases"}, where + " > sounds");
                    e.switch_parameter = sounds.at("parameter").get<std::string>();
                    const AudioParameter* p = labelled(e.switch_parameter);
                    if (p == nullptr || p->labels.empty()) {
                        fail(where, "sounds: '" + e.switch_parameter + "' is not a labelled parameter");
                    }
                    for (const auto& [label, list] : sounds.at("cases").items()) {
                        if (p->label(label) < 0) {
                            fail(where, "sounds: '" + label + "' is not a label of '" + e.switch_parameter + "'");
                        }
                        e.cases[label] = file_list(list, join(where, " > sounds > ", label));
                    }
                } else {
                    e.sounds = file_list(sounds, where + " > sounds");
                }
            }
            const std::string order = value.value("order", std::string("random"));
            e.order = order == "shuffle"    ? AudioEventDescription::Order::Shuffle
                      : order == "sequence" ? AudioEventDescription::Order::Sequence
                                            : AudioEventDescription::Order::Random;
            if (order != "random" && order != "shuffle" && order != "sequence") {
                fail(where, "order: random, shuffle or sequence");
            }
            if (value.contains("volume")) e.volume = range(value.at("volume"), where + " > volume");
            if (value.contains("pitch")) e.pitch = range(value.at("pitch"), where + " > pitch");
            e.loop = value.value("loop", false);
            e.stream = value.value("stream", false);
            e.fade_in = std::max(0.0f, value.value("fade_in", 0.0f));
            e.fade_out = std::max(0.0f, value.value("fade_out", 0.0f));
            if (value.contains("spatial")) {
                const nlohmann::json& s = value.at("spatial");
                check_keys(s, {"min", "max", "occlusion"}, where + " > spatial");
                e.spatial = true;
                e.min_distance = s.contains("min") ? number(s.at("min"), where) : e.min_distance;
                e.max_distance = s.contains("max") ? number(s.at("max"), where) : e.max_distance;
                e.occlusion = s.value("occlusion", false);
                if (!(e.min_distance > 0.0f) || e.max_distance <= e.min_distance) {
                    fail(where, "spatial: 0 < min < max");
                }
            }
            e.max_instances = std::max(0, value.value("max_instances", 0));
            const std::string steal = value.value("steal", std::string("oldest"));
            e.steal = steal == "quietest" ? AudioEventDescription::Steal::Quietest
                      : steal == "none"   ? AudioEventDescription::Steal::None
                                          : AudioEventDescription::Steal::Oldest;
            if (steal != "oldest" && steal != "quietest" && steal != "none") {
                fail(where, "steal: oldest, quietest or none");
            }
            e.priority = std::clamp(value.value("priority", 128), 0, 255);
            if (value.contains("automation")) {
                for (const nlohmann::json& a : value.at("automation")) {
                    AudioAutomation parsed = automation(a, where + " > automation");
                    if (!known(parsed.parameter)) {
                        fail(where, "automation: no parameter '" + parsed.parameter + "'");
                    }
                    e.automation.push_back(std::move(parsed));
                }
            }
            if (value.contains("music")) {
                const nlohmann::json& m = value.at("music");
                check_keys(m, {"parameter", "sections", "quantize", "crossfade"}, where + " > music");
                e.is_music = true;
                e.stream = true;
                e.loop = true;
                e.music.parameter = m.at("parameter").get<std::string>();
                const AudioParameter* p = labelled(e.music.parameter);
                if (p == nullptr || p->labels.empty()) {
                    fail(where, "music: '" + e.music.parameter + "' is not a labelled parameter");
                }
                const std::string quantize = m.value("quantize", std::string("bar"));
                e.music.quantize = quantize == "beat"  ? AudioEventDescription::Music::Quantize::Beat
                                   : quantize == "now" ? AudioEventDescription::Music::Quantize::Now
                                                       : AudioEventDescription::Music::Quantize::Bar;
                e.music.crossfade = std::max(0.0f, m.value("crossfade", 0.5f));
                for (const auto& [label, section] : m.at("sections").items()) {
                    const std::string at = join(where, " > music > ", label);
                    if (p->label(label) < 0) {
                        fail(at, "not a label of '" + e.music.parameter + "'");
                    }
                    check_keys(section, {"file", "tempo", "beats_per_bar", "layers"}, at);
                    MusicSection s;
                    s.file = section.at("file").get<std::string>();
                    s.tempo = section.contains("tempo") ? number(section.at("tempo"), at) : s.tempo;
                    s.beats_per_bar = section.value("beats_per_bar", 4);
                    if (!(s.tempo > 0.0f) || s.beats_per_bar < 1) {
                        fail(at, "tempo and beats_per_bar above 0");
                    }
                    if (section.contains("layers")) {
                        for (const nlohmann::json& l : section.at("layers")) {
                            check_keys(l, {"file", "parameter", "points"}, at + " > layers");
                            MusicSection::Layer layer;
                            layer.file = l.at("file").get<std::string>();
                            layer.automation = automation({{"parameter", l.at("parameter")}, {"points", l.at("points")}}, at + " > layers");
                            if (!known(layer.automation.parameter)) {
                                fail(at, "layers: no parameter '" + layer.automation.parameter + "'");
                            }
                            s.layers.push_back(std::move(layer));
                        }
                    }
                    e.music.sections[label] = std::move(s);
                }
                if (e.music.sections.empty()) {
                    fail(where, "music: no section");
                }
            } else if (e.sounds.empty() && e.cases.empty()) {
                fail(where, "no sounds");
            }
            bank.events_[name] = std::move(e);
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Sound bank '" + source + "' is not valid: " + e.what());
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Sound bank '" + source + "': " + e.what());
    }
    return bank;
}

const AudioEventDescription* SoundBank::event(std::string_view name) const {
    const auto found = events_.find(name);
    return found != events_.end() ? &found->second : nullptr;
}

int check_sound_banks(const std::vector<std::string>& roots, std::vector<std::string>& errors,
                      std::vector<std::string>& warnings) {
    namespace fs = std::filesystem;
    const auto from_utf8 = [](const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); };
    const auto exists = [&](const std::string& key) {
        std::error_code error;
        return std::any_of(roots.begin(), roots.end(), [&](const std::string& r) { return fs::is_regular_file(from_utf8(r) / from_utf8(key), error); });
    };
    int banks = 0;
    std::error_code error;
    for (const std::string& r : roots) {
        const fs::path base = from_utf8(r);
        const fs::path folder = base / "audio";
        if (!fs::is_directory(folder, error)) {
            continue;
        }
        for (fs::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            const fs::path& path = it->path();
            const std::u8string name = path.filename().u8string();
            if (!it->is_regular_file(error) || !std::string(name.begin(), name.end()).ends_with(".bank.json")) {
                continue;
            }
            const std::u8string relative = path.lexically_relative(base).generic_u8string();
            const std::string key(relative.begin(), relative.end());
            std::ifstream file(path, std::ios::binary);
            std::ostringstream text;
            text << file.rdbuf();
            ++banks;
            try {
                const SoundBank bank = SoundBank::parse(text.str(), key);
                for (const std::string& sound : bank.files()) {
                    if (!exists(sound)) {
                        warnings.push_back(join(key, ": sound '", sound, "' not found (tools/audio/fetch_test_sounds.py)"));
                    }
                }
            } catch (const std::exception& e) {
                errors.push_back(key + ": " + e.what());
            }
        }
    }
    return banks;
}

std::vector<std::string> SoundBank::files() const {
    std::set<std::string> all;
    for (const auto& [name, e] : events_) {
        all.insert(e.sounds.begin(), e.sounds.end());
        for (const auto& [label, list] : e.cases) {
            all.insert(list.begin(), list.end());
        }
        for (const auto& [label, section] : e.music.sections) {
            all.insert(section.file);
            for (const MusicSection::Layer& layer : section.layers) {
                all.insert(layer.file);
            }
        }
    }
    return {all.begin(), all.end()};
}

}  // namespace moteur
