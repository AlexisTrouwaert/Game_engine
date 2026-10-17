// The playable slice as a program of its own (milestone 8, part 12): what the ARPG's executable
// will look like. Title, loading, the game, pause, saves; no test scene. In the shipping
// configuration (MOTEUR_SHIPPING) it has no Dear ImGui, no hot reload, no profiler, and reads its
// assets prepared and packed (tables compiled, textures in BC7 / BC5, archives).
//
//   tranche [--run-seconds S] [--no-vsync] [--pixel-size W H] [+set NAME VALUE]
//           [--replay-input FILE] [--freeze-after TICKS --capture FILE.png] [--states-cycles N]
//           [--report-json FILE] [--test-plantage acces|abort|exception|assertion|gpu]
//
// The options after the first line are for the checks of the delivery (the same reference capture
// as the sandbox's slice, measures, the crash reports); a player never needs them.

#include <SDL3/SDL.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/crash.hpp"
#include "moteur/paths.hpp"
#include "moteur/version.hpp"

#include "sandbox_data.hpp"
#include "states_demo.hpp"

int main(int argc, char* argv[]) {
    StatesDemo::Options options;
    moteur::ApplicationConfig config;
    config.title = "Tranche jouable";
    config.organization = "moteur";
    config.application = "tranche";  // the player's files: SDL_GetPrefPath("moteur", "tranche")
    config.display_settings = true;  // the player's display options (display.json)
    // What the player reads after a crash, in the game's language (the report itself is in English).
    config.crash_message =
        "Le jeu a rencontré une erreur et doit se fermer.\n\nUn rapport a été écrit ici :\n{report}\n\n"
        "Joindre ce fichier (et le .dmp voisin) à un signalement aide à corriger le problème.";
    config.gpu_lost_message =
        "La carte graphique ne répond plus (son pilote a été réinitialisé, ou la carte retirée) et le jeu doit se "
        "fermer.\n\nMettre à jour le pilote graphique corrige le plus souvent le problème.\n\nUn rapport a été écrit ici :\n{report}";
    long freeze_after = 0;
    std::string crash_test;
    std::string capture;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_one = i + 1 < argc;
        if (arg == "--run-seconds" && has_one) {
            options.run_seconds = std::atof(argv[++i]);
        } else if (arg == "--no-vsync") {
            config.vsync = false;
        } else if (arg == "--pixel-size" && i + 2 < argc) {
            config.pixel_width = std::atoi(argv[i + 1]);
            config.pixel_height = std::atoi(argv[i + 2]);
            i += 2;
        } else if (arg == "+set" && i + 2 < argc) {
            config.startup_commands.push_back("set " + std::string(argv[i + 1]) + " \"" + std::string(argv[i + 2]) + "\"");
            i += 2;
        } else if (arg == "--replay-input" && has_one) {
            config.replay_input_path = argv[++i];
        } else if (arg == "--freeze-after" && has_one) {
            freeze_after = std::atol(argv[++i]);
        } else if (arg == "--capture" && has_one) {
            capture = argv[++i];
        } else if (arg == "--states-cycles" && has_one) {
            options.cycles = std::atoi(argv[++i]);
        } else if (arg == "--test-plantage" && has_one) {  // acces | abort | exception | assertion | gpu: checks the crash reports
            crash_test = argv[++i];
        } else if (arg == "--report-json" && has_one) {
            config.report_performance = true;
            config.report_json_path = argv[++i];
        } else {
            std::cerr << "tranche : option inconnue « " << arg << " »\n";
            return 2;
        }
    }
    // A check (a replay compared with a reference capture) must not depend on the player's settings.
    if (!config.replay_input_path.empty()) {
        config.display_settings = false;
    }
    std::cout << moteur::build_description() << '\n';
    try {
#if !defined(MOTEUR_SHIPPING) && defined(MOTEUR_ASSETS_SOURCE_DIR)
        config.assets_source_directory = MOTEUR_ASSETS_SOURCE_DIR;  // development: hot reload
#endif
        moteur::Application app(config);
        if (crash_test == "gpu") {
            app.renderer().simulate_device_lost();  // reported at the first frame, then a clean end
        } else if (!crash_test.empty()) {
            moteur::crash_now(crash_test == "abort"       ? moteur::CrashKind::Abort
                              : crash_test == "exception" ? moteur::CrashKind::UncaughtException
                              : crash_test == "assertion" ? moteur::CrashKind::Assertion
                                                          : moteur::CrashKind::AccessViolation);
        }
        register_sandbox_tables(app.data());
#if defined(MOTEUR_SHIPPING)
        // The shipped game reads its tables compiled (prepared by tranche_cook).
        app.data().set_source(moteur::DataSource::Compiled, app.assets().root() + "data/");
#endif
        if (const moteur::DataIssues& issues = app.data().load_all(app.assets().root()); issues.has_errors()) {
            throw std::runtime_error("données invalides :\n" + issues.text());
        }
        options.capture_path = capture;
        if (freeze_after > 0) {
            options.demo.freeze_after_ticks = freeze_after;
            options.demo.capture_path = capture;
        }
        StatesDemo game(app, options, true);
        app.run(game);
        if (app.ended_on_error()) {
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tranche jouable", e.what(), nullptr);
        return 1;
    }
    return 0;
}
