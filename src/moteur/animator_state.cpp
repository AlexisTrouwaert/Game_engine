// The Animator's state, written and read back exactly (milestone 7, part 6).

#include <nlohmann/json.hpp>

#include <array>
#include <stdexcept>
#include <utility>

#include "moteur/animator.hpp"

namespace moteur {

namespace {

nlohmann::json clock_json(const ClipClock& clock) {
    const ClipClock::State s = clock.state();
    return {s.cycle_ticks, s.once, s.time, s.speed, s.started};
}

ClipClock clock_from(const nlohmann::json& j) {
    ClipClock clock;
    clock.set_state({j.at(0).get<int>(), j.at(1).get<bool>(), j.at(2).get<std::int64_t>(), j.at(3).get<std::int64_t>(),
                     j.at(4).get<bool>()});
    return clock;
}

const SkeletalClip* clip_named(const Asset<ClipLibrary>& clips, const std::string& name) {
    if (!clips || !clips->contains(name)) {
        throw std::runtime_error("animation state: no clip '" + name + "' in the library");
    }
    return &clips->clip(name);
}

}  // namespace

nlohmann::json Animator::save_state() const {
    nlohmann::json state;
    state["speed"] = speed_;
    state["move_speed"] = {move_speed_, tick_move_speed_, previous_move_speed_};
    state["ticks"] = ticks_;
    state["last_event"] = {last_event_.name, last_event_.clip, last_event_.tick};
    nlohmann::json layers = nlohmann::json::array();
    for (const Layer& layer : layers_) {
        nlohmann::json l;
        l["weights"] = {layer.weight, layer.previous_weight, layer.fade_from, layer.fade_to, layer.fade_ticks, layer.fade_elapsed};
        nlohmann::json motions = nlohmann::json::array();
        for (const Motion& m : layer.motions) {
            nlohmann::json j;
            j["name"] = m.name;
            j["clip"] = m.clip != nullptr ? m.clip->name() : std::string();
            nlohmann::json points = nlohmann::json::array();
            for (const BlendPoint& p : m.points) {
                points.push_back({p.clip != nullptr ? p.clip->name() : std::string(), p.position, p.phase});
            }
            j["points"] = points;
            j["ground_speed"] = m.ground_speed;
            j["match_speed"] = m.match_speed;
            j["clock"] = clock_json(m.clock);
            j["previous_time"] = m.previous_time;
            j["continuous"] = m.continuous;
            j["rate"] = m.rate;
            j["weights"] = {m.weight, m.previous_weight, m.fade_from, m.fade_to, m.fade_ticks, m.fade_elapsed, m.fade_setting};
            j["marks"] = m.marks;
            nlohmann::json events = nlohmann::json::array();
            for (const MotionEvent& e : m.events) {
                events.push_back({e.name, e.clip, e.point, e.always, static_cast<int>(e.phase)});
            }
            j["events"] = events;
            motions.push_back(std::move(j));
        }
        l["motions"] = motions;
        layers.push_back(std::move(l));
    }
    state["layers"] = layers;
    state["root_motion"] = {root_motion_.x, root_motion_.y, root_motion_.z};
    if (graph_.active) {
        // The graph: its parameters and the state of each layer, by name (the file may change).
        nlohmann::json values = nlohmann::json::object();
        for (std::size_t i = 0; i < graph_.names.size(); ++i) {
            values[graph_.names[i]] = graph_.values[i];
        }
        state["graph"] = {{"values", values}, {"states", graph_.state_names}};
    }
    return state;
}

void Animator::load_state(const nlohmann::json& state) {
    // Everything is read into copies first, then taken at once: a state that fails halfway leaves
    // the Animator as it was.
    std::int64_t speed = 0;
    std::int64_t move_speed = 0;
    std::int64_t tick_move_speed = 0;
    std::int64_t previous_move_speed = 0;
    std::int64_t ticks = 0;
    LastEvent last_event;
    std::vector<Layer> loaded = layers_;  // the masks and kinds come from create(): kept
    glm::ivec3 root_motion(0);
    GraphRuntime graph_state;                     // no "graph": not started, as when it was saved
    try {
        speed = state.at("speed").get<std::int64_t>();
        move_speed = state.at("move_speed").at(0).get<std::int64_t>();
        tick_move_speed = state.at("move_speed").at(1).get<std::int64_t>();
        previous_move_speed = state.at("move_speed").at(2).get<std::int64_t>();
        ticks = state.at("ticks").get<std::int64_t>();
        const nlohmann::json& last = state.at("last_event");
        last_event = {last.at(0).get<std::string>(), last.at(1).get<std::string>(), last.at(2).get<std::int64_t>()};
        const nlohmann::json& layers = state.at("layers");
        // A state saved with fewer layers (a set that gained one since) leaves the others idle.
        if (layers.size() > loaded.size()) {
            throw std::runtime_error("animation state: " + std::to_string(layers.size()) + " layers for " +
                                     std::to_string(loaded.size()));
        }
        for (std::size_t i = 0; i < layers.size(); ++i) {
            Layer& layer = loaded[i];
            const nlohmann::json& l = layers[i];
            const nlohmann::json& w = l.at("weights");
            layer.weight = w.at(0).get<int>();
            layer.previous_weight = w.at(1).get<int>();
            layer.fade_from = w.at(2).get<int>();
            layer.fade_to = w.at(3).get<int>();
            layer.fade_ticks = w.at(4).get<int>();
            layer.fade_elapsed = w.at(5).get<int>();
            layer.motions.clear();
            for (const nlohmann::json& j : l.at("motions")) {
                Motion m;
                m.name = j.at("name").get<std::string>();
                const std::string clip = j.at("clip").get<std::string>();
                m.clip = clip.empty() ? nullptr : clip_named(clips, clip);
                for (const nlohmann::json& p : j.at("points")) {
                    m.points.push_back({clip_named(clips, p.at(0).get<std::string>()), p.at(1).get<std::int64_t>(), p.at(2).get<float>()});
                }
                m.ground_speed = j.at("ground_speed").get<std::int64_t>();
                m.match_speed = j.at("match_speed").get<bool>();
                m.clock = clock_from(j.at("clock"));
                m.previous_time = j.at("previous_time").get<std::int64_t>();
                m.continuous = j.at("continuous").get<bool>();
                m.rate = j.at("rate").get<std::int64_t>();
                const nlohmann::json& mw = j.at("weights");
                m.weight = mw.at(0).get<int>();
                m.previous_weight = mw.at(1).get<int>();
                m.fade_from = mw.at(2).get<int>();
                m.fade_to = mw.at(3).get<int>();
                m.fade_ticks = mw.at(4).get<int>();
                m.fade_elapsed = mw.at(5).get<int>();
                m.fade_setting = mw.at(6).get<int>();
                m.marks = j.at("marks").get<std::vector<int>>();
                for (const nlohmann::json& e : j.at("events")) {
                    m.events.push_back({e.at(0).get<std::string>(), e.at(1).get<std::string>(), e.at(2).get<int>(), e.at(3).get<bool>(),
                                        static_cast<AnimatorEvent::Phase>(e.size() > 4 ? e.at(4).get<int>() : 0)});
                }
                bind_clip_data(m, !m.clock.once());  // root motion and windows, from the set
                layer.motions.push_back(std::move(m));
            }
        }
        if (const auto saved = state.find("root_motion"); saved != state.end()) {
            root_motion = {saved->at(0).get<int>(), saved->at(1).get<int>(), saved->at(2).get<int>()};
        }
        if (state.contains("graph") && set && !set->graph().empty()) {
            const AnimationGraph& graph = set->graph();
            const nlohmann::json& g = state.at("graph");
            graph_state.active = true;
            graph_state.id = graph.id;
            graph_state.state.assign(loaded.size(), -1);
            graph_state.state_names.assign(loaded.size(), std::string());
            for (const GraphParameter& p : graph.parameters) {
                graph_state.names.push_back(p.name);
                graph_state.values.push_back(g.at("values").value(p.name, 0.0));
            }
            const nlohmann::json& states = g.at("states");
            for (std::size_t l = 0; l < graph.layers.size() && l < states.size(); ++l) {
                const std::string name = states[l].get<std::string>();
                const int found = graph.layers[l].state(name);
                graph_state.state[l] = found >= 0 ? found : graph.layers[l].start;
                graph_state.state_names[l] = graph.layers[l].states[static_cast<std::size_t>(graph_state.state[l])].name;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("animation state: ") + e.what());
    }
    speed_ = speed;
    move_speed_ = move_speed;
    tick_move_speed_ = tick_move_speed;
    previous_move_speed_ = previous_move_speed;
    ticks_ = ticks;
    last_event_ = std::move(last_event);
    layers_ = std::move(loaded);
    root_motion_ = root_motion;
    graph_ = std::move(graph_state);
}

}  // namespace moteur
