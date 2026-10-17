#pragma once

#include <entt/entity/fwd.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "moteur/animation_clock.hpp"
#include "moteur/animation_set.hpp"
#include "moteur/asset_cache.hpp"
#include "moteur/skeleton.hpp"

namespace moteur {

/// How to start a motion (a clip, or a blend space of the AnimationSet) on an Animator.
struct PlayOptions {
    int layer = 0;         ///< 0: the whole body; 1: the upper body (the set's upper mask)
    /// The crossfade, in ticks; -1: the set's for this motion (AnimationSet::fade_ticks()), 0
    /// without a set: at once.
    int fade_ticks = -1;
    bool loop = true;      ///< a blend space always loops
    bool restart = false;  ///< false: asking for what the layer already plays changes nothing
    /// A clip with a ground speed plays at the pace of the move speed (set_move_speed): no sliding
    /// feet. A blend space always does, past its fastest clip.
    bool match_speed = false;
};

/// An event of a clip (see ClipEvent) that fired during a tick: advance_animators() lists them for
/// the game to read in its update() (footsteps, the moment a blow lands). Never while drawing.
struct AnimatorEvent {
    entt::entity entity{};  ///< the entity whose Animator fired it (the one given to Animator::advance())
    std::string name;    ///< as in the description file
    std::string clip;    ///< the clip it belongs to
    std::string motion;  ///< what the layer plays: the clip, or its blend space
    int layer = 0;       ///< the Animator layer that played it: 0 or 1
    int weight = 0;      ///< of the clip when it fired, thousandths
};

/// What drawing needs from an Animator for one frame: its clips, each with its place and weight,
/// ready for PoseSampler::blend. Filled by Animator::pose(); the joint weights point into it and
/// into the Animator: valid until the next pose() into the same request, or until the Animator is
/// destroyed. Keep one and reuse it: pose() then allocates nothing once it has grown.
struct PoseRequest {
    std::vector<PoseInput> inputs;  ///< the clips to blend; none: the rest pose
    std::vector<float> base_weights;  ///< the whole-body layer where the upper layer takes over
};

/// Plays skeletal clips on an entity (milestone 5): which clips, where in them, how much each
/// counts. Game state: times count whole ticks of the fixed step (ClipClock) and weights are
/// thousandths moved tick by tick, so a replay plays the same clips with the same weights on every
/// machine. The pose itself is only computed when drawing (see World::collect and AnimationPose),
/// between the last two ticks: the game never reads it.
///
/// Two layers: 0 moves the whole body, 1 the upper body only (the set's upper mask), over layer 0.
/// A layer plays motions: a clip, or a blend space (clips of a walk placed on the move speed,
/// played in step). play() crossfades from what the layer played to the new motion; interrupted
/// fades keep their weights and fade out from there.
///
/// Not thread-safe; game state, changed in update() on the game's thread (drawing only calls the
/// const pose()). Usually an entity component, advanced by advance_animators(); saved with
/// save_state().
///
/// ```
/// auto animator = Animator::create(assets.skeleton(path), assets.clips(path), assets.animation_set(set));
/// animator.play("locomotion");
/// registry.emplace<Animator>(knight, std::move(animator));
/// ...each fixed step: animator.set_move_speed(speed / scale); then advance_animators(registry);
/// ...on a click: animator.play("1H_Melee_Attack_Chop", {.layer = 1, .loop = false});
/// ```
class Animator {
public:
    static constexpr int kLayers = 2;  ///< layer 0: the whole body; layer 1: the upper body
    static constexpr int kWeightOne = 1000;    ///< weights in thousandths
    static constexpr int kPhaseTicks = 1000;   ///< a blend space's cycle, in thousandths of its cycle
    static constexpr std::size_t kMaxMotions = 4;  ///< per layer: beyond, the faintest one goes
    static constexpr int kEventWeight = 500;       ///< a clip fires its events from half its weight

