#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/input.hpp"
#include "moteur/state_stack.hpp"

namespace {

// Every call a state receives, in order, as "name.call".
using Log = std::vector<std::string>;

class Probe : public moteur::GameState {
public:
    Probe(Log& log, std::string name, bool transparent = false, bool blocking = true)
        : log_(log), name_(std::move(name)), transparent_(transparent), blocking_(blocking) {
        log_.push_back(name_ + ".built");
    }
    ~Probe() override { log_.push_back(name_ + ".destroyed"); }

    void enter() override { log_.push_back(name_ + ".enter"); }
    void exit() override { log_.push_back(name_ + ".exit"); }
    void covered() override { log_.push_back(name_ + ".covered"); }
    void uncovered() override { log_.push_back(name_ + ".uncovered"); }
    void on_event(const SDL_Event&) override { log_.push_back(name_ + ".event"); }
    void update(double) override {
        ++ticks;
        log_.push_back(name_ + ".update");
        if (on_update) {
            on_update(*this);
        }
    }
    bool transparent() const override { return transparent_; }
    bool blocking() const override { return blocking_; }

    int ticks = 0;
    std::function<void(Probe&)> on_update;

private:
    Log& log_;
    std::string name_;
    bool transparent_;
    bool blocking_;
};

std::unique_ptr<Probe> probe(Log& log, const std::string& name, bool transparent = false, bool blocking = true) {
    return std::make_unique<Probe>(log, name, transparent, blocking);
}

// The states drawn this frame, from the bottom, and the alpha each one gets.
std::vector<moteur::GameState*> drawn(moteur::StateStack& stack, double alpha, std::vector<double>* alphas = nullptr) {
    std::vector<moteur::GameState*> states;
    stack.visit_drawn(alpha, [&](moteur::GameState& state, double state_alpha) {
        states.push_back(&state);
        if (alphas != nullptr) {
            alphas->push_back(state_alpha);
        }
    });
    return states;
}

}  // namespace

TEST_CASE("state stack: a push waits for the next update, then enters") {
    Log log;
    moteur::StateStack stack;
    stack.push(probe(log, "title"));
    CHECK(stack.empty());
    CHECK(stack.has_pending());
    CHECK(log == Log{"title.built"});

    stack.update(1.0 / 60.0);
    REQUIRE(stack.size() == 1);
    CHECK(log == Log{"title.built", "title.enter", "title.update"});
    CHECK(stack.top()->stack() == &stack);
}

TEST_CASE("state stack: push covers, pop exits, destroys and uncovers") {
    Log log;
    moteur::StateStack stack;
    stack.push(probe(log, "game"));
    stack.apply_pending();
    log.clear();

    stack.push(probe(log, "pause"));
    stack.apply_pending();
    CHECK(log == Log{"pause.built", "game.covered", "pause.enter"});
    log.clear();

    stack.pop();
    stack.apply_pending();
    CHECK(log == Log{"pause.exit", "pause.destroyed", "game.uncovered"});
    CHECK(stack.size() == 1);
}

TEST_CASE("state stack: replace builds the next state before the previous one goes") {
    Log log;
    moteur::StateStack stack;
    stack.push(probe(log, "base"));
    stack.push(probe(log, "loading"));
    stack.apply_pending();
    log.clear();

    stack.replace(probe(log, "game"));
    stack.apply_pending();
    // Built by the caller first (the assets both use stay loaded); the state below is not
    // uncovered in between.
    CHECK(log == Log{"game.built", "loading.exit", "loading.destroyed", "game.enter"});
    CHECK(stack.size() == 2);
}

TEST_CASE("state stack: reset empties the stack from the top, then pushes") {
    Log log;
    moteur::StateStack stack;
    stack.push(probe(log, "game"));
    stack.push(probe(log, "pause"));
    stack.apply_pending();
    log.clear();

    stack.reset(probe(log, "title"));
    stack.apply_pending();
    CHECK(log == Log{"title.built", "pause.exit", "pause.destroyed", "game.exit", "game.destroyed", "title.enter"});
    CHECK(stack.size() == 1);
}

