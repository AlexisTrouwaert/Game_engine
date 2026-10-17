#pragma once

#include <string>

#include "moteur/data_table.hpp"

// The sandbox's data tables (milestone 7, part 2): what its scenes read from assets/data/. The
// engine knows none of these fields; each table is a struct, a function that reads a row, and its
// registration.

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

// Registers every table of the sandbox into `tables` (before DataTables::load_all).
void register_sandbox_tables(moteur::DataTables& tables);
