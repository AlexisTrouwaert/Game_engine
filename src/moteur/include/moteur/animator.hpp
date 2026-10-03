#pragma once

#include <entt/entity/fwd.hpp>

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

// How to start a motion (a clip, or a blend space of the AnimationSet) on an Animator.
struct PlayOptions {
    int layer = 0;         // 0: the whole body; 1: the upper body (the set's upper mask)
    int fade_ticks = -1;   // the crossfade; -1: the set's (0 without a set: at once)
    bool loop = true;      // a blend space always loops
    bool restart = false;  // false: asking for what the layer already plays changes nothing
    // A clip with a ground speed plays at the pace of the move speed (set_move_speed): no sliding
    // feet. A blend space always does, past its fastest clip.
    bool match_speed = false;
};

// An event of a clip (see ClipEvent) that fired during a tick: advance_animators() lists them for
// the game to read in its update() (footsteps, the moment a blow lands). Never while drawing.
struct AnimatorEvent {
    entt::entity entity{};
    std::string name;    // as in the description file
    std::string clip;    // the clip it belongs to
    std::string motion;  // what the layer plays: the clip, or its blend space
    int layer = 0;
    int weight = 0;      // of the clip when it fired, thousandths
};

// What drawing needs from an Animator for one frame: its clips, each with its place and weight,
// ready for PoseSampler::blend. Filled by Animator::pose(); the joint weights point into it and
// into the Animator.
struct PoseRequest {
    std::vector<PoseInput> inputs;
    std::vector<float> base_weights;  // the whole-body layer where the upper layer takes over
};

// Plays skeletal clips on an entity (milestone 5): which clips, where in them, how much each
// counts. Game state: times count whole ticks of the fixed step (ClipClock) and weights are
// thousandths moved tick by tick, so a replay plays the same clips with the same weights on every
// machine. The pose itself is only computed when drawing (see World::collect and AnimationPose),
// between the last two ticks: the game never reads it.
//
// Two layers: 0 moves the whole body, 1 the upper body only (the set's upper mask), over layer 0.
// A layer plays motions: a clip, or a blend space (clips of a walk placed on the move speed,
// played in step). play() crossfades from what the layer played to the new motion; interrupted
// fades keep their weights and fade out from there.
//
//   auto animator = Animator::create(assets.skeleton(path), assets.clips(path), assets.animation_set(set));
//   animator.play("locomotion");
//   registry.emplace<Animator>(knight, std::move(animator));
//   ...each fixed step: animator.set_move_speed(speed / scale); then advance_animators(registry);
//   ...on a click: animator.play("1H_Melee_Attack_Chop", {.layer = 1, .loop = false});
class Animator {
public:
    static constexpr int kLayers = 2;
    static constexpr int kWeightOne = 1000;    // weights in thousandths
    static constexpr int kPhaseTicks = 1000;   // a blend space's cycle, in thousandths of its cycle
    static constexpr std::size_t kMaxMotions = 4;  // per layer: beyond, the faintest one goes
    static constexpr int kEventWeight = 500;       // a clip fires its events from half its weight

    // A clip of a blend space, placed on the move speed (in thousandths of metres per second).
    struct BlendPoint {
        const SkeletalClip* clip = nullptr;
        std::int64_t position = 0;
        float phase = 0.0f;
    };

    // An event of a motion, at a mark of its clock (ticks of a clip, or thousandths of a blend
    // space's cycle): see ClipClock.
    struct MotionEvent {
        std::string name;
        std::string clip;
        int point = -1;  // in a blend space: the clip it belongs to
        bool always = false;
    };

    // One clip or blend space in a layer, with its time and its weight in the layer.
    struct Motion {
        std::string name;
        const SkeletalClip* clip = nullptr;  // a clip; null for a blend space
        std::vector<BlendPoint> points;      // a blend space's clips, by position
        std::int64_t ground_speed = 0;       // a clip's, thousandths of m/s (0: none)
        bool match_speed = false;            // a clip's pace follows the move speed
        ClipClock clock;                     // a clip's ticks, or a blend space's phase (kPhaseTicks)
        std::int64_t previous_time = 0;
        bool continuous = false;             // false until its first tick: nothing to interpolate from
        std::int64_t rate = kWeightOne;      // playback rate from the move speed, thousandths
        int weight = 0;                      // thousandths, in its layer
        int previous_weight = 0;
        int fade_from = 0;
        int fade_to = kWeightOne;
        int fade_ticks = 0;
        int fade_elapsed = 0;
        int fade_setting = 0;  // the crossfade it was started with (the upper layer fades out with it)
        std::vector<int> marks;             // the events' points on the clock, sorted
        std::vector<MotionEvent> events;    // one per mark

        bool blend_space() const { return clip == nullptr; }
        // Where the motion is between the previous tick (alpha 0) and the current one, as a ratio
        // of its cycle (see Animator::ratio).
        float ratio(float alpha) const;
        float weight_at(float alpha) const;
    };

    struct Layer {
        std::vector<Motion> motions;  // oldest first
        // The layer's own weight over the layers below (layer 0: always one), thousandths.
        int weight = 0;
        int previous_weight = 0;
        int fade_from = 0;
        int fade_to = 0;
        int fade_ticks = 0;
        int fade_elapsed = 0;
        std::vector<float> mask;  // one per joint; empty: the whole body
    };

