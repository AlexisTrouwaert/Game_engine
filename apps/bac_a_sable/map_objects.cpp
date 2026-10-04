#include "map_objects.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

#include "moteur/application.hpp"
#include "moteur/color.hpp"
#include "moteur/world_save.hpp"

namespace {

using Kind = ObjectProperty::Kind;

std::vector<ObjectType> make_types() {
    std::vector<ObjectType> types;
    types.push_back({"rocher", "Rocher", {0.5f, 0.5f, 0.48f}, false,
                     {{"echelle", "Échelle (x, y, z)", Kind::Vec3, {0.6, 0.45, 0.6}, 0.05f, 5.0f}}});
    types.push_back({"arbre", "Arbre", {0.22f, 0.42f, 0.18f}, false,
                     {{"hauteur", "Hauteur du tronc (m)", Kind::Number, 1.4, 0.2f, 10.0f},
                      {"couronne", "Couronne (m)", Kind::Number, 1.2, 0.2f, 10.0f}}});
    types.push_back({"caisse", "Caisse", {0.6f, 0.44f, 0.26f}, false,
                     {{"arete", "Arête (m)", Kind::Number, 0.6, 0.1f, 3.0f}}});
    types.push_back({"tonneau", "Tonneau", {0.45f, 0.3f, 0.18f}, false,
                     {{"arete", "Arête si le modèle manque (m)", Kind::Number, 0.6, 0.1f, 3.0f}}});
    types.push_back({"monstre", "Monstre (apparition)", {0.9f, 0.2f, 0.15f}, false,
                     {{"personnage", "Personnage (table personnages)", Kind::Character, "squelette_guerrier"},
                      {"rythme", "Rythme", Kind::Number, 1.0, 0.1f, 3.0f},
                      {"direction", "Direction d'errance", Kind::Vec2, {1.0, 0.0}, -1.0f, 1.0f},
                      {"vie", "Vie au départ (0 à 1)", Kind::Number, 1.0, 0.0f, 1.0f},
                      {"couleur", "Couleur (sans modèle animé)", Kind::Color, {0.6, 0.55, 0.5}}}});
    types.push_back({"lumiere", "Lumière ponctuelle", {1.0f, 0.8f, 0.4f}, false,
                     {{"couleur", "Couleur", Kind::Color, {1.0, 0.6, 0.3}},
                      {"intensite", "Intensité", Kind::Number, 5.0, 0.0f, 100.0f},
                      {"portee", "Portée (m)", Kind::Number, 7.0, 0.5f, 50.0f},
                      {"ombres", "Ombres", Kind::Bool, false}}});
    types.push_back({"effet", "Effet de particules", {0.4f, 0.7f, 1.0f}, false,
                     {{"effet", "Fichier d'effet", Kind::Effect, "effects/fire.json"}}});
    types.push_back({"marqueur", "Marqueur", {1.0f, 1.0f, 1.0f}, false, {{"nom", "Nom", Kind::Text, ""}}});
    return types;
}

}  // namespace

const std::vector<ObjectType>& object_types() {
    static const std::vector<ObjectType> types = make_types();
    return types;
}

const ObjectType* find_object_type(const std::string& name) {
    for (const ObjectType& type : object_types()) {
        if (type.name == name) {
            return &type;
        }
    }
    return nullptr;
}

nlohmann::json object_props(const ObjectType& type, const nlohmann::json& props) {
    nlohmann::json result = props.is_object() ? props : nlohmann::json::object();
    for (const ObjectProperty& property : type.properties) {
        if (!result.contains(property.key)) {
            result[property.key] = property.value;
        }
    }
    return result;
}

ObjectMeshes make_object_meshes(moteur::Application& app) {
    ObjectMeshes meshes;
    moteur::Renderer& renderer = app.renderer();
    meshes.cube = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_cube(), "objects.cube"));
    meshes.sphere = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_sphere(0.5f, 16, 8), "objects.sphere"));
    meshes.rock = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_sphere(0.5f, 7, 4), "objects.rock"));
    const char* const barrel = "models/polyhaven/wine_barrel_01/wine_barrel_01_1k.gltf";
    if (app.assets().exists(barrel)) {
        meshes.barrel = app.assets().model(barrel);
    }
    return meshes;
}

namespace {

moteur::Material surface(glm::vec3 srgb, float roughness, bool selected) {
    moteur::Material material;
    material.base_color = glm::vec4(moteur::srgb_to_linear(srgb), 1.0f);
    material.roughness = roughness;
    if (selected) {
        material.emissive = glm::vec3(0.6f, 0.45f, 0.1f);
    }
    return material;
}

moteur::Transform placed(glm::vec3 position, glm::vec3 scale, float degrees) {
    moteur::Transform t;
    t.position = position;
    t.rotation = glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    t.scale = scale;
    return t;
}

}  // namespace

