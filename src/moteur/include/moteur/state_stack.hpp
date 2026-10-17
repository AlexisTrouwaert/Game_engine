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

/// One screen of the game with its logic: title screen, loading, game, pause, options...
///
/// States live on a StateStack. The one on top gets the input; those below may still be drawn
/// (under a transparent state) and updated (under a non-blocking one).
class GameState {
public:
    virtual ~GameState() = default;

    /// The state was pushed onto the stack (its constructor has run before: that is where it loads,
    /// unless a LoadingState did it). stack() is set by then. Transitions asked for here are applied
    /// right after the current batch.
    virtual void enter() {}
    /// The state is about to leave the stack (popped, replaced, reset, or the stack destroyed): it is
    /// still on it, and destroyed right after. A state that was requested but never pushed gets no
    /// exit().
    virtual void exit() {}
    /// Another state was pushed over this one (not called when the one over it is replaced).
    virtual void covered() {}
    /// The states over this one were popped: it is the top state again (not called by reset(), nor
    /// when the stack is destroyed).
    virtual void uncovered() {}

    /// Raw SDL events, to the top state only (see Game::on_event).
    virtual void on_event(const SDL_Event&) {}
    /// At the fixed rate, with the tick's duration in seconds, while no blocking state is over this
    /// one; from the bottom state updated up to the top. Only the top state reads the actions: under
    /// it, the Input reads as idle.
    virtual void update(double) {}
    /// Every frame while the state is visible, from the bottom state drawn up to the top (so the top
    /// state draws last, over the others). `alpha` is the Application's while the state is
    /// updated; under a blocking state it stays at its last value, so that the frozen world does not
    /// shake between its last two ticks.
    virtual void render(Renderer&, double) {}

    /// true: the state below is drawn first (a pause menu over the frozen game).
    virtual bool transparent() const { return false; }
    /// true: the states below are not updated: their simulation stops (a pause, a menu).
    virtual bool blocking() const { return true; }

    /// A short technical name, for the debug tools (the states on the stack) and the log. The
    /// string must outlive the state (a literal).
    virtual const char* name() const { return "state"; }
    /// The top state only: its next tick only waits for background work (see Game::waiting()).
    virtual bool waiting() { return false; }

    /// The stack the state is on, to ask for transitions; null before it is pushed (in its
    /// constructor).
    StateStack* stack() const { return stack_; }

private:
    friend class StateStack;
    StateStack* stack_ = nullptr;
};

/// The game's screens, as a stack: the pause is pushed over the game and popped to resume it, the
/// title screen replaces the game, a loading state replaces itself with what it loaded.
///
/// The stack is a Game: Application::run() drives it like any other.
///
/// Transitions (push, pop, replace, reset) are requests: they happen at the start of the next
/// update(), between two ticks, never while a state is being updated or drawn. A state may ask for
/// them from anywhere, including its own update() or render(). A replaced state is destroyed after
/// its successor was built (by whoever asked), so what both use stays loaded. Requests are applied
/// in the order they were made; those that enter() and exit() make are applied in the same batch,
/// up to 64 rounds (beyond, they are dropped with a log line: a state pushing states forever).
///
/// The stack owns its states. Main thread only.
class StateStack final : public Game {
public:
    /// Without an Application (tests): no input masking, no asset collection.
    StateStack() = default;
    /// The usual one: only the top state reads the input, the assets nobody holds any more are freed
    /// after each transition, and restart_clock() restarts the Application's clock.
    explicit StateStack(Application& app);
    /// Every state leaves, from the top (exit(), then destruction); the requested states that were
    /// never pushed are destroyed without enter() nor exit().
    ~StateStack() override;

    StateStack(const StateStack&) = delete;
    StateStack& operator=(const StateStack&) = delete;

    /// Asks to push `state` over the top one (which gets covered()); it then gets enter(). A null
    /// state is ignored.
    void push(std::unique_ptr<GameState> state);
    /// Asks to pop the top state (exit(), destruction; the one below gets uncovered()). Nothing
    /// happens if the stack is empty by then.
    void pop();
    /// Pops the top state and pushes this one (the state below is not uncovered in between). Null:
    /// a pop.
    void replace(std::unique_ptr<GameState> state);
    /// Pops every state and pushes this one (back to the title screen). Null: empties the stack.
    void reset(std::unique_ptr<GameState> state);
    /// Applies the requests now, then calls after_transitions (if there were any). update() does
    /// it; call it to install the first state before run(), or from outside a frame. Never from a
    /// state's update() or render(): the stack would change under the loop.
    void apply_pending();
    /// Whether transitions were requested and not applied yet.
    bool has_pending() const { return !pending_.empty(); }

    /// Whether no state is on the stack (requests pending not counted).
    bool empty() const { return states_.empty(); }
    /// The number of states on the stack.
    std::size_t size() const { return states_.size(); }
    /// The top state (the one that gets the input), or null if the stack is empty. Owned by the
    /// stack: valid until it is popped.
    GameState* top() const { return states_.empty() ? nullptr : states_.back().state.get(); }
    /// The state at `index`, 0 being the bottom; throws std::out_of_range from size() on. Owned by
    /// the stack.
    GameState* at(std::size_t index) const { return states_.at(index).state.get(); }  // 0: the bottom

    /// After a long step (a load), the time it took does not count: the logic does not run the
    /// ticks it would owe to catch up. Without an Application, calls the hook below only.
    void restart_clock();