    Asset<Skeleton> skeleton;
    Asset<ClipLibrary> clips;
    Asset<AnimationSet> set;  // may be empty: plain clips, no crossfade by default

    // Throws std::runtime_error if the clips were made for another skeleton (ClipLibrary::mismatch),
    // or if the set's upper mask names a joint the skeleton does not have.
    static Animator create(Asset<Skeleton> skeleton, Asset<ClipLibrary> clips, Asset<AnimationSet> set = {});

    // Starts the clip or blend space `name` on a layer. Returns false if the layer already plays it
    // (the latest motion, not finished) and `restart` is not set: clicking every tick does not
    // start an attack again. Throws std::runtime_error naming the file if there is no such motion,
    // or std::invalid_argument for a layer out of range. A motion keeps the animator's speed.
    bool play(const std::string& name, const PlayOptions& options = {});
    bool play(const std::string& name, bool loop) { return play(name, PlayOptions{0, -1, loop, true, false}); }
    // Fades a layer out (layer 1) or its motions to the rest pose (layer 0).
    void stop(int layer, int fade_ticks = 0);
    // No motion: the rest pose, at once.
    void clear();

    // The speed at which the character moves, in metres of the file per second (divide by the
    // entity's scale): it places the blend spaces on their axis, and makes clips with a ground
    // speed play at the pace of the feet. Rounded to a thousandth. Applies from the next tick.
    void set_move_speed(double metres_per_second);
    double move_speed() const { return static_cast<double>(move_speed_) / kWeightOne; }
    // A multiplier on every motion (0 pauses). Rounded to a thousandth.
    void set_speed(double multiplier);
    double speed() const { return static_cast<double>(speed_) / kWeightOne; }

    // The motion that counts most on layer 0 (the latest one if two weigh the same); empty without.
    const std::string& clip_name() const;
    // That motion is a clip played once, and has reached its end (it stays on its last pose).
    bool finished() const;
    // Whether the layer's latest motion is `name` (and not fading out).
    bool playing(const std::string& name, int layer = 0) const;
    const Layer& layer(int index) const { return layers_.at(static_cast<std::size_t>(index)); }
    const Motion* dominant(int layer = 0) const;

    // Ticks advanced since it was made, and the last event that fired (tick -1: none yet), for
    // the debug tools, whether or not the game collects the events.
    std::int64_t ticks() const { return ticks_; }
    struct LastEvent {
        std::string name;
        std::string clip;
        std::int64_t tick = -1;
    };
    const LastEvent& last_event() const { return last_event_; }

    // Puts the dominant motion of layer 0 at `ticks` from its start (tools; no interpolation).
    void seek(double ticks);

    // Where the dominant motion of layer 0 is, between the previous tick (alpha 0) and the current
    // one (alpha 1), as a ratio of its duration: 0 at its start, 1 at its end. A loop that wrapped
    // between the two ticks goes on forward (0.95 -> 1.0 -> 0.05), never back through the middle.
    float ratio(float alpha) const;

    // The clips to blend for drawing at `alpha`, with their weights (interpolated between ticks).
    // No input: the rest pose.
    void pose(float alpha, PoseRequest& out) const;

    // One fixed step (or `ticks`): times, fades, blend space weights; the events crossed are added to
    // `events` (if given), with `entity`. See advance_animators().
    void advance(int ticks = 1, std::vector<AnimatorEvent>* events = nullptr, entt::entity entity = {});

    // The weights of `mask` for this skeleton (see MaskData). Throws std::runtime_error naming the
    // joint if the skeleton does not have it.
    static std::vector<float> mask_weights(const SkeletonData& skeleton, const MaskData& mask);

private:
    Motion* dominant_motion(int layer);
    Motion make_motion(const std::string& name, const PlayOptions& options) const;
    // An event's tick in its clip. Throws std::runtime_error naming the set if it is past the end.
    int event_tick(const ClipEvent& event, const SkeletalClip& clip, int cycle) const;
    static void set_marks(Motion& motion, std::vector<std::pair<int, MotionEvent>> marks);
    // A blend space's clock speed and weights for the current move speed.
    void pace(Motion& motion) const;

    std::array<Layer, kLayers> layers_;
    std::int64_t speed_ = kWeightOne;
    std::int64_t move_speed_ = 0;           // thousandths of m/s, as last set
    std::int64_t tick_move_speed_ = 0;      // at the last tick
    std::int64_t previous_move_speed_ = 0;  // at the tick before, for drawing between the two
    std::int64_t ticks_ = 0;
    LastEvent last_event_;
};

// The system: one fixed step (or `ticks`) for every Animator. Call it once per update(), after
// what decides the clips (a clip started during this tick shows its first pose, and fires its
// events at 0). The events crossed are added to `events`, entity by entity, in the order of the
// registry: the same on every machine.
void advance_animators(entt::registry& registry, int ticks = 1, std::vector<AnimatorEvent>* events = nullptr);

// What drawing last computed for an animated entity: its pose, in model space. A cache owned by
// the World, not game state; debug tools read it (the skeleton in lines). Absent for an entity
// that was never visible.
struct AnimationPose {
    PoseSampler sampler;
    std::uint64_t collection = 0;  // the World::collect() it was sampled for
};

}  // namespace moteur
