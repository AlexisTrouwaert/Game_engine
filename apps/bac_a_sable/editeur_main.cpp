#include <SDL3/SDL.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "moteur/application.hpp"
#include "moteur/paths.hpp"

#include "level_editor.hpp"
#include "map_editor.hpp"
#include "sandbox_data.hpp"

// The editors as a program of their own: the level editor (milestone 9, part 10) and the map editor
// of milestone 7 (the maps of versions 1 and 2), the same as the sandbox's DEBUG > Engine tests.
//
//   editeur --level [KEY] [--run-seconds S] [--capture FILE.png] [--self-test] [--no-vsync]
//   editeur --new-level
//   editeur [MAP | --new] [--top] [--run-seconds S] [--capture FILE.png] [--self-test] [--no-vsync]
//
// KEY: a level of the assets ("levels/tranche.level.json"; default levels/editeur_essai.level.json).
// MAP: a file of assets/maps without ".json" (default: salle_portes); "--new": a new map.
// "--self-test": the editor's workflow, checked, on a copy (of the map; a new level for the level
// editor); "--no-vsync": frames as fast as they come (measures).
int main(int argc, char** argv) {
    MapEditor::Options options;
    LevelEditor::Options level_options;
    bool level_editor = false;
    bool vsync = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--level") {  // the level editor, on a level of the assets if one follows
            level_editor = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                level_options.level = argv[++i];
            }
        } else if (arg == "--focus" && i + 2 < argc) {  // the level editor's view framed at X Z
            level_options.focus = glm::vec3(static_cast<float>(std::atof(argv[i + 1])), 0.0f, static_cast<float>(std::atof(argv[i + 2])));
            i += 2;
        } else if (arg == "--focus-height" && i + 1 < argc) {
            level_options.focus_height = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--show-collision") {
            level_options.show_collision = true;
        } else if (arg == "--new-level") {
            level_editor = true;
            level_options.level.clear();
        } else if (arg == "--run-seconds" && i + 1 < argc) {
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
            std::cout << "editeur --level [KEY] | --new-level [--run-seconds S] [--capture FILE.png] [--self-test] [--no-vsync]\n"
                         "editeur [MAP | --new] [--top] [--run-seconds S] [--capture FILE.png] [--self-test] [--no-vsync]\n";
            return 0;
        } else if (!arg.empty() && arg[0] != '-') {
            options.map = arg;
        } else {
            std::cerr << "fatal: unknown option " << arg << '\n';
            return 1;
        }
    }
    try {
        moteur::ApplicationConfig config;
        config.title = level_editor ? "Level editor" : "Map editor";
        config.application = "editeur";
        config.debug_ui = true;
        config.vsync = vsync;
        config.debug_ui_font = moteur::asset_path("fonts/Inter-Regular.ttf");  // accents
#ifdef MOTEUR_ASSETS_SOURCE_DIR
        if (std::filesystem::is_directory(std::filesystem::path(u8"" MOTEUR_ASSETS_SOURCE_DIR))) {
            config.assets_source_directory = MOTEUR_ASSETS_SOURCE_DIR;
        }
#endif
#if defined(MOTEUR_ARPG_ASSETS_DIR)
        config.asset_folders.push_back(MOTEUR_ARPG_ASSETS_DIR);  // the game's assets: its kits, props, grounds
#endif
        moteur::Application app(config);
        // The tables: the object panel lists the characters of the "personnages" table.
        register_sandbox_tables(app.data());
        if (const moteur::DataIssues& issues = app.data().load_all(app.assets().root()); issues.has_errors()) {
            std::cerr << issues.text();
            std::cerr << "fatal: invalid data (" << issues.errors() << " error(s))\n";
            return 1;
        }
        if (level_editor) {
            level_options.run_seconds = options.run_seconds;
            level_options.capture_path = options.capture_path;
            level_options.self_test = options.self_test;
            if (options.self_test) {
                // The real levels are not touched: the test writes its level in the player's preferences.
                const std::string directory = app.preferences_directory() + "essai_editeur_niveaux/";
                std::filesystem::create_directories(std::filesystem::path(std::u8string(directory.begin(), directory.end())));
                level_options.levels_directory = directory;
                level_options.level.clear();
            }
            LevelEditor editor(app, level_options, true);
            app.run(editor);
            return options.self_test && editor.self_test_failures() != 0 ? 1 : 0;
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
