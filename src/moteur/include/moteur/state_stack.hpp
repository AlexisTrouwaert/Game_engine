#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "moteur/application.hpp"

namespace moteur {

class Input;
class Renderer;
class StateStack;

// One screen of the game with its logic: title screen, loading, game, pause, options...
//
// States live on a StateStack. The one on top gets the input; those below may still be drawn
// (under a transparent state) and updated (under a non-blocking one).
class GameState {
public:
    virtual ~GameState() = default;

    // The state was pushed onto the stack (its constructor has run before: that is where it loads,
    // unless a LoadingState did it), and it is about to leave it (it is destroyed right after).
    virtual void enter() {}
    virtual void exit() {}
    // Another state was pushed over this one; the states over it were popped.
    virtual void covered() {}
    virtual void uncovered() {}

    // Raw SDL events, to the top state only (see Game::on_event).
    virtual void on_event(const SDL_Event&) {}
    // At the fixed rate, while no blocking state is over this one. Only the top state reads the
    // actions: under it, the Input reads as idle.
    virtual void update(double) {}
    // Every frame while the state is visible. `alpha` is the Application's while the state is
    // updated; under a blocking state it stays at its last value, so that the frozen world does not
    // shake between its last two ticks.
    virtual void render(Renderer&, double) {}

    // true: the state below is drawn first (a pause menu over the frozen game).
    virtual bool transparent() const { return false; }
    // true: the states below are not updated: their simulation stops (a pause, a menu).
    virtual bool blocking() const { return true; }

    // For the debug tools (the states on the stack).
    virtual const char* name() const { return "state"; }

    // The stack the state is on; null before it is pushed.
    StateStack* stack() const { return stack_; }

private:
    friend class StateStack;
    StateStack* stack_ = nullptr;
};

// The game's screens, as a stack: the pause is pushed over the game and popped to resume it, the
// title screen replaces the game, a loading state replaces itself with what it loaded.
//
// The stack is a Game: Application::run() drives it like any other.
//
// Transitions (push, pop, replace, reset) are requests: they happen at the start of the next
// update(), between two ticks, never while a state is being updated or drawn. A state may ask for
// them from anywhere, including its own update() or render(). A replaced state is destroyed after
// its successor was built (by whoever asked), so what both use stays loaded.
class StateStack final : public Game {
public:
    // Without an Application (tests): no input masking, no asset collection.
    StateStack() = default;
    // The usual one: only the top state reads the input, the assets nobody holds any more are freed
    // after each transition, and restart_clock() restarts the Application's clock.
    explicit StateStack(Application& app);
    // Every state leaves, from the top (exit(), then destruction).
    ~StateStack() override;

    StateStack(const StateStack&) = delete;
    StateStack& operator=(const StateStack&) = delete;

    void push(std::unique_ptr<GameState> state);
    void pop();
    // Pops the top state and pushes this one (the state below is not uncovered in between).
    void replace(std::unique_ptr<GameState> state);
    // Pops every state and pushes this one (back to the title screen).
    void reset(std::unique_ptr<GameState> state);
    // Applies the requests now. update() does it; call it to install the first state before run(),
    // or from outside a frame.
    void apply_pending();
    bool has_pending() const { return !pending_.empty(); }

    bool empty() const { return states_.empty(); }
    std::size_t size() const { return states_.size(); }
    GameState* top() const { return states_.empty() ? nullptr : states_.back().state.get(); }
    GameState* at(std::size_t index) const { return states_.at(index).state.get(); }  // 0: the bottom

    // After a long step (a load), the time it took does not count: the logic does not run the
    // ticks it would owe to catch up. Without an Application, calls the hook below only.
    void restart_clock();

    // The Input muted for the states under the top one while they update (the Application's
    // constructor above sets it; null: none).
    void set_input(Input* input) { input_ = input; }

    // The states drawn this frame, from the bottom, each with the alpha it is drawn with (what
    // render() does with them; tests call it without a Renderer).
    void visit_drawn(double alpha, const std::function<void(GameState&, double)>& visit);

    // Called after each batch of transitions, once the states that left are destroyed (the
    // Application's stack frees the assets there). Replaceable, for tests.
    std::function<void()> after_transitions;
    // Called by restart_clock().
    std::function<void()> on_restart_clock;

    void on_event(const SDL_Event& event) override;
    void update(double dt) override;
    void render(Renderer& renderer, double alpha) override;

private:
    struct Entry {
        std::unique_ptr<GameState> state;
        double alpha = 0.0;  // the last alpha it was drawn with
    };
    struct Request {
        enum class Kind { Push, Pop, Replace, Reset } kind;
        std::unique_ptr<GameState> state;
    };

    // The lowest state that is updated (the top one, or down to the first blocking one).
    std::size_t first_updated() const;
    // The lowest state that is drawn (the top one, or down to the first opaque one).
    std::size_t first_drawn() const;
    void push_now(std::unique_ptr<GameState> state, bool cover);
    void pop_now(bool uncover);

    std::vector<Entry> states_;
    std::vector<Request> pending_;
    Input* input_ = nullptr;
};

// Shows how far a load is while it runs its steps, one per tick, then replaces itself with the
// state its last step builds. The loading is synchronous (each step blocks), but split into steps
// so that the screen moves between them; an asynchronous loader will keep this interface.
//
// One step per tick, and the clock restarted after each: a load always takes the same number of
// ticks, whatever the machine, and replays of recorded input stay in step.
//
// Keeping what the steps load until the next state is built (so that it finds it in the cache) is
// up to the steps: they usually fill a holder captured by the last step too.
class LoadingState : public GameState {
public:
    struct Step {
        std::string label;  // what the screen says while it runs ("Textures", "Carte"...)
        std::function<void()> run;
    };
    using MakeState = std::function<std::unique_ptr<GameState>()>;

    // `make_next` runs last, labelled `next_label`: the state that replaces this one.
    LoadingState(std::vector<Step> steps, std::string next_label, MakeState make_next);

    void update(double dt) override;
    void render(Renderer& renderer, double alpha) override;
    const char* name() const override { return "loading"; }

    // Steps done, over all steps (the last one included): 0 to 1.
    float progress() const;
    // The label of the step that runs next (what is being loaded).
    const std::string& label() const;
    bool done() const { return done_ >= steps_.size() + 1; }
    const std::string& error() const { return error_; }

protected:
    // Draws the screen. The default only clears it: games draw their own.
    virtual void draw(Renderer& renderer, float progress, const std::string& label);
    // A step threw. The default logs it and pops this state (back to what was under it, if any).
    virtual void failed(const std::string& error);

private:
    std::vector<Step> steps_;
    std::string next_label_;
    MakeState make_next_;
    std::size_t done_ = 0;
    std::string error_;
};

}  // namespace moteur
