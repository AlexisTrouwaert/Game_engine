#include "moteur/material.hpp"

#include <nlohmann/json.hpp>

#include <set>
#include <stdexcept>

namespace moteur {

namespace {

glm::vec4 read_vec(const nlohmann::json& value, std::size_t size, const std::string& where) {
    if (!value.is_array() || value.size() != size) {
        throw std::runtime_error(where + ": " + std::to_string(size) + " numbers expected");
    }
    glm::vec4 out(0.0f, 0.0f, 0.0f, 1.0f);
    for (std::size_t i = 0; i < size; ++i) {
        if (!value[i].is_number()) {
            throw std::runtime_error(where + ": " + std::to_string(size) + " numbers expected");
        }
        out[static_cast<int>(i)] = value[i].get<float>();
    }
    return out;
}

float read_number(const nlohmann::json& value, const std::string& where, float min, float max) {
    if (!value.is_number()) {
        throw std::runtime_error(where + ": a number expected");
    }
    const auto v = value.get<float>();
    if (!(v >= min && v <= max)) {
        throw std::runtime_error(where + ": " + std::to_string(v) + " out of [" + std::to_string(min) + ", " + std::to_string(max) + "]");
    }
    return v;
}

bool read_bool(const nlohmann::json& value, const std::string& where) {
    if (!value.is_boolean()) {
        throw std::runtime_error(where + ": true or false expected");
    }
    return value.get<bool>();
}

}  // namespace

Material parse_material(std::string_view text, const std::string& name, const MaterialTextureLoader& texture) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(name + ": " + e.what());
    }
    if (!root.is_object()) {
        throw std::runtime_error(name + ": a JSON object expected");
    }
    static const std::set<std::string, std::less<>> kKeys = {
        "version", "base_color", "base_color_texture", "metallic", "roughness", "metallic_roughness_texture",
        "normal_texture", "normal_scale", "occlusion_texture", "occlusion_strength", "emissive", "emissive_strength",
        "emissive_texture", "alpha", "alpha_cutoff", "double_sided", "casts_shadow", "fades", "uv_scroll", "emissive_pulse"};
    for (const auto& [key, value] : root.items()) {
        if (!kKeys.contains(key)) {
            std::string message = name;
            message += ": unknown key '";
            message += key;
            message += "'";
            throw std::runtime_error(message);
        }
    }
    if (root.contains("version") && root["version"] != 1) {
        throw std::runtime_error(name + ": version " + root["version"].dump() + " (expected 1)");
    }
    const auto at = [&name](const char* key) { return name + " > " + key; };
    Material m;
    if (root.contains("base_color")) {
        const nlohmann::json& c = root["base_color"];
        m.base_color = read_vec(c, c.is_array() && c.size() == 3 ? 3 : 4, at("base_color"));
    }
    if (root.contains("metallic")) m.metallic = read_number(root["metallic"], at("metallic"), 0.0f, 1.0f);
    if (root.contains("roughness")) m.roughness = read_number(root["roughness"], at("roughness"), 0.0f, 1.0f);
    if (root.contains("normal_scale")) m.normal_scale = read_number(root["normal_scale"], at("normal_scale"), -10.0f, 10.0f);
    if (root.contains("occlusion_strength")) {
        m.occlusion_strength = read_number(root["occlusion_strength"], at("occlusion_strength"), 0.0f, 1.0f);
    }
    if (root.contains("emissive")) m.emissive = glm::vec3(read_vec(root["emissive"], 3, at("emissive")));
    if (root.contains("emissive_strength")) {
        m.emissive *= read_number(root["emissive_strength"], at("emissive_strength"), 0.0f, 1000.0f);
    }
    const std::string alpha = root.value("alpha", std::string("opaque"));
    if (alpha == "mask") {
        m.alpha_cutoff = root.contains("alpha_cutoff") ? read_number(root["alpha_cutoff"], at("alpha_cutoff"), 1.0f / 512.0f, 1.0f) : 0.5f;
    } else if (alpha == "blend") {
        m.blend = true;
    } else if (alpha != "opaque") {
        throw std::runtime_error(at("alpha") + ": opaque, mask or blend expected, not '" + alpha + "'");
    }
    if (alpha != "mask" && root.contains("alpha_cutoff")) {
        throw std::runtime_error(at("alpha_cutoff") + ": only for \"alpha\": \"mask\"");
    }
    m.casts_shadow = !m.blend;
    if (root.contains("double_sided")) m.double_sided = read_bool(root["double_sided"], at("double_sided"));
    if (root.contains("casts_shadow")) m.casts_shadow = read_bool(root["casts_shadow"], at("casts_shadow")) && !m.blend;
    if (root.contains("fades")) m.fades = read_bool(root["fades"], at("fades"));
    if (root.contains("uv_scroll")) m.uv_scroll = glm::vec2(read_vec(root["uv_scroll"], 2, at("uv_scroll")));
    if (root.contains("emissive_pulse")) m.emissive_pulse = read_number(root["emissive_pulse"], at("emissive_pulse"), 0.0f, 60.0f);

    const auto load = [&](const char* key, MaterialTexture role) -> Asset<Texture> {
        if (!root.contains(key) || root[key].is_null()) {
            return {};
        }
        try {
            return texture(root[key], role);
        } catch (const std::exception& e) {
            throw std::runtime_error(at(key) + ": " + e.what());
        }
    };
    m.base_color_texture = load("base_color_texture", MaterialTexture::Color);
    m.metallic_roughness_texture = load("metallic_roughness_texture", MaterialTexture::Data);
    m.normal_texture = load("normal_texture", MaterialTexture::Normal);
    m.occlusion_texture = load("occlusion_texture", MaterialTexture::Data);
    m.emissive_texture = load("emissive_texture", MaterialTexture::Color);
    return m;
}

}  // namespace moteur