std::vector<entt::entity> build_object(moteur::World& world, const moteur::MapObject& object, const ObjectMeshes& meshes,
                                       bool selected) {
    std::vector<entt::entity> entities;
    entt::registry& registry = world.registry();
    const ObjectType* type = find_object_type(object.type);
    const nlohmann::json props = type != nullptr ? object_props(*type, object.props) : object.props;
    const auto mesh = [&](const moteur::Asset<moteur::Mesh>& m, const moteur::Transform& t, const moteur::Material& material) {
        const entt::entity e = registry.create();
        registry.emplace<moteur::Transform>(e, t);
        registry.emplace<moteur::MeshComponent>(e, m, material);
        entities.push_back(e);
    };
    const glm::vec3 p = object.position;
    const float s = object.scale;
    try {
        if (object.type == "rocher") {
            const glm::vec3 scale = moteur::vec3_from_json(props.at("echelle")) * s;
            mesh(meshes.rock, placed(p + glm::vec3(0.0f, scale.y * 0.3f, 0.0f), scale, object.rotation), surface({0.5f, 0.5f, 0.48f}, 0.9f, selected));
        } else if (object.type == "arbre") {
            const float height = props.at("hauteur").get<float>() * s;
            const float crown = props.at("couronne").get<float>() * s;
            moteur::Material bark = surface({0.35f, 0.24f, 0.15f}, 0.9f, selected);
            moteur::Material leaves = surface({0.22f, 0.42f, 0.18f}, 0.8f, selected);
            bark.fades = leaves.fades = true;
            mesh(meshes.cube, placed(p + glm::vec3(0.0f, height * 0.5f, 0.0f), {0.16f, height, 0.16f}, object.rotation), bark);
            mesh(meshes.sphere, placed(p + glm::vec3(0.0f, height + crown * 0.3f, 0.0f), glm::vec3(crown, crown * 0.85f, crown), 0.0f), leaves);
        } else if (object.type == "caisse" || (object.type == "tonneau" && !meshes.barrel)) {
            const float edge = props.at("arete").get<float>() * s;
            mesh(meshes.cube, placed(p + glm::vec3(0.0f, edge * 0.5f, 0.0f), glm::vec3(edge), object.rotation), surface({0.6f, 0.44f, 0.26f}, 0.7f, selected));
        } else if (object.type == "tonneau") {
            const entt::entity e = registry.create();
            registry.emplace<moteur::Transform>(e, placed(p, glm::vec3(s), object.rotation));
            registry.emplace<moteur::ModelComponent>(e, meshes.barrel);
            entities.push_back(e);
        } else if (object.type == "lumiere") {
            const entt::entity e = registry.create();
            registry.emplace<moteur::Transform>(e, placed(p + glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0.15f), 0.0f));
            registry.emplace<moteur::LightSource>(e, moteur::vec3_from_json(props.at("couleur")), props.at("intensite").get<float>(),
                                                  props.at("portee").get<float>(), props.at("ombres").get<bool>());
            moteur::Material glow;
            glow.base_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            glow.emissive = moteur::vec3_from_json(props.at("couleur")) * 6.0f;
            glow.casts_shadow = false;
            registry.emplace<moteur::MeshComponent>(e, meshes.sphere, glow);
            entities.push_back(e);
        } else if (object.type == "marqueur") {
            moteur::Material glow;
            glow.base_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            glow.emissive = glm::vec3(1.2f, 1.2f, 1.2f) * (selected ? 1.5f : 1.0f);
            glow.casts_shadow = false;
            mesh(meshes.cube, placed(p + glm::vec3(0.0f, 0.05f, 0.0f), {0.3f, 0.1f, 0.3f}, object.rotation), glow);
        }
    } catch (...) {
        registry.destroy(entities.begin(), entities.end());  // nothing half built stays behind
        throw;
    }
    return entities;
}

float object_height(const moteur::MapObject& object) {
    const nlohmann::json& p = object.props;
    if (object.type == "arbre") {
        return p.value("hauteur", 1.4f) + p.value("couronne", 1.2f) * 0.8f;
    }
    if (object.type == "rocher" && p.contains("echelle")) {
        return p["echelle"].at(1).get<float>() * 0.6f;
    }
    if (object.type == "caisse" || object.type == "tonneau") {
        return p.value("arete", 0.6f) * object.scale;
    }
    if (object.type == "monstre") {
        return 1.5f;
    }
    return 0.6f;
}