    /// A clip of a blend space, placed on the move speed (in thousandths of metres per second).
    struct BlendPoint {
        const SkeletalClip* clip = nullptr;  ///< in the Animator's `clips`
        std::int64_t position = 0;  ///< the clip's ground speed (ClipSettings::ground_speed), thousandths of m/s
        float phase = 0.0f;         ///< ClipSettings::phase: [0, 1), where the clip starts in the cycle
    };

    /// An event of a motion, at a mark of its clock (ticks of a clip, or thousandths of a blend
    /// space's cycle): see ClipClock.
    struct MotionEvent {
        std::string name;    ///< ClipEvent::name
        std::string clip;    ///< the clip it belongs to
        int point = -1;      ///< in a blend space: the index in Motion::points of its clip; -1 for a clip
        bool always = false; ///< ClipEvent::always
    };

    /// One clip or blend space in a layer, with its time and its weight in the layer. Read-only
    /// for the game (Animator::layer(), Animator::dominant()): debug tools and saves.
    struct Motion {
        std::string name;                    ///< the name given to play(): a clip or a blend space
        const SkeletalClip* clip = nullptr;  ///< a clip; null for a blend space
        std::vector<BlendPoint> points;      ///< a blend space's clips, by position
        std::int64_t ground_speed = 0;       ///< a clip's, thousandths of m/s (0: none)
        bool match_speed = false;            ///< a clip's pace follows the move speed
        ClipClock clock;                     ///< a clip's ticks, or a blend space's phase (kPhaseTicks)
        std::int64_t previous_time = 0;      ///< clock.time() at the previous tick, for drawing between the two
        bool continuous = false;             ///< false until its first tick: nothing to interpolate from
        std::int64_t rate = kWeightOne;      ///< playback rate from the move speed, thousandths
        int weight = 0;                      ///< thousandths, in its layer
        int previous_weight = 0;             ///< `weight` at the previous tick
        int fade_from = 0;                   ///< the weight its current fade started from, thousandths
        int fade_to = kWeightOne;            ///< the weight its current fade goes to: 0 when fading out
        int fade_ticks = 0;                  ///< the length of its current fade, ticks (0: at once)
        int fade_elapsed = 0;                ///< ticks of the current fade done, up to `fade_ticks`
        int fade_setting = 0;  ///< the crossfade it was started with (the upper layer fades out with it)
        std::vector<int> marks;             ///< the events' points on the clock, sorted
        std::vector<MotionEvent> events;    ///< one per mark

        /// True for a blend space (no single clip), false for a clip.
        bool blend_space() const { return clip == nullptr; }
        /// Where the motion is between the previous tick (alpha 0) and the current one, as a ratio
        /// of its cycle (see Animator::ratio).
        float ratio(float alpha) const;
        /// Its weight in its layer between the previous tick (alpha 0, clamped) and the current one
        /// (alpha 1), as a fraction: [0, 1].
        float weight_at(float alpha) const;
    };

    /// One of the two layers: its motions and, for layer 1, its own weight and fade. Read-only
    /// for the game (Animator::layer()).
    struct Layer {
        std::vector<Motion> motions;  ///< oldest first
        /// The layer's own weight over the layers below (layer 0: always one), thousandths.
        int weight = 0;
        int previous_weight = 0;  ///< `weight` at the previous tick
        int fade_from = 0;        ///< the weight its current fade started from, thousandths
        int fade_to = 0;          ///< the weight its current fade goes to: 0 when fading out
        int fade_ticks = 0;       ///< the length of its current fade, ticks (0: at once)
        int fade_elapsed = 0;     ///< ticks of the current fade done, up to `fade_ticks`
        std::vector<float> mask;  ///< one per joint; empty: the whole body
    };

    /// The skeleton it plays on (shared, hot-reloaded in place by the asset cache). Set by create().
    Asset<Skeleton> skeleton;
    /// The clips it plays, made for that skeleton (shared, hot-reloaded in place). Set by create().
    Asset<ClipLibrary> clips;
    Asset<AnimationSet> set;  ///< may be empty: plain clips, no crossfade by default

