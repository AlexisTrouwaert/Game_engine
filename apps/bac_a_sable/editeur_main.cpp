#include <SDL3/SDL.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "moteur/application.hpp"
#include "moteur/paths.hpp"

#include "map_editor.hpp"
#include "sandbox_data.hpp"

// The map editor as a program of its own (milestone 7, part 7): the same editor as the sandbox's
// DEBUG > Tests moteur > Éditeur de cartes, in a window of its own.
//
//   editeur [CARTE] [--top] [--run-seconds S] [--capture FICHIER.png]
//
// CARTE: a file of assets/maps without ".json" (default: salle_portes); "--new": a new map.
int main(int argc, char** argv) {
    MapEditor::Options options;
    bool vsync = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--run-seconds" && i + 1 < argc) {
            options.run_seconds = std::atof(argv[++i]);
        } else if (arg == "--capture" && i + 1 < argc) {
            options.capture_path = argv[++i];
        } else if (arg == "--self-test") {  // the editor's workflow, checked, on a copy of the map
            options.self_test = true;
        } else if (arg == "--no-vsync") {  // measures: frames as fast as they come
            vsync = false;
        } else if (arg == "--top") {
            options.top_view = true;
        } else if (arg == "--new") {
            options.map.clear();
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "editeur [CARTE | --new] [--top] [--run-seconds S] [--capture FICHIER.png] [--self-test] [--no-vsync]\n";
            return 0;
        } else if (!arg.empty() && arg[0] != '-') {
            options.map = arg;
        } else {
            std::cerr << "fatal: option inconnue " << arg << '\n';
            return 1;
        }
    }
    try {
        moteur::ApplicationConfig config;
        config.title = "editeur de cartes";
        config.application = "editeur";
        config.debug_ui = true;
        config.vsync = vsync;
        config.debug_ui_font = moteur::asset_path("fonts/Inter-Regular.ttf");  // accents
#ifdef MOTEUR_ASSETS_SOURCE_DIR
        if (std::filesystem::is_directory(std::filesystem::path(u8"" MOTEUR_ASSETS_SOURCE_DIR))) {
            config.assets_source_directory = MOTEUR_ASSETS_SOURCE_DIR;
        }
#endif
        moteur::Application app(config);
        // The tables: the object panel lists the characters of the "personnages" table.
        register_sandbox_tables(app.data());
        if (const moteur::DataIssues& issues = app.data().load_all(app.assets().root()); issues.has_errors()) {
            std::cerr << issues.text();
            std::cerr << "fatal: données invalides (" << issues.errors() << " erreur(s))\n";
            return 1;
        }
        if (options.self_test) {
            // The real maps are not touched: the test works on a copy, in the player's preferences.
            const std::string directory = app.preferences_directory() + "essai_editeur/";
            const auto path = [](const std::string& text) { return std::filesystem::path(std::u8string(text.begin(), text.end())); };
            std::filesystem::create_directories(path(directory));
            std::filesystem::copy_file(path(MapEditor::maps_directory(app) + options.map + ".json"),
                                       path(directory + options.map + ".json"), std::filesystem::copy_options::overwrite_existing);
            options.maps_directory = directory;
        }
        MapEditor editor(app, options, true);
        app.run(editor);
        if (options.self_test) {
            return editor.self_test_failures() == 0 ? 0 : 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
