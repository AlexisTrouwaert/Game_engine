#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/map_data.hpp"
#include "moteur/mesh.hpp"
#include "moteur/model.hpp"
#include "moteur/world.hpp"

// The types of objects the sandbox's maps can hold (milestone 7, part 8): what the map editor
// offers, the properties it edits, and how the scenes see them. The engine stores objects as
// type + properties (MapObject); these types are the game's.
struct ObjectProperty {
    enum class Kind { Number, Integer, Text, Vec2, Vec3, Color, Bool, Character, Effect };
    std::string key;    // in the file's "props"
    std::string label;  // in the editor
    Kind kind = Kind::Number;
    nlohmann::json value;  // default
    float min = 0.0f, max = 100.0f;
};

struct ObjectType {
    std::string name;   // in the file
    std::string label;  // in the editor
    glm::vec3 color{1.0f};  // of its marker in the editor (linear)
    bool blocks_cell = false;  // its cell is not walkable (decor that stands in the way)
    std::vector<ObjectProperty> properties;
};

// Every type, in the order the editor shows them.
const std::vector<ObjectType>& object_types();
const ObjectType* find_object_type(const std::string& name);
// The properties of `type` with their defaults, completed by those `props` already has.
nlohmann::json object_props(const ObjectType& type, const nlohmann::json& props = nlohmann::json::object());
// The height of an object's box (for picking in the editor), metres.
float object_height(const moteur::MapObject& object);

// What the decor objects are drawn with (the scenes create these once).
struct ObjectMeshes {
    moteur::Asset<moteur::Mesh> cube;
    moteur::Asset<moteur::Mesh> sphere;
    moteur::Asset<moteur::Mesh> rock;    // faceted sphere
    moteur::Asset<moteur::Model> barrel;  // empty: crates instead
};
ObjectMeshes make_object_meshes(moteur::Application& app);

// The entities that show an object in `world`: decor as meshes, a light as a LightSource, a marker
// as a small glowing square. Monsters and effects are the scenes' own (they return nothing here).
// `selected`: drawn glowing (the editor's selection).
std::vector<entt::entity> build_object(moteur::World& world, const moteur::MapObject& object, const ObjectMeshes& meshes,
                                       bool selected = false);
