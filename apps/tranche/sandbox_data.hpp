#pragma once

#include <string>

#include "moteur/data_table.hpp"
#include "moteur/reflection.hpp"

// The sandbox's data tables (milestone 7, part 2): what its scenes read from assets/data/. The
// engine knows none of these fields; each table is a struct, its description (milestone 9, part 3:
// the reflection reads the rows, the inspector shows them, the schema follows), and its registration.

// A character of the slice: the hero or a creature (assets/data/personnages/).
struct CharacterData {
    std::string name;    // shown to the player
    std::string model;   // glTF file of the assets
    float height = 1.8f;   // metres: the model is scaled to it
    float radius = 0.35f;  // metres: its Collider
    float speed = 3.0f;    // metres per second (the creatures' own pace multiplies it)
    int weight = 1;        // Collider::push_weight while it does not strike
    int life = 100;        // points: a blow takes damage / life of the bar
    int damage = 10;       // points per blow
    float reach = 1.0f;    // metres: how far its blows land (beyond touching, for the creatures)
    float sight = 0.0f;    // metres: how far it sees the hero (creatures)
    float cadence = 1.0f;  // seconds between two blows (creatures)
    // Milestone 7, part 11: the effects it plays (files of assets/effects; empty: none).
    std::string hit_effect;    // where a blow lands on it
    std::string death_effect;  // where it falls
};
// The table's keys are the game's (French).
MOTEUR_REFLECT(CharacterData,
    MOTEUR_FIELD_AS(name, "nom", {.doc = "shown to the player", .required = true}),
    MOTEUR_FIELD_AS(model, "modele", {.doc = "glTF file of the assets", .required = true}),
    MOTEUR_FIELD_AS(height, "taille", {.min = 0.1, .max = 20.0, .unit = "m", .required = true}),
    MOTEUR_FIELD_AS(radius, "rayon", {.min = 0.05, .max = 5.0, .unit = "m", .doc = "its Collider", .required = true}),
    MOTEUR_FIELD_AS(speed, "vitesse", {.min = 0.0, .max = 30.0, .unit = "m/s", .required = true}),
    MOTEUR_FIELD_AS(weight, "poids", {.min = 0, .max = 100, .doc = "Collider::push_weight while it does not strike",
                                      .required = true}),
    MOTEUR_FIELD_AS(life, "vie", {.min = 1, .max = 1000000, .required = true}),
    MOTEUR_FIELD_AS(damage, "degats", {.min = 0, .max = 1000000, .doc = "points per blow", .required = true}),
    MOTEUR_FIELD_AS(reach, "portee", {.min = 0.0, .max = 50.0, .unit = "m", .required = true}),
    MOTEUR_FIELD_AS(sight, "vue", {.min = 0.0, .max = 100.0, .unit = "m", .doc = "how far it sees the hero"}),
    MOTEUR_FIELD_AS(cadence, "cadence", {.min = 0.05, .max = 60.0, .unit = "s", .doc = "between two blows"}),
    MOTEUR_FIELD_AS(hit_effect, "effet_touche", {.doc = "effect where a blow lands on it"}),
    MOTEUR_FIELD_AS(death_effect, "effet_mort", {.doc = "effect where it falls"}));

// Registers every table of the sandbox into `tables` (before DataTables::load_all).
void register_sandbox_tables(moteur::DataTables& tables);
