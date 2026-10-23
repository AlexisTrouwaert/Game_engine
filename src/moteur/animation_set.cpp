#include "moteur/animation_set.hpp"

#include "moteur/document.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace moteur {

AnimationSet AnimationSet::parse(std::string_view json_text, const std::string& source) {
    AnimationSet set;
    set.source_ = source;
    nlohmann::json doc;
    try {
        doc = parse_document(json_text);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Animation set '" + source + "' is not valid: " + e.what());
    }
    try {
        const int version = doc.value("version", 0);
        if (version != kVersion) {
            throw std::runtime_error("format version " + std::to_string(version) + " is not supported (expected " +
                                     std::to_string(kVersion) + ")");
        }
        set.default_fade_ticks_ = doc.value("fade_ticks", 0);
        if (set.default_fade_ticks_ < 0) {
            throw std::runtime_error("fade_ticks is negative");
        }
        if (doc.contains("clips")) {
            for (const auto& [name, clip] : doc.at("clips").items()) {
                ClipSettings settings;
                settings.ground_speed = clip.value("ground_speed", 0.0f);
                settings.fade_ticks = clip.value("fade_ticks", -1);
                settings.phase = clip.value("phase", 0.0f);
                if (clip.contains("root_motion")) {
                    const nlohmann::json& root = clip.at("root_motion");
                    settings.root_motion = root.is_boolean() ? (root.get<bool>() ? 1.0f : 0.0f) : root.get<float>();
                    if (settings.root_motion < 0.0f) {
                        throw std::runtime_error("clip '" + name + "': root_motion is negative");
                    }
                }
                if (settings.ground_speed < 0.0f || settings.fade_ticks < -1 || settings.phase < 0.0f ||
                    settings.phase >= 1.0f) {
                    throw std::runtime_error("clip '" + name +
                                             "': ground_speed and fade_ticks must be positive, phase in [0, 1)");
                }
                if (clip.contains("events")) {
                    for (const nlohmann::json& event : clip.at("events")) {
                        ClipEvent parsed;
                        parsed.time = event.at("time").get<float>();
                        parsed.name = event.at("name").get<std::string>();
                        parsed.always = event.value("always", false);
                        parsed.end = event.value("end", -1.0f);
                        if (parsed.time < 0.0f || parsed.name.empty()) {
                            throw std::runtime_error("clip '" + name + "': an event needs a name and a time >= 0");
                        }
                        if (event.contains("end") && parsed.end <= parsed.time) {
                            throw std::runtime_error("clip '" + name + "': event '" + parsed.name + "' ends before it starts");
                        }
                        settings.events.push_back(std::move(parsed));
                    }
                    std::stable_sort(settings.events.begin(), settings.events.end(),
                                     [](const ClipEvent& a, const ClipEvent& b) { return a.time < b.time; });
                }
                set.clips_[name] = std::move(settings);
            }
        }
        if (doc.contains("masks")) {
            for (const auto& [name, mask] : doc.at("masks").items()) {
                MaskData data;
                data.joint = mask.at("joint").get<std::string>();
                data.ramp = mask.value("ramp", 0);
                if (data.ramp < 0) {
                    throw std::runtime_error("mask '" + name + "': negative ramp");
                }
                set.masks_[name] = data;
            }
        }
        set.upper_mask_ = doc.value("upper_mask", std::string());
        if (!set.upper_mask_.empty() && set.masks_.count(set.upper_mask_) == 0) {
            throw std::runtime_error("upper_mask '" + set.upper_mask_ + "' is not in masks");
        }
        // The layers (milestone 9 bis, part 3); without them, the whole body and the upper body.
        if (doc.contains("layers")) {
            const nlohmann::json& layers = doc.at("layers");
            if (!layers.is_array() || layers.empty() || layers.size() > static_cast<std::size_t>(kMaxLayers)) {
                throw std::runtime_error("layers: 1 to " + std::to_string(kMaxLayers) + " layers");
            }
            for (const nlohmann::json& l : layers) {
                LayerSettings layer;
                layer.name = l.value("name", std::string());
                layer.mask = l.value("mask", std::string());
                layer.additive = l.value("additive", false);
                if (!layer.mask.empty() && set.masks_.count(layer.mask) == 0) {
                    throw std::runtime_error("layer '" + layer.name + "': mask '" + layer.mask + "' is not in masks");
                }
                set.layers_.push_back(std::move(layer));
            }
            if (set.layers_.front().additive || !set.layers_.front().mask.empty()) {
                throw std::runtime_error("layers: the first one moves the whole body (no mask, not additive)");
            }
        } else {
            set.layers_ = {LayerSettings{"body", {}, false}, LayerSettings{"upper", set.upper_mask_, false}};
        }
        set.max_motions_ = doc.value("max_motions", 4);
        if (set.max_motions_ < 1 || set.max_motions_ > 16) {
            throw std::runtime_error("max_motions: 1 to 16");
        }
        set.root_joint_ = doc.value("root_joint", std::string("root"));
        if (doc.contains("attach_points")) {
            for (const auto& [name, point] : doc.at("attach_points").items()) {
                AttachPoint data;
                data.joint = point.at("joint").get<std::string>();
                if (point.contains("position")) {
                    const auto p = point.at("position").get<std::vector<float>>();
                    if (p.size() != 3) {
                        throw std::runtime_error("attach point '" + name + "': position needs 3 numbers");
                    }
                    data.position = {p[0], p[1], p[2]};
                }
                if (point.contains("rotation")) {
                    const auto r = point.at("rotation").get<std::vector<float>>();
                    if (r.size() != 3) {
                        throw std::runtime_error("attach point '" + name + "': rotation needs 3 angles");
                    }
                    // X, then Y, then Z (each about the fixed axes).
                    data.rotation = glm::angleAxis(glm::radians(r[2]), glm::vec3(0.0f, 0.0f, 1.0f)) *
                                    glm::angleAxis(glm::radians(r[1]), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                    glm::angleAxis(glm::radians(r[0]), glm::vec3(1.0f, 0.0f, 0.0f));
                }
                set.attach_points_[name] = data;
            }
        }
        if (doc.contains("blend_spaces")) {
            for (const auto& [name, clips] : doc.at("blend_spaces").items()) {
                std::vector<std::string> names = clips.get<std::vector<std::string>>();
                if (names.size() < 2) {
                    throw std::runtime_error("blend space '" + name + "' needs two clips at least");
                }
                std::vector<std::size_t> order(names.size());
                std::iota(order.begin(), order.end(), std::size_t{0});
                std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    return set.clip(names[a]).ground_speed < set.clip(names[b]).ground_speed;
                });
                BlendSpaceData data;
                for (const std::size_t i : order) {
                    const float position = set.clip(names[i]).ground_speed;
                    if (!data.positions.empty() && position == data.positions.back()) {
                        throw std::runtime_error("blend space '" + name + "': '" + data.clips.back() + "' and '" + names[i] +
                                                 "' have the same ground speed");
                    }
                    data.clips.push_back(names[i]);
                    data.positions.push_back(position);
                }
                set.blend_spaces_[name] = std::move(data);
            }
        }
        if (doc.contains("graph")) {
            set.graph_ = AnimationGraph::parse(doc.at("graph"));
            if (set.graph_.layers.size() > set.layers_.size()) {
                throw std::runtime_error("graph: " + std::to_string(set.graph_.layers.size()) + " layers for " +
                                         std::to_string(set.layers_.size()) + " layers of the animator");
            }
            // Every motion of a state is a clip (any name: the library is checked when it plays) or a
            // blend space of this file; a blend space's name is only known here.
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Animation set '" + source + "' is not valid: " + e.what());
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Animation set '" + source + "': " + e.what());
    }
    return set;
}

const ClipSettings& AnimationSet::clip(const std::string& name) const {
    static const ClipSettings defaults;
    const auto found = clips_.find(name);
    return found != clips_.end() ? found->second : defaults;
}

int AnimationSet::fade_ticks(const std::string& clip) const {
    const int own = this->clip(clip).fade_ticks;
    return own >= 0 ? own : default_fade_ticks_;
}

const BlendSpaceData* AnimationSet::blend_space(const std::string& name) const {
    const auto found = blend_spaces_.find(name);
    return found != blend_spaces_.end() ? &found->second : nullptr;
}

const AttachPoint* AnimationSet::attach_point(const std::string& name) const {
    const auto found = attach_points_.find(name);
    return found != attach_points_.end() ? &found->second : nullptr;
}

glm::mat4 AttachPoint::offset() const {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
}

const MaskData* AnimationSet::mask(const std::string& name) const {
    const auto found = masks_.find(name);
    return found != masks_.end() ? &found->second : nullptr;
}

}  // namespace moteur
