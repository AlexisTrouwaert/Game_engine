#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "moteur/animator.hpp"

// The Animator's side of the animation graph (milestone 7, part 9): its state, its evaluation.

namespace moteur {

namespace {

constexpr std::size_t kHistory = 16;    // transitions kept for the debug tools
constexpr int kStepsPerEvaluation = 4;  // a layer may chain this many transitions in one evaluation

}  // namespace

std::string Animator::check_graph_motions(const AnimationGraph& graph) const {
    for (const GraphLayer& layer : graph.layers) {
        for (const GraphState& state : layer.states) {
            if (state.motion.empty() || set->blend_space(state.motion) != nullptr || (clips && clips->contains(state.motion))) {
                continue;
            }
            return "state '" + state.name + "' of layer '" + layer.name + "': no clip or blend space '" + state.motion + "'";
        }
    }
    return {};
}

void Animator::start_graph() {
    if (!set || set->graph().empty()) {
        throw std::runtime_error("Animator: '" + (set ? set->source() : std::string("(no set)")) + "' has no graph");
    }
    const AnimationGraph& graph = set->graph();
    if (const std::string error = check_graph_motions(graph); !error.empty()) {
        throw std::runtime_error("Animation set '" + set->source() + "': graph > " + error);
    }
    graph_ = GraphRuntime{};
    graph_.active = true;
    graph_.id = graph.id;
    for (const GraphParameter& p : graph.parameters) {
        graph_.names.push_back(p.name);
        graph_.values.push_back(0.0);
    }
    for (std::size_t l = 0; l < graph.layers.size(); ++l) {
        enter_state(static_cast<int>(l), graph.layers[l].start, 0, false);
    }
    graph_.history.clear();  // the start is not a transition
    evaluate_graph(-1);
}

bool Animator::sync_graph() {
    if (!set || set->graph().empty()) {
        if (graph_.error.empty()) {
            graph_.error = "the set has no graph any more";
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Animator: %s", graph_.error.c_str());
        }
        return false;
    }
    const AnimationGraph& graph = set->graph();
    if (graph.id == graph_.id) {
        return graph_.error.empty();
    }
    // The file was read again: parameters and states found again by name.
    graph_.id = graph.id;
    if (std::string error = check_graph_motions(graph); !error.empty()) {
        graph_.error = std::move(error);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Animator: '%s': graph > %s (the graph waits for a valid file)",
                    set->source().c_str(), graph_.error.c_str());
        return false;
    }
    graph_.error.clear();
    std::vector<std::string> names;
    std::vector<double> values;
    for (const GraphParameter& p : graph.parameters) {
        const auto old = std::find(graph_.names.begin(), graph_.names.end(), p.name);
        names.push_back(p.name);
        values.push_back(old != graph_.names.end() ? graph_.values[static_cast<std::size_t>(old - graph_.names.begin())] : 0.0);
    }
    graph_.names = std::move(names);
    graph_.values = std::move(values);
    for (std::size_t l = 0; l < static_cast<std::size_t>(kLayers); ++l) {
        if (l >= graph.layers.size()) {
            graph_.state[l] = -1;
            graph_.state_names[l].clear();
            continue;
        }
        const int found = graph.layers[l].state(graph_.state_names[l]);
        if (found >= 0) {
            graph_.state[l] = found;
        } else {
            enter_state(static_cast<int>(l), graph.layers[l].start, -1, false);  // its state is gone: back to the start
        }
    }
    return true;
}

bool Animator::layer_ended(int index) const {
    const Layer& layer = layers_[static_cast<std::size_t>(index)];
    if (layer.motions.empty() || (index > 0 && layer.fade_to == 0)) {
        return true;
    }
    const Motion& latest = layer.motions.back();
    return !latest.blend_space() && latest.clock.finished();
}