    /// An Animator playing nothing (the rest pose), at x1, move speed 0, graph not started. Throws
    /// std::runtime_error if `skeleton` or `clips` is empty, if the clips were made for another
    /// skeleton (ClipLibrary::mismatch), or if the set's upper mask names a joint the skeleton does
    /// not have.
    static Animator create(Asset<Skeleton> skeleton, Asset<ClipLibrary> clips, Asset<AnimationSet> set = {});

    /// Starts the clip or blend space `name` on a layer. Returns false if the layer already plays it
    /// (the latest motion, not finished) and `restart` is not set: clicking every tick does not
    /// start an attack again. Throws std::runtime_error naming the file if there is no such motion,
    /// or std::invalid_argument for a layer out of range; the Animator is then unchanged. A motion
    /// keeps the animator's speed. Beyond kMaxMotions on a layer, the faintest one is dropped. Call
    /// it in update(), not while drawing; with the graph started, the graph also calls it.
    bool play(const std::string& name, const PlayOptions& options = {});
    /// Shorthand: `name` on layer 0 with the set's crossfade, looping or not, always from its start
    /// (`restart` set: unlike the default PlayOptions, it starts again a motion already playing).
    bool play(const std::string& name, bool loop) { return play(name, PlayOptions{0, -1, loop, true, false}); }
    /// Fades a layer out (layer 1) or its motions to the rest pose (layer 0), over `fade_ticks`
    /// ticks (negative: 0, at once). Throws std::out_of_range for a layer out of range.
    void stop(int layer, int fade_ticks = 0);
    /// No motion: the rest pose, at once (layer 1's weight back to 0). Speeds, ticks and the
    /// graph are kept.
    void clear();

    /// The speed at which the character moves, in metres of the file per second (divide by the
    /// entity's scale): it places the blend spaces on their axis, and makes clips with a ground
    /// speed play at the pace of the feet. Rounded to a thousandth; negative values count as 0.
    /// Applies from the next tick.
    void set_move_speed(double metres_per_second);
    /// The move speed as last set (set_move_speed()), metres of the file per second.
    double move_speed() const { return static_cast<double>(move_speed_) / kWeightOne; }
    /// A multiplier on every motion (0 pauses). Rounded to a thousandth; applies at once. Throws
    /// std::invalid_argument if negative or NaN.
    void set_speed(double multiplier);
    /// The multiplier set by set_speed() (1 by default), a multiple of 0.001.
    double speed() const { return static_cast<double>(speed_) / kWeightOne; }

    /// The motion that counts most on layer 0 (the latest one if two weigh the same); empty without.
    const std::string& clip_name() const;
    /// That motion is a clip played once, and has reached its end (it stays on its last pose).
    bool finished() const;
    /// Whether the layer's latest motion is `name` (and not fading out, nor a clip played once
    /// that has finished). Throws std::out_of_range for a layer out of range.
    bool playing(const std::string& name, int layer = 0) const;
    /// The layer `index` (0 or 1), for debug tools. The reference stays valid as long as the
    /// Animator; its content changes at every play(), stop() and advance(). Throws
    /// std::out_of_range for another index.
    const Layer& layer(int index) const { return layers_.at(static_cast<std::size_t>(index)); }
    /// The motion that counts most on `layer` (the latest one if two weigh the same), null if the
    /// layer plays nothing. The pointer is invalidated by the next play(), stop(), clear(),
    /// advance() or load_state(). Throws std::out_of_range for a layer out of range.
    const Motion* dominant(int layer = 0) const;

