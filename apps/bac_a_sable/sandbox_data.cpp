#include "sandbox_data.hpp"

namespace {

CharacterData parse_character(moteur::DataReader& row) {
    CharacterData c;
    c.name = row.text("nom");
    c.model = row.text("modele");
    c.height = row.number("taille", 0.1f, 20.0f);
    c.radius = row.number("rayon", 0.05f, 5.0f);
    c.speed = row.number("vitesse", 0.0f, 30.0f);
    c.weight = row.integer("poids", 0, 100);
    c.life = row.integer("vie", 1, 1000000);
    c.damage = row.integer("degats", 0, 1000000);
    c.reach = row.number("portee", 0.0f, 50.0f);
    c.sight = row.number("vue", 0.0f, 100.0f, 0.0f);
    c.cadence = row.number("cadence", 0.05f, 60.0f, 1.0f);
    c.hit_effect = row.text("effet_touche", std::string());
    c.death_effect = row.text("effet_mort", std::string());
    if (!c.model.ends_with(".glb") && !c.model.ends_with(".gltf")) {
        row.error("modele", "attendu un fichier .glb ou .gltf");
    }
    for (const auto& [field, path] : {std::pair{"effet_touche", &c.hit_effect}, std::pair{"effet_mort", &c.death_effect}}) {
        if (!path->empty() && (!path->starts_with("effects/") || !path->ends_with(".json"))) {
            row.error(field, "attendu un effet de assets/effects (effects/nom.json)");
        }
    }
    return c;
}

}  // namespace

void register_sandbox_tables(moteur::DataTables& tables) {
    tables.add<CharacterData>("personnages", "data/personnages", parse_character);
}