void Animator::enter_state(int index, int state, int fade_ticks, bool restart) {
    const GraphLayer& layer = set->graph().layers[static_cast<std::size_t>(index)];
    const GraphState& target = layer.states[static_cast<std::size_t>(state)];
    const std::size_t l = static_cast<std::size_t>(index);
    graph_.history.push_back({index, graph_.state_names[l], target.name, ticks_});
    if (graph_.history.size() > kHistory) {
        graph_.history.erase(graph_.history.begin());
    }
    graph_.state[l] = state;
    graph_.state_names[l] = target.name;
    if (target.motion.empty()) {
        // Nothing to play: the layer fades out. A transition without a fade of its own leaves a
        // layer that already fades out alone (an upper-body clip ending by itself).
        const bool fading = index > 0 ? layers_[l].fade_to == 0 : layers_[l].motions.empty();
        if (fade_ticks >= 0) {
            stop(index, fade_ticks);
        } else if (!fading) {
            stop(index, set->default_fade_ticks());
        }
        return;
    }
    PlayOptions options;
    options.layer = index;
    options.fade_ticks = fade_ticks;
    options.loop = target.loop;
    options.restart = restart || target.restart;
    options.match_speed = target.match_speed;
    play(target.motion, options);
}

bool Animator::evaluate_graph(int fired) {
    if (!graph_.active || !sync_graph()) {
        return false;
    }
    const AnimationGraph& graph = set->graph();
    bool taken = false;
    std::vector<int> used;
    std::vector<double> values;
    for (std::size_t l = 0; l < graph.layers.size(); ++l) {
        const GraphLayer& layer = graph.layers[l];
        // Each layer sees the trigger once: the body falls and the upper body lets go on one "mort".
        values = graph_.values;
        if (fired >= 0) {
            values[static_cast<std::size_t>(fired)] = 1.0;
        }
        for (int step = 0; step < kStepsPerEvaluation; ++step) {
            const int current = graph_.state[l];
            const GraphState& state = layer.states[static_cast<std::size_t>(current)];
            const bool ended = layer_ended(static_cast<int>(l));
            const bool locked = !state.interruptible && !ended;
            const GraphTransition* chosen = nullptr;
            for (const GraphTransition& t : layer.transitions) {
                if ((t.from >= 0 && t.from != current) || (t.from < 0 && t.to == current && !t.self) || (locked && !t.interrupt)) {
                    continue;
                }
                used.clear();
                if (graph_condition_holds(t.when, values, ended, &used)) {
                    chosen = &t;
                    break;
                }
            }
            if (chosen == nullptr) {
                break;
            }
            // The triggers it read are spent for this layer.
            for (const int p : used) {
                if (graph.parameters[static_cast<std::size_t>(p)].type == GraphParameter::Type::Trigger) {
                    values[static_cast<std::size_t>(p)] = 0.0;
                    taken = taken || p == fired;
                }
            }
            const bool again = chosen->to == current;
            enter_state(static_cast<int>(l), chosen->to, chosen->fade_ticks, again);
            if (again) {
                break;  // a state entered again once per evaluation
            }
        }
    }
    return taken;
}

bool Animator::set_parameter(const std::string& name, double value) {
    if (!graph_.active || !sync_graph()) {
        return false;
    }
    const AnimationGraph& graph = set->graph();
    const int index = graph.parameter(name);
    if (index < 0) {
        return false;
    }
    const GraphParameter::Type type = graph.parameters[static_cast<std::size_t>(index)].type;
    if (type == GraphParameter::Type::Trigger) {
        return value != 0.0 ? fire(name) : true;
    }
    if (type == GraphParameter::Type::Bool) {
        value = value != 0.0 ? 1.0 : 0.0;
    } else if (type == GraphParameter::Type::Int) {
        value = std::round(value);
    }
    double& slot = graph_.values[static_cast<std::size_t>(index)];
    if (slot != value) {
        slot = value;
        evaluate_graph(-1);
    }
    return true;
}

double Animator::parameter(const std::string& name) const {
    const auto found = std::find(graph_.names.begin(), graph_.names.end(), name);
    return found != graph_.names.end() ? graph_.values[static_cast<std::size_t>(found - graph_.names.begin())] : 0.0;
}

bool Animator::fire(const std::string& name) {
    if (!graph_.active || !sync_graph()) {
        return false;
    }
    const AnimationGraph& graph = set->graph();
    const int index = graph.parameter(name);
    if (index < 0 || graph.parameters[static_cast<std::size_t>(index)].type != GraphParameter::Type::Trigger) {
        return false;
    }
    return evaluate_graph(index);
}

const std::string& Animator::graph_state(int layer) const {
    return graph_.state_names.at(static_cast<std::size_t>(layer));
}

}  // namespace moteur