    /// Ticks advanced since it was made, and the last event that fired (tick -1: none yet), for
    /// the debug tools, whether or not the game collects the events.
    std::int64_t ticks() const { return ticks_; }
    /// The last event that fired (see last_event()).
    struct LastEvent {
        std::string name;        ///< AnimatorEvent::name
        std::string clip;        ///< AnimatorEvent::clip
        std::int64_t tick = -1;  ///< ticks() when it fired; -1: none yet
    };
    /// The last event that fired, on any layer. The reference stays valid as long as the Animator.
    const LastEvent& last_event() const { return last_event_; }

    /// Puts the dominant motion of layer 0 at `ticks` from its start (tools; no interpolation).
    /// Fractions of a tick are kept (to a thousandth); a clip played once is clamped to its end.
    /// Does nothing when layer 0 plays nothing or a blend space.
    void seek(double ticks);

    /// Where the dominant motion of layer 0 is, between the previous tick (alpha 0) and the current
    /// one (alpha 1), as a ratio of its duration: 0 at its start, 1 at its end. A loop that wrapped
    /// between the two ticks goes on forward (0.95 -> 1.0 -> 0.05), never back through the middle.
    float ratio(float alpha) const;

    /// The clips to blend for drawing at `alpha` (clamped to [0, 1]), with their weights
    /// (interpolated between ticks), into `out` (cleared first): give it to PoseSampler::blend().
    /// No input: the rest pose. Const, and changes nothing: drawing may call it as often as needed.
    void pose(float alpha, PoseRequest& out) const;

    /// One fixed step (or `ticks`): times, fades, blend space weights; the events crossed are added to
    /// `events` (if given), with `entity`. With the graph started, it is evaluated first. A
    /// negative `ticks` counts as 0. See advance_animators().
    void advance(int ticks = 1, std::vector<AnimatorEvent>* events = nullptr, entt::entity entity = {});

    /// The weights of `mask` for this skeleton (see MaskData): one per joint, 0 outside the mask.
    /// Throws std::runtime_error naming the joint if the skeleton does not have it.
    static std::vector<float> mask_weights(const SkeletonData& skeleton, const MaskData& mask);

    // --- The set's animation graph (milestone 7, part 9; see AnimationGraph) ---
    // The game sets parameters; the graph picks the motions. It is evaluated when a parameter
    // changes value, when a trigger fires, and at the start of each advance(): always at a tick,
    // in a defined order, so a replay takes the same transitions.

    /// Starts the graph: each layer enters its start state at once. Without it the graph is
    /// ignored and the game calls play() itself. Throws std::runtime_error naming the file if the
    /// set has no graph or a state names a motion the clips and blend spaces do not have. Starting
    /// it again resets every parameter to 0 and every layer to its start state.
    void start_graph();
    /// Whether start_graph() (or load_state() of a state saved with a graph) has started the graph.
    bool graph_active() const { return graph_.active; }
    /// Sets a parameter (a bool as 0 or 1, an int rounded), and evaluates the graph at once if the
    /// value changed. False if the graph is not started or cannot be used, or has no such
    /// parameter: a file edited while the game runs never stops it (graph_error() says why). For a
    /// trigger, a non-zero value fires it, and the result is fire()'s; 0 does nothing (true).
    ///@{
    bool set_parameter(const std::string& name, double value);
    bool set_parameter(const std::string& name, bool value) { return set_parameter(name, value ? 1.0 : 0.0); }
    bool set_parameter(const std::string& name, int value) { return set_parameter(name, static_cast<double>(value)); }
    ///@}
    double parameter(const std::string& name) const;  ///< its current value; 0 if none (triggers: always 0)
    /// Fires a trigger and evaluates the graph at once: true if a transition took it. A trigger no
    /// transition takes is dropped (it does not wait for a later tick). False also if the graph is
    /// not started or cannot be used, or `name` is not a trigger.
    bool fire(const std::string& name);
    /// The state of a layer ("" without graph, or for a layer the graph does not drive). Throws
    /// std::out_of_range for a layer out of range.
    const std::string& graph_state(int layer) const;
    /// A transition the graph took (see graph_history()).
    struct GraphChange {
        int layer = 0;          ///< the Animator layer: 0 or 1
        /// The state it left, and the state it entered.
        ///@{
        std::string from, to;
        ///@}
        std::int64_t tick = 0;  ///< ticks() when it was taken
    };
    /// The last 16 transitions taken, oldest first (entering the start states is not one), for
    /// the debug tools.
    const std::vector<GraphChange>& graph_history() const { return graph_.history; }
    /// Why the graph is not used since the set was reloaded (it no longer has a graph, or a state
    /// names a motion that does not exist); empty when it works. The motions playing go on.
    const std::string& graph_error() const { return graph_.error; }

