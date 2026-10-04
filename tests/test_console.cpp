#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "moteur/console.hpp"
#include "moteur/input.hpp"
#include "moteur/log_file.hpp"

// Milestone 7, part 3: console, variables, game commands in recordings.

TEST_CASE("Console::split keeps quoted words and reports an unclosed quote") {
    std::vector<std::string> words;
    std::string error;
    REQUIRE(moteur::Console::split(R"(spawn  "squelette guerrier" 5)", words, error));
    CHECK(words == std::vector<std::string>{"spawn", "squelette guerrier", "5"});
    REQUIRE(moteur::Console::split(R"(echo "")", words, error));
    CHECK(words == std::vector<std::string>{"echo", ""});
    CHECK_FALSE(moteur::Console::split(R"(echo "oups)", words, error));
    CHECK(error == "guillemet non fermé");
}

TEST_CASE("Variables are typed, bounded, and parsed from text") {
    moteur::Variables vars;
    moteur::Variable& fog = vars.add_bool("fog.enabled", true, "brouillard");
    moteur::Variable& count = vars.add_int("debug.count", 3, 0, 10, "compte");
    moteur::Variable& scale = vars.add_float("time.scale", 1.0f, 0.1f, 4.0f, "temps");
    moteur::Variable& name = vars.add_text("player.name", "Héros", "nom");
    std::string error;
    CHECK(fog.set_text("off", error));
    CHECK_FALSE(fog.as_bool());
    CHECK_FALSE(fog.set_text("peut-être", error));
    CHECK(count.set_text("7", error));
    CHECK(count.as_int() == 7);
    CHECK_FALSE(count.set_text("11", error));
    CHECK(error.find("entre 0 et 10") != std::string::npos);
    CHECK_FALSE(count.set_text("2.5", error));
    CHECK_FALSE(scale.set_text("vite", error));
    CHECK(scale.set_text("0.5", error));
    CHECK(scale.as_float() == 0.5f);
    CHECK(name.set_text("Alix", error));
    CHECK(name.as_text() == "Alix");
    const std::uint64_t revision = count.revision();
    count.set_int(7);  // unchanged: no new revision
    CHECK(count.revision() == revision);
    count.reset();
    CHECK(count.as_int() == 3);
    CHECK(count.is_default());
    // Declared again (a scene restarting): the same variable, its value kept.
    CHECK(&vars.add_int("debug.count", 9, 0, 10, "compte") == &count);
    CHECK_THROWS_AS(vars.add_bool("debug.count", true, "x"), std::logic_error);
    CHECK(vars.list("debug.").size() == 1);
}

TEST_CASE("Archived variables round-trip, including those declared later") {
    moteur::Variables vars;
    vars.add_bool("audio.mute", false, "", moteur::Variable::Archive);
    vars.add_int("tool.size", 2, 1, 9, "");  // not archived
    vars.set_pending("set audio.mute \"1\"\nset later.value \"42\"\nset tool.size 5\n");
    CHECK(vars.find("audio.mute")->as_bool());  // already declared: applied at once
    CHECK(vars.find("tool.size")->as_int() == 5);
    moteur::Variable& later = vars.add_int("later.value", 0, 0, 100, "", moteur::Variable::Archive);
    CHECK(later.as_int() == 42);  // applied when declared
    const std::string text = vars.archived_text();
    CHECK(text.find("set audio.mute \"1\"") != std::string::npos);
    CHECK(text.find("set later.value \"42\"") != std::string::npos);
    CHECK(text.find("tool.size") == std::string::npos);
}

TEST_CASE("Tool commands run at once, game commands are queued for the next tick") {
    moteur::Console console;
    int spawned = 0;
    std::vector<std::string> seen;
    console.add("spawn", "<monstre> [n]", "fait apparaître", moteur::Console::Kind::Game,
                [&](const moteur::Console::Args& args, moteur::Console&) {
                    seen = args;
                    spawned += args.size() > 1 ? std::stoi(args[1]) : 1;
                },
                [](std::size_t index, const std::string&) {
                    return index == 0 ? std::vector<std::string>{"squelette", "squelette_sbire", "liche"} : std::vector<std::string>{};
                });
    moteur::Variable& god = console.variables().add_bool("cheat.god", false, "invincible", moteur::Variable::Logic);
    moteur::Variable& paths = console.variables().add_bool("debug.paths", false, "chemins");

    console.submit("spawn squelette 3");
    CHECK(spawned == 0);  // queued
    console.submit("set debug.paths 1");
    CHECK(paths.as_bool());  // a tool variable: at once
    console.submit("set cheat.god 1");
    CHECK_FALSE(god.as_bool());  // a logic variable: queued like a game command
    const std::vector<std::string> queued = console.take_game_commands();
    REQUIRE(queued == std::vector<std::string>{"spawn squelette 3", "set cheat.god 1"});
    CHECK(console.take_game_commands().empty());
    for (const std::string& line : queued) {
        console.run_game_command(line);
    }
    CHECK(spawned == 3);
    CHECK(seen == std::vector<std::string>{"squelette", "3"});
    CHECK(god.as_bool());

    console.submit("nimporte quoi");
    CHECK(console.lines().back().level == moteur::Console::Level::Error);
    console.submit("set debug.paths 7");
    CHECK(console.lines().back().text.find("attendu 0 ou 1") != std::string::npos);
    CHECK(console.history().size() == 5);
}

TEST_CASE("Completion of commands, variables and arguments") {
    moteur::Console console;
    console.add("spawn", "", "", moteur::Console::Kind::Game, [](const moteur::Console::Args&, moteur::Console&) {},
                [](std::size_t, const std::string&) { return std::vector<std::string>{"squelette", "squelette_sbire"}; });
    console.variables().add_bool("fog.enabled", true, "");
    console.variables().add_bool("fog.explored", true, "");
    CHECK(console.complete("sp") == std::vector<std::string>{"spawn"});
    CHECK(console.complete_line("sp") == "spawn ");
    CHECK(console.complete_line("spawn sq") == "spawn squelette");  // common part of two candidates
    CHECK(console.complete("spawn squelette_") == std::vector<std::string>{"squelette_sbire"});
    CHECK(console.complete_line("set fog.") == "set fog.e");  // common part of fog.enabled and fog.explored
    CHECK(console.complete("set fog.e") == std::vector<std::string>{"fog.enabled", "fog.explored"});
    CHECK(console.complete_line("set fog.en") == "set fog.enabled ");
}

TEST_CASE("Scripts run line by line, comments skipped, failures reported") {
    moteur::Console console;
    moteur::Variable& v = console.variables().add_int("a.b", 0, 0, 9, "");
    CHECK(console.run_script("# réglage de test\nset a.b 4\n\n  echo fait\n"));
    CHECK(v.as_int() == 4);
    CHECK_FALSE(console.run_script("set a.b 99\n"));
}

TEST_CASE("Game commands travel in input recordings") {
    moteur::InputRecording recording;
    recording.actions = {"move_to"};
    recording.axis = {false};
    moteur::InputFrame frame;
    frame.buttons.resize(1);
    frame.axes.resize(1);
    frame.commands = {"spawn squelette 2", "tp 4 5"};
    recording.frames.push_back(frame);
    recording.frames.push_back(moteur::InputFrame{{moteur::ButtonState{}}, {glm::vec2(0.0f)}, glm::vec2(-1.0f), {}});
    const moteur::InputRecording back = moteur::input_recording_from_json(moteur::input_recording_to_json(recording), "test");
    REQUIRE(back.frames.size() == 2);
    CHECK(back.frames[0].commands == frame.commands);
    CHECK(back.frames[1].commands.empty());
}

TEST_CASE("Log lines carry the time and the level") {
    const std::string line = moteur::LogFile::format_line("info", "Assets: map reloaded");
    CHECK(line.find("[info] Assets: map reloaded") != std::string::npos);
    CHECK(line[2] == ':');
}
