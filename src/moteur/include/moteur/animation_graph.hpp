#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace moteur {

/// The animation graph of a character (milestone 7, part 9): which motion each layer plays, from
/// parameters the game sets. Part of the AnimationSet file (key "graph"), reloaded with it:
///
/// ```
/// "graph": {
///   "parameters": { "dead": "bool", "attack": "trigger", "speed": "float", "weapon": "int" },
///   "layers": [
///     { "name": "body", "start": "locomotion",
///       "states": { "locomotion": { "motion": "locomotion" },
///                   "dead": { "motion": "Death_A", "loop": false } },
///       "transitions": [
///         { "from": "*", "to": "dead", "when": { "param": "dead" }, "priority": 10, "interrupt": true } ] },
///     { "name": "upper", "start": "idle",
///       "states": { "idle": {},                         // no motion: the layer fades out
///                   "attack": { "motion": "1H_Melee_Attack_Chop", "loop": false, "interruptible": false } },
///       "transitions": [
///         { "from": "idle", "to": "attack", "when": { "all": [ { "param": "attack" },
///                                                             { "param": "dead", "op": "==", "value": false } ] } },
///         { "from": "attack", "to": "idle", "when": { "end": true } } ] }
///   ]
/// }
/// ```
///
/// Layer i of the graph drives layer i of the Animator (0: the whole body, 1: the upper body).
/// A state plays a clip or a blend space of the set (or nothing). Conditions are structured, never
/// parsed from text: {"param", "op", "value"} (op one of `== != < <= > >=`, a bool only `==` and
/// `!=`; a trigger or a bool alone means "set", and a trigger never takes an op), {"end": true}
/// (the layer's clip played once has ended, or the layer fades out), {"all": [...]},
/// {"any": [...]}, {"not": {...}}. A transition without "when" always holds.
///
/// Order: the layers in turn, the whole body first; the transitions of a layer are tried by
/// priority (highest first), then in file order; the first that holds is taken. A trigger is
/// taken at most once by each layer (one "dead" makes the body fall and the upper body let go).
/// "from": "*" is any state (except the target itself unless "self": true, which plays the motion
/// again from its start). A state with "interruptible": false only leaves by a transition marked
/// "interrupt": true until its clip ends. A layer chains at most 4 transitions in one evaluation,
/// and enters a state again at most once.
///
/// The structures of this file are the parsed form of that JSON (AnimationGraph::parse()): the
/// game does not build them by hand, it drives the graph through Animator::start_graph(),
/// Animator::set_parameter() and Animator::fire().
///
/// A parameter of the graph: a value the game sets (key "parameters": name to type).
struct GraphParameter {
    /// The type named in the file: "float", "int" (values rounded), "bool" (0 or 1), "trigger"
    /// (an event, set for a single evaluation by Animator::fire()).
    enum class Type { Float, Int, Bool, Trigger };
    std::string name;         ///< the key in "parameters" (JSON objects are read sorted by key)
    Type type = Type::Float;  ///< see Type
};

/// A condition of a transition (key "when"), as a tree.
struct GraphCondition {
    /// What the condition tests: Always (no "when"), Compare ({"param", "op", "value"}), Set (a
    /// bool or a trigger alone), End ({"end": true}), All, Any, Not (the "all", "any", "not" keys).
    enum class Kind { Always, Compare, Set, End, All, Any, Not };
    /// The comparison of a Compare: `==`, `!=`, `<`, `<=`, `>`, `>=` (exact, on doubles).
    enum class Op { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
    Kind kind = Kind::Always;  ///< see Kind
    int parameter = -1;  ///< Compare, Set: index in AnimationGraph::parameters
    Op op = Op::Equal;   ///< Compare only
    double value = 0.0;  ///< a bool as 0 or 1
    std::vector<GraphCondition> children;  ///< All, Any, Not (one)
};

/// A state of a graph layer (an entry of "states"): the motion the layer plays while in it.
struct GraphState {
    std::string name;    ///< its key in "states" (JSON objects are read sorted by key)
    std::string motion;  ///< a clip or a blend space of the set; empty: the layer plays nothing
    bool loop = true;    ///< "loop": false plays the clip once (then the End condition holds)
    bool restart = false;  ///< entering it plays its motion from the start even if it already plays
    /// "interruptible": false: until its clip ends, only transitions marked "interrupt" leave it.
    bool interruptible = true;
    bool match_speed = false;  ///< see PlayOptions::match_speed
};

/// A transition of a graph layer (an entry of "transitions").
struct GraphTransition {
    int from = -1;  ///< index in the layer's states; -1: any state ("from": "*")
    int to = 0;     ///< index in the layer's states
    GraphCondition when;  ///< Kind::Always when the file gives no "when"
    /// The crossfade into the target, in ticks; -1: the target clip's fade (see
    /// AnimationSet::fade_ticks()), or the set's default when the target plays nothing.
    int fade_ticks = -1;
    int priority = 0;        ///< higher first; equal priorities keep their file order
    bool interrupt = false;  ///< may leave a state that is not interruptible before its clip ends
    /// With "from": "*", may also enter the state the layer is in (its motion plays again from its
    /// start); without it, a "*" transition skips its own target.
    bool self = false;
};

/// One layer of the graph: its states and transitions. Layer i drives layer i of the Animator.
struct GraphLayer {
    std::string name;  ///< "name" in the file; defaults to "body" for layer 0, "upper" for layer 1
    int start = 0;     ///< index in `states` of the state the layer enters at Animator::start_graph()
    std::vector<GraphState> states;  ///< at least one
    std::vector<GraphTransition> transitions;  ///< tried in this order (sorted by priority at load)

    int state(const std::string& name) const;  ///< the index in `states` of the state `name`; -1 if none
};

/// A whole animation graph, as read from the "graph" key of an AnimationSet file (see the format
/// above). Reach it through AnimationSet::graph().
struct AnimationGraph {
    std::vector<GraphParameter> parameters;  ///< the "parameters" entries
    std::vector<GraphLayer> layers;  ///< one or two (Animator::kLayers): the whole body, then the upper body
    std::uint64_t id = 0;            ///< a fresh number for every graph read: who uses it sees a reload

    /// No layer: the set has no graph (a parsed graph always has one layer at least).
    bool empty() const { return layers.empty(); }
    int parameter(const std::string& name) const;  ///< the index in `parameters` of `name`; -1 if none

    /// Reads the "graph" object. Throws std::runtime_error naming the place ("graph > layers[1] >
    /// transitions[0] > when > param: 'attaq' is not a parameter"), and a missing required key or a
    /// value of the wrong JSON type as a std::runtime_error too ("graph: " and nlohmann's message).
    /// A non-empty `source` (usually the file's path) starts every message, in quotes;
    /// AnimationSet::parse() names its file itself. Motions are not checked here: see
    /// Animator::start_graph().
    static AnimationGraph parse(const nlohmann::json& graph, const std::string& source = {});
};

/// Whether `condition` holds, with the parameters' values (`values`, one per
/// AnimationGraph::parameters; triggers: 1 when fired) and whether the layer's clip has ended
/// (`ended`). `used` (if given) collects the parameters that Set conditions found set, triggers
/// included but not under a Not: the triggers a transition taken on this condition consumes.
bool graph_condition_holds(const GraphCondition& condition, const std::vector<double>& values, bool ended,
                           std::vector<int>* used = nullptr);

}  // namespace moteur
