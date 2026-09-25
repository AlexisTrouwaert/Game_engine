#include "moteur/state_stack.hpp"

#include <exception>
#include <utility>

#include "moteur/input.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

namespace {

// Requests applied in one go: a state whose enter() keeps pushing states would never end.
constexpr int kMaxRequestRounds = 64;

// Mutes the input for the states under the top one, and always unmutes it.
class MutedInput {
public:
    MutedInput(Input* input, bool muted) : input_(muted ? input : nullptr) {
        if (input_ != nullptr) {
            input_->set_muted(true);
        }
    }
    ~MutedInput() {
        if (input_ != nullptr) {
            input_->set_muted(false);
        }
    }
    MutedInput(const MutedInput&) = delete;
    MutedInput& operator=(const MutedInput&) = delete;

private:
    Input* input_;
};

}  // namespace

// --- StateStack

StateStack::StateStack(Application& app) : input_(&app.input()) {
    after_transitions = [&app] { app.assets().collect_garbage(); };
    on_restart_clock = [&app] { app.restart_clock(); };
}

StateStack::~StateStack() {
    pending_.clear();  // requested states that never entered: destroyed without exit()
    while (!states_.empty()) {
        pop_now(false);
    }
}

void StateStack::push(std::unique_ptr<GameState> state) {
    pending_.push_back({Request::Kind::Push, std::move(state)});
}

void StateStack::pop() {
    pending_.push_back({Request::Kind::Pop, nullptr});
}

void StateStack::replace(std::unique_ptr<GameState> state) {
    pending_.push_back({Request::Kind::Replace, std::move(state)});
}

void StateStack::reset(std::unique_ptr<GameState> state) {
    pending_.push_back({Request::Kind::Reset, std::move(state)});
}

void StateStack::apply_pending() {
    if (pending_.empty()) {
        return;
    }
    for (int round = 0; round < kMaxRequestRounds && !pending_.empty(); ++round) {
        // enter() and exit() may ask for more: those come in the next round.
        std::vector<Request> requests = std::move(pending_);
        pending_.clear();
        for (Request& request : requests) {
            switch (request.kind) {
                case Request::Kind::Push:
                    push_now(std::move(request.state), true);
                    break;
                case Request::Kind::Pop:
                    if (!states_.empty()) {
                        pop_now(true);
                    }
                    break;
                case Request::Kind::Replace:
                    if (!states_.empty()) {
                        pop_now(!request.state);  // replaced by nothing: a pop
                    }
                    push_now(std::move(request.state), false);  // the state below stays covered
                    break;
                case Request::Kind::Reset:
                    while (!states_.empty()) {
                        pop_now(false);
                    }
                    push_now(std::move(request.state), false);
                    break;
            }
        }
    }
    if (!pending_.empty()) {
        SDL_Log("StateStack: states keep asking for transitions; %zu requests dropped", pending_.size());
        pending_.clear();
    }
    if (after_transitions) {
        after_transitions();
    }
}

void StateStack::push_now(std::unique_ptr<GameState> state, bool cover) {
    if (!state) {
        return;
    }
    if (cover && !states_.empty()) {
        states_.back().state->covered();
    }
    state->stack_ = this;
    states_.push_back({std::move(state), 0.0});
    states_.back().state->enter();
}

void StateStack::pop_now(bool uncover) {
    // exit() while the state is still on the stack (it may look at it), destroyed once off it.
    states_.back().state->exit();
    std::unique_ptr<GameState> leaving = std::move(states_.back().state);
    states_.pop_back();
    leaving.reset();
    if (uncover && !states_.empty()) {
        states_.back().state->uncovered();
    }
}

void StateStack::restart_clock() {
    if (on_restart_clock) {
        on_restart_clock();
    }
}

std::size_t StateStack::first_updated() const {
    std::size_t first = states_.size() - 1;
    while (first > 0 && !states_[first].state->blocking()) {
        --first;
    }
    return first;
}

std::size_t StateStack::first_drawn() const {
    std::size_t first = states_.size() - 1;
    while (first > 0 && states_[first].state->transparent()) {
        --first;
    }
    return first;
}

void StateStack::on_event(const SDL_Event& event) {
    if (!states_.empty()) {
        states_.back().state->on_event(event);
    }
}

void StateStack::update(double dt) {
    apply_pending();
    if (states_.empty()) {
        return;
    }
    // From the bottom up; a state asking for a transition changes nothing before the next tick.
    // By index: the vector does not change here (requests wait), but a state may push onto it.
    const std::size_t top = states_.size() - 1;
    for (std::size_t i = first_updated(); i <= top; ++i) {
        const MutedInput muted(input_, i != top);
        states_[i].state->update(dt);
    }
}

void StateStack::render(Renderer& renderer, double alpha) {
    visit_drawn(alpha, [&renderer](GameState& state, double state_alpha) { state.render(renderer, state_alpha); });
}

void StateStack::visit_drawn(double alpha, const std::function<void(GameState&, double)>& visit) {
    if (states_.empty()) {
        return;
    }
    const std::size_t updated = first_updated();
    for (std::size_t i = first_drawn(); i < states_.size(); ++i) {
        Entry& entry = states_[i];
        if (i >= updated) {
            entry.alpha = alpha;  // alive: follows the clock; frozen otherwise
        }
        visit(*entry.state, entry.alpha);
    }
}

// --- LoadingState

LoadingState::LoadingState(std::vector<Step> steps, std::string next_label, MakeState make_next)
    : steps_(std::move(steps)), next_label_(std::move(next_label)), make_next_(std::move(make_next)) {}

float LoadingState::progress() const {
    return static_cast<float>(done_) / static_cast<float>(steps_.size() + 1);
}

const std::string& LoadingState::label() const {
    return done_ < steps_.size() ? steps_[done_].label : next_label_;
}

void LoadingState::update(double) {
    if (done() || !error_.empty() || stack() == nullptr) {
        return;
    }
    try {
        if (done_ < steps_.size()) {
            if (steps_[done_].run) {
                steps_[done_].run();
            }
            ++done_;
        } else {
            std::unique_ptr<GameState> next = make_next_ ? make_next_() : nullptr;
            ++done_;
            stack()->replace(std::move(next));
        }
    } catch (const std::exception& e) {
        error_ = e.what();
        failed(error_);
    }
    // The step's time is not owed to the game: without this, the next frame would run the ticks
    // it "missed", each with a step of its own, and the screen would not move in between.
    stack()->restart_clock();
}

void LoadingState::render(Renderer& renderer, double) {
    draw(renderer, progress(), label());
}

void LoadingState::draw(Renderer& renderer, float, const std::string&) {
    renderer.set_clear_color(0.02f, 0.02f, 0.03f);
}

void LoadingState::failed(const std::string& error) {
    SDL_Log("Loading failed (%s): %s", label().c_str(), error.c_str());
    stack()->pop();
}

}  // namespace moteur
