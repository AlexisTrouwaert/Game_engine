#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace moteur {

// The animation graph of a character (milestone 7, part 9): which motion each layer plays, from
// parameters the game sets. Part of the AnimationSet file (key "graph"), reloaded with it:
//
//   "graph": {
//     "parameters": { "mort": "bool", "attaque": "trigger", "vitesse": "float", "arme": "int" },
//     "layers": [
//       { "name": "corps", "start": "locomotion",
//         "states": { "locomotion": { "motion": "locomotion" },
//                     "mort": { "motion": "Death_A", "loop": false } },
//         "transitions": [
//           { "from": "*", "to": "mort", "when": { "param": "mort" }, "priority": 10, "interrupt": true } ] },
//       { "name": "haut", "start": "repos",
//         "states": { "repos": {},                       // no motion: the layer fades out
//                     "attaque": { "motion": "1H_Melee_Attack_Chop", "loop": false, "interruptible": false } },
//         "transitions": [
//           { "from": "repos", "to": "attaque", "when": { "all": [ { "param": "attaque" },
//                                                                 { "param": "mort", "op": "==", "value": false } ] } },
//           { "from": "attaque", "to": "repos", "when": { "end": true } } ] }
//     ]
//   }
//
// Layer i of the graph drives layer i of the Animator (0: the whole body, 1: the upper body).
// A state plays a clip or a blend space of the set (or nothing). Conditions are structured, never
// parsed from text: {"param", "op", "value"} (op one of == != < <= > >=; a trigger or a bool alone
// means "set"), {"end": true} (the layer's clip played once has ended, or the layer fades out),
// {"all": [...]}, {"any": [...]}, {"not": {...}}. A transition without "when" always holds.
//
// Order: the layers in turn, the whole body first; the transitions of a layer are tried by
// priority (highest first), then in file order; the first that holds is taken. A trigger is
// taken at most once by each layer (one "mort" makes the body fall and the upper body let go). "from": "*" is any state (except the target itself unless
// "self": true, which plays the motion again from its start). A state with "interruptible": false
// only leaves by a transition marked "interrupt": true until its clip ends.
struct GraphParameter {
    enum class Type { Float, Int, Bool, Trigger };
    std::string name;
    Type type = Type::Float;
};

struct GraphCondition {
    enum class Kind { Always, Compare, Set, End, All, Any, Not };
    enum class Op { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
    Kind kind = Kind::Always;
    int parameter = -1;  // Compare, Set: index in AnimationGraph::parameters
    Op op = Op::Equal;
    double value = 0.0;  // a bool as 0 or 1
    std::vector<GraphCondition> children;  // All, Any, Not (one)
};

struct GraphState {
    std::string name;
    std::string motion;  // a clip or a blend space of the set; empty: the layer plays nothing
    bool loop = true;
    bool restart = false;  // entering it plays its motion from the start even if it already plays
    bool interruptible = true;
    bool match_speed = false;  // see PlayOptions::match_speed
};

struct GraphTransition {
    int from = -1;  // index in the layer's states; -1: any state
    int to = 0;
    GraphCondition when;
    int fade_ticks = -1;  // -1: the target clip's fade (see AnimationSet::fade_ticks)
    int priority = 0;
    bool interrupt = false;
    bool self = false;
};

struct GraphLayer {
    std::string name;
    int start = 0;
    std::vector<GraphState> states;
    std::vector<GraphTransition> transitions;  // tried in this order (sorted by priority at load)

    int state(const std::string& name) const;  // -1 if none
};

struct AnimationGraph {
    std::vector<GraphParameter> parameters;
    std::vector<GraphLayer> layers;  // at most Animator::kLayers
    std::uint64_t id = 0;            // a fresh number for every graph read: who uses it sees a reload

    bool empty() const { return layers.empty(); }
    int parameter(const std::string& name) const;  // -1 if none

    // Reads the "graph" object. Throws std::runtime_error naming the place ("layers[1] >
    // transitions[0] > when > param: 'attaq' is not a parameter").
    static AnimationGraph parse(const nlohmann::json& graph);
};

// Whether `condition` holds, with the parameters' values (triggers: 1 when fired) and whether the
// layer's clip has ended. `used` (if given) collects the triggers the condition read.
bool graph_condition_holds(const GraphCondition& condition, const std::vector<double>& values, bool ended,
                           std::vector<int>* used = nullptr);

}  // namespace moteur
