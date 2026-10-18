#include "sandbox_data.hpp"

#include <utility>

#include "moteur/reflection_io.hpp"

void register_sandbox_tables(moteur::DataTables& tables) {
    // Read by the description; what it cannot say is checked here.
    moteur::add_reflected_table<CharacterData>(tables, "personnages", "data/personnages",
                                               [](CharacterData& c, moteur::DataReader& row) {
        if (!c.model.ends_with(".glb") && !c.model.ends_with(".gltf")) {
            row.error("modele", "expected a .glb or .gltf file");
        }
        for (const auto& [field, path] : {std::pair{"effet_touche", &c.hit_effect}, std::pair{"effet_mort", &c.death_effect}}) {
            if (!path->empty() && (!path->starts_with("effects/") || !path->ends_with(".json"))) {
                row.error(field, "expected an effect from assets/effects (effects/name.json)");
            }
        }
    });
}