TEST_CASE("state stack: transitions asked during an update wait for the next one") {
    Log log;
    moteur::StateStack stack;
    stack.push(probe(log, "game"));
    stack.apply_pending();
    auto* game = static_cast<Probe*>(stack.top());
    game->on_update = [&log](Probe& self) {
        if (self.ticks == 1) {
            self.stack()->push(probe(log, "pause"));
        }
    };
    log.clear();

    stack.update(1.0);
    CHECK(stack.size() == 1);  // still the game: nothing changes during a tick
    CHECK(stack.top() == game);
    CHECK(log == Log{"game.update", "pause.built"});

    stack.update(1.0);
    CHECK(stack.size() == 2);
    CHECK(game->ticks == 1);  // the pause blocks it from this tick on
}

TEST_CASE("state stack: a blocking state stops the simulation below; a non-blocking one does not") {
    Log log;
    moteur::StateStack stack;
    stack.push(probe(log, "game"));
    stack.apply_pending();
    auto* game = static_cast<Probe*>(stack.top());

    stack.push(probe(log, "hud", true, false));  // transparent, not blocking
    stack.update(1.0);
    CHECK(game->ticks == 1);
    log.clear();
    stack.update(1.0);
    CHECK(log == Log{"game.update", "hud.update"});  // from the bottom up

    stack.push(probe(log, "pause", true, true));
    for (int i = 0; i < 100; ++i) {
        stack.update(1.0);
    }
    CHECK(game->ticks == 2);  // frozen for 100 ticks

    stack.pop();
    stack.update(1.0);
    CHECK(game->ticks == 3);  // resumes where it was: nothing to catch up
}

TEST_CASE("state stack: transparent states show the ones below, opaque ones hide them") {
    Log log;
    moteur::StateStack stack;
    stack.push(probe(log, "game"));
    stack.push(probe(log, "pause", true));
    stack.push(probe(log, "options", true));
    stack.apply_pending();
    CHECK(drawn(stack, 0.5) == std::vector<moteur::GameState*>{stack.at(0), stack.at(1), stack.at(2)});

    stack.push(probe(log, "loading"));  // opaque
    stack.apply_pending();
    CHECK(drawn(stack, 0.5) == std::vector<moteur::GameState*>{stack.at(3)});
}

TEST_CASE("state stack: a frozen state keeps the alpha it was last drawn with") {
    Log log;
    moteur::StateStack stack;
    stack.push(probe(log, "game"));
    stack.apply_pending();
    std::vector<double> alphas;
    drawn(stack, 0.3, &alphas);
    CHECK(alphas == std::vector<double>{0.3});

    stack.push(probe(log, "pause", true));
    stack.apply_pending();
    alphas.clear();
    drawn(stack, 0.8, &alphas);
    drawn(stack, 0.1, &alphas);
    // The game stays at 0.3 (its world would otherwise shake between two ticks); the pause is live.
    CHECK(alphas == std::vector<double>{0.3, 0.8, 0.3, 0.1});

    stack.pop();
    stack.apply_pending();
    alphas.clear();
    drawn(stack, 0.6, &alphas);
    CHECK(alphas == std::vector<double>{0.6});
}

TEST_CASE("state stack: events and actions go to the top state only") {
    moteur::Input input;
    const moteur::ActionId jump = input.add_button("jump");
    input.load_bindings(R"({"version": 1, "profile": "p", "profiles": {"p": {"buttons": {"jump": ["key:Space"]}}}})",
                        "test");

    Log log;
    moteur::StateStack stack;
    stack.set_input(&input);
    stack.push(probe(log, "game"));
    stack.push(probe(log, "hud", true, false));  // the game below still updates
    stack.apply_pending();
    auto* game = static_cast<Probe*>(stack.at(0));
    auto* hud = static_cast<Probe*>(stack.at(1));
    bool game_saw = false, hud_saw = false;
    game->on_update = [&](Probe&) { game_saw = input.pressed(jump) || input.down(jump); };
    hud->on_update = [&](Probe&) { hud_saw = input.pressed(jump) && input.down(jump); };

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;
    event.key.down = true;
    input.process_event(event);
    log.clear();
    stack.on_event(event);
    CHECK(log == Log{"hud.event"});

    stack.update(1.0);
    CHECK(hud_saw);
    CHECK_FALSE(game_saw);
    CHECK_FALSE(input.muted());  // given back after the states below
    CHECK(input.pressed(jump));  // muting lost nothing
}