    /// The whole state of the Animator (saves, milestone 7, part 6): what plays on each layer, the
    /// clocks, weights and crossfades, speeds, ticks; never the pose. load_state() expects an
    /// Animator created with the same skeleton, clips and description (the game recreates the
    /// character from its definition, then gives it its state back). Throws std::runtime_error if
    /// the state names a clip the library does not have, or is not a state written by save_state();
    /// then the Animator is left exactly as it was (all or nothing). The graph is as saved: a state
    /// saved without a started graph (or loaded into an Animator whose set has none) stops it.
    ///@{
    nlohmann::json save_state() const;
    void load_state(const nlohmann::json& state);
    ///@}

private:
    Motion* dominant_motion(int layer);
    Motion make_motion(const std::string& name, const PlayOptions& options) const;
    /// An event's tick in its clip. Throws std::runtime_error naming the set if it is past the end.
    int event_tick(const ClipEvent& event, const SkeletalClip& clip, int cycle) const;
    static void set_marks(Motion& motion, std::vector<std::pair<int, MotionEvent>> marks);
    /// A blend space's clock speed and weights for the current move speed.
    void pace(Motion& motion) const;
    /// The graph: evaluated with `fired` (a trigger's index, or -1); true if the trigger was taken.
    bool evaluate_graph(int fired);
    /// After a reload of the set: the parameters and states found again by name. False if the graph
    /// cannot be used (graph_.error says why): the motions playing go on.
    bool sync_graph();
    void enter_state(int layer, int state, int fade_ticks, bool restart);
    bool layer_ended(int layer) const;
    std::string check_graph_motions(const AnimationGraph& graph) const;  ///< empty if every motion exists

    struct GraphRuntime {
        bool active = false;
        std::uint64_t id = 0;                ///< the graph the indices are for
        std::vector<std::string> names;      ///< its parameters
        std::vector<double> values;          ///< their values (triggers: always 0 here)
        std::array<int, kLayers> state{-1, -1};
        std::array<std::string, kLayers> state_names;
        std::vector<GraphChange> history;
        std::string error;
    };
    GraphRuntime graph_;

    std::array<Layer, kLayers> layers_;
    std::int64_t speed_ = kWeightOne;
    std::int64_t move_speed_ = 0;           ///< thousandths of m/s, as last set
    std::int64_t tick_move_speed_ = 0;      ///< at the last tick
    std::int64_t previous_move_speed_ = 0;  ///< at the tick before, for drawing between the two
    std::int64_t ticks_ = 0;
    LastEvent last_event_;
};

/// The system: one fixed step (or `ticks`) for every Animator. Call it once per update(), after
/// what decides the clips (a clip started during this tick shows its first pose, and fires its
/// events at 0). The events crossed are added to `events`, entity by entity, in the order of the
/// registry: the same on every machine.
void advance_animators(entt::registry& registry, int ticks = 1, std::vector<AnimatorEvent>* events = nullptr);

/// What drawing last computed for an animated entity: its pose, in model space. A cache owned by
/// the World, not game state; debug tools read it (the skeleton in lines). Absent for an entity
/// that was never visible.
struct AnimationPose {
    PoseSampler sampler;  ///< its model() is the pose (see draw_skeleton())
    std::uint64_t collection = 0;  ///< the World::collect() it was sampled for
};

}  // namespace moteur