    /// The Input muted for the states under the top one while they update (the constructor taking
    /// an Application sets it; null: none). It must outlive the stack, or be unset first.
    void set_input(Input* input) { input_ = input; }

    /// The states drawn this frame, from the bottom, each with the alpha it is drawn with (what
    /// render() does with them; tests call it without a Renderer).
    void visit_drawn(double alpha, const std::function<void(GameState&, double)>& visit);

    /// Called after each batch of transitions, once the states that left are destroyed (the
    /// Application's stack frees the assets there). Replaceable, for tests.
    std::function<void()> after_transitions;
    /// Called by restart_clock().
    std::function<void()> on_restart_clock;

    /// Gives the event to the top state only.
    void on_event(const SDL_Event& event) override;
    /// Applies the pending transitions (apply_pending()), then updates the states from the lowest
    /// one not under a blocking state up to the top, the input muted for all but the top.
    void update(double dt) override;
    /// Draws the visible states (from the lowest one not under an opaque state), each with its
    /// alpha (see GameState::render()).
    void render(Renderer& renderer, double alpha) override;
    /// The top state's, unless a transition is pending (it happens in a tick that counts).
    bool waiting() override { return !has_pending() && top() != nullptr && top()->waiting(); }

private:
    struct Entry {
        std::unique_ptr<GameState> state;
        double alpha = 0.0;  ///< the last alpha it was drawn with
    };
    struct Request {
        enum class Kind { Push, Pop, Replace, Reset } kind;
        std::unique_ptr<GameState> state;
    };

    /// The lowest state that is updated (the top one, or down to the first blocking one).
    std::size_t first_updated() const;
    /// The lowest state that is drawn (the top one, or down to the first opaque one).
    std::size_t first_drawn() const;
    void push_now(std::unique_ptr<GameState> state, bool cover);
    void pop_now(bool uncover);

    std::vector<Entry> states_;
    std::vector<Request> pending_;
    Input* input_ = nullptr;
};

/// Shows how far a load is while it runs its steps, one per tick, then replaces itself with the
/// state its last step builds. A step's `run` blocks the main thread; it may instead start
/// background work (Assets::prefetch_*) and leave the wait to its `until`, the screen moving
/// meanwhile.
///
/// One step per tick, and the clock restarted after each. The ticks spent waiting for an `until`
/// do not count (see waiting()), and the tick that finds it true goes on with the next step: a load
/// always takes the same number of counted ticks, whatever the machine and however long the
/// background work lasts, and replays of recorded input stay in step.
///
/// Keeping what the steps load until the next state is built (so that it finds it in the cache) is
/// up to the steps: they usually fill a holder captured by the last step too.
class LoadingState : public GameState {
public:
    /// One step of the load.
    struct Step {
        std::string label;  ///< what the screen says while it runs ("Textures", "Carte"...)
        /// The work of the step, run on the main thread during one tick (it may load assets, or
        /// start prefetches and leave the wait to `until`). Empty: nothing to run. Throws to fail
        /// the load (see failed()).
        std::function<void()> run;
        /// Optional: once `run` ran, the state stays on this step, one tick after the other, until
        /// this returns true (files decoded in the background: Assets::prefetching() == 0). The
        /// screen keeps moving meanwhile.
        std::function<bool()> until = {};
    };
    /// Builds the state that replaces the loading state once every step ran.
    using MakeState = std::function<std::unique_ptr<GameState>()>;

    /// `make_next` runs last, labelled `next_label`: the state that replaces this one (null, or an
    /// empty `make_next`: the loading state is popped instead).
    LoadingState(std::vector<Step> steps, std::string next_label, MakeState make_next);

    /// Runs the next step (or goes on waiting for its `until`), logs its time, then restarts the
    /// clock. Does nothing before the state is on a stack, once done() or after an error().
    void update(double dt) override;
    /// Calls draw() with progress() and label().
    void render(Renderer& renderer, double alpha) override;
    /// `loading`.
    const char* name() const override { return "loading"; }
    /// While a step's `until` is false: decided once per tick, here, so that the tick that sees it
    /// become true is the one that counts and moves on.
    bool waiting() override;

    /// Steps done, over all steps (the building of the next state included): 0 to 1.
    float progress() const;
    /// The label of the step that runs next (what is being loaded), `next_label` for the last one.
    const std::string& label() const;
    /// Whether every step ran and the next state was built (its replacement requested).
    bool done() const { return done_ >= steps_.size() + 1; }
    /// The message of the exception a step (or `make_next`) threw; empty while none did. Once set,
    /// the state runs no more steps.
    const std::string& error() const { return error_; }

protected:
    /// Draws the screen, every frame: `progress` from 0 to 1 and the `label` of the step under way.
    /// The default only clears it (to a near-black): games draw their own.
    virtual void draw(Renderer& renderer, float progress, const std::string& label);
    /// A step threw `error` (the exception's message, also in error()). The default logs it and pops
    /// this state (back to what was under it, if any).
    virtual void failed(const std::string& error);

private:
    std::vector<Step> steps_;
    std::string next_label_;
    MakeState make_next_;
    std::size_t done_ = 0;
    bool waiting_ = false;  ///< the current step ran and waits for its `until`
    bool ready_ = false;    ///< what waiting() saw: the `until` of the current step is true
    bool asked_ = false;    ///< waiting() was asked before this tick (otherwise update() checks itself)
    std::string error_;
};

}  // namespace moteur