TEST_CASE("state stack: collection after each batch of transitions, and exit of all on destruction") {
    Log log;
    int collections = 0;
    {
        moteur::StateStack stack;
        stack.after_transitions = [&collections] { ++collections; };
        stack.update(1.0);
        CHECK(collections == 0);  // nothing changed

        stack.push(probe(log, "a"));
        stack.push(probe(log, "b"));
        stack.update(1.0);
        CHECK(collections == 1);  // one batch, one collection, once both are in
        stack.update(1.0);
        CHECK(collections == 1);
        log.clear();
    }
    CHECK(log == Log{"b.exit", "b.destroyed", "a.exit", "a.destroyed"});
}

TEST_CASE("state stack: states asking for transitions from enter() get them in the same batch") {
    Log log;
    moteur::StateStack stack;
    struct Redirect final : moteur::GameState {
        explicit Redirect(Log& log) : log_(log) {}
        void enter() override { stack()->replace(probe(log_, "menu")); }
        Log& log_;
    };
    stack.push(std::make_unique<Redirect>(log));
    stack.apply_pending();
    REQUIRE(stack.size() == 1);
    CHECK_FALSE(stack.has_pending());
    CHECK(log == Log{"menu.built", "menu.enter"});
}

TEST_CASE("loading state: one step per tick, then replaced by what it built") {
    Log log;
    moteur::StateStack stack;
    int clock_restarts = 0;
    stack.on_restart_clock = [&clock_restarts] { ++clock_restarts; };

    auto held = std::make_shared<std::vector<std::string>>();  // what the steps load, until the game is built
    std::vector<moteur::LoadingState::Step> steps = {
        {"Textures", [held] { held->push_back("textures"); }},
        {"Modèles", [held] { held->push_back("models"); }},
    };
    std::weak_ptr<std::vector<std::string>> watch = held;
    std::size_t seen_by_game = 0;
    auto loading = std::make_unique<moteur::LoadingState>(std::move(steps), "Carte", [&log, held, &seen_by_game] {
        seen_by_game = held->size();
        return probe(log, "game");
    });
    const moteur::LoadingState* state = loading.get();
    held.reset();
    stack.push(std::move(loading));
    stack.apply_pending();

    CHECK(state->progress() == doctest::Approx(0.0f));
    CHECK(state->label() == "Textures");
    stack.update(1.0);
    CHECK(state->progress() == doctest::Approx(1.0f / 3.0f));
    CHECK(state->label() == "Modèles");
    stack.update(1.0);
    CHECK(state->label() == "Carte");
    CHECK(log.empty());
    stack.update(1.0);  // builds the game, asks to be replaced
    CHECK(log == Log{"game.built"});
    CHECK(state->done());
    CHECK(clock_restarts == 3);
    CHECK_FALSE(watch.expired());  // still held: the loading state is still there

    stack.update(1.0);
    CHECK(seen_by_game == 2);
    REQUIRE(stack.size() == 1);
    CHECK(log == Log{"game.built", "game.enter", "game.update"});
    CHECK(watch.expired());  // released with the loading state, after the game was built
}

TEST_CASE("loading state: a step that throws pops it by default") {
    Log log;
    moteur::StateStack stack;
    stack.push(probe(log, "title"));
    std::vector<moteur::LoadingState::Step> steps = {{"Carte", [] { throw std::runtime_error("map.json: missing"); }}};
    auto loading = std::make_unique<moteur::LoadingState>(std::move(steps), "Jeu", [&log] { return probe(log, "game"); });
    const moteur::LoadingState* state = loading.get();
    stack.push(std::move(loading));
    stack.apply_pending();

    stack.update(1.0);
    CHECK(state->error() == "map.json: missing");
    stack.update(1.0);
    REQUIRE(stack.size() == 1);
    CHECK(log == Log{"title.built", "title.enter", "title.covered", "title.uncovered", "title.update"});
}
