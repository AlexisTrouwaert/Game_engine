#include "moteur/animator.hpp"

#include "moteur/profiler.hpp"

#include <entt/entity/registry.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace moteur {

namespace {

// Thousandths, from a value given in units.
std::int64_t thousandths(double value) {
    return std::llround(value * Animator::kWeightOne);
}

// One step of a fade: `elapsed` ticks of `ticks` from `from` to `to` (integers: the same weights
// on every machine).
int faded(int from, int to, int elapsed, int ticks) {
    if (ticks <= 0 || elapsed >= ticks) {
        return to;
    }
    return from + static_cast<int>(static_cast<std::int64_t>(to - from) * elapsed / ticks);
}

// A blend space's two neighbours around `position` and the weight of the second, in [0, 1].
struct Neighbours {
    std::size_t first = 0;
    std::size_t second = 0;
    double t = 0.0;
};

// The same in integers, at a tick: the first neighbour and the second's weight in thousandths.
struct Share {
    std::size_t first = 0;
    std::int64_t t = 0;
};

Share share(const std::vector<Animator::BlendPoint>& points, std::int64_t speed) {
    Share s;
    if (speed >= points.back().position) {
        s.first = points.size() - 2;
        s.t = Animator::kWeightOne;
    } else if (speed > points.front().position) {
        while (s.first + 2 < points.size() && speed >= points[s.first + 1].position) {
            ++s.first;
        }
        s.t = (speed - points[s.first].position) * Animator::kWeightOne /
              (points[s.first + 1].position - points[s.first].position);
    }
    return s;
}

Neighbours neighbours(const std::vector<Animator::BlendPoint>& points, double position) {
    Neighbours n;
    if (position <= static_cast<double>(points.front().position)) {
        return n;
    }
    if (position >= static_cast<double>(points.back().position)) {
        n.first = n.second = points.size() - 1;
        return n;
    }
    std::size_t i = 0;
    while (i + 2 < points.size() && position >= static_cast<double>(points[i + 1].position)) {
        ++i;
    }
    n.first = i;
    n.second = i + 1;
    const auto a = static_cast<double>(points[i].position);
    const auto b = static_cast<double>(points[i + 1].position);
    n.t = (position - a) / (b - a);
    return n;
}

}  // namespace

float Animator::Motion::ratio(float alpha) const {
    const auto cycle = static_cast<double>(clock.cycle_length());
    double time = static_cast<double>(clock.time());  // never wrapped: a loop keeps counting
    if (continuous) {
        const auto previous = static_cast<double>(previous_time);
        time = previous + (time - previous) * static_cast<double>(std::clamp(alpha, 0.0f, 1.0f));
    }
    if (clock.once()) {
        return static_cast<float>(std::min(time, cycle) / cycle);
    }
    return static_cast<float>(std::fmod(time, cycle) / cycle);
}

float Animator::Motion::weight_at(float alpha) const {
    const float a = std::clamp(alpha, 0.0f, 1.0f);
    return (static_cast<float>(previous_weight) + (static_cast<float>(weight - previous_weight)) * a) /
           static_cast<float>(kWeightOne);
}

Animator Animator::create(Asset<Skeleton> skeleton, Asset<ClipLibrary> clips, Asset<AnimationSet> set) {
    if (!skeleton || !clips) {
        throw std::runtime_error("Animator: no skeleton or no clips");
    }
    if (const std::string why = clips->mismatch(*skeleton); !why.empty()) {
        throw std::runtime_error("Animator: the clips of '" + clips->source() + "' do not fit the skeleton of '" +
                                 skeleton->name() + "': " + why);
    }
    Animator animator;
    if (set && !set->upper_mask().empty()) {
        try {
            animator.layers_[1].mask = mask_weights(skeleton->data(), *set->mask(set->upper_mask()));
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("Animator: animation set '" + set->source() + "', skeleton '" + skeleton->name() +
                                     "': " + e.what());
        }
    }
    animator.layers_[0].weight = animator.layers_[0].previous_weight = kWeightOne;
    animator.layers_[0].fade_from = animator.layers_[0].fade_to = kWeightOne;
    animator.skeleton = std::move(skeleton);
    animator.clips = std::move(clips);
    animator.set = std::move(set);
    return animator;
}

std::vector<float> Animator::mask_weights(const SkeletonData& skeleton, const MaskData& mask) {
    const int root = skeleton.find(mask.joint);
    if (root < 0) {
        throw std::runtime_error("the mask's joint '" + mask.joint + "' is not in the skeleton");
    }
    // Joints come parents first: one pass finds the subtree and each joint's depth in it.
    std::vector<int> depth(skeleton.joints.size(), -1);
    std::vector<float> weights(skeleton.joints.size(), 0.0f);
    for (std::size_t j = 0; j < skeleton.joints.size(); ++j) {
        const int parent = skeleton.joints[j].parent;
        if (static_cast<int>(j) == root) {
            depth[j] = 0;
        } else if (parent >= 0 && depth[static_cast<std::size_t>(parent)] >= 0) {
            depth[j] = depth[static_cast<std::size_t>(parent)] + 1;
        } else {
            continue;
        }
        weights[j] = depth[j] < mask.ramp ? static_cast<float>(depth[j] + 1) / static_cast<float>(mask.ramp + 1) : 1.0f;
    }
    return weights;
}

Animator::Motion Animator::make_motion(const std::string& name, const PlayOptions& options) const {
    Motion motion;
    motion.name = name;
    motion.fade_setting = std::max(0, options.fade_ticks);
    if (const BlendSpaceData* space = set ? set->blend_space(name) : nullptr) {
        for (std::size_t i = 0; i < space->clips.size(); ++i) {
            BlendPoint point;
            point.clip = &clips->clip(space->clips[i]);  // throws, naming the file
            point.position = thousandths(space->positions[i]);
            point.phase = set->clip(space->clips[i]).phase;
            motion.points.push_back(point);
        }
        motion.clock = ClipClock(kPhaseTicks, false);
        // Each clip's events, at their place in the shared cycle.
        std::vector<std::pair<int, MotionEvent>> marks;
        for (std::size_t i = 0; i < motion.points.size(); ++i) {
            const BlendPoint& point = motion.points[i];
            const int cycle = point.clip->duration_ticks();
            for (const ClipEvent& event : set->clip(point.clip->name()).events) {
                const double ratio = static_cast<double>(event_tick(event, *point.clip, cycle)) / cycle;
                const double phase = std::fmod(ratio - static_cast<double>(point.phase) + 1.0, 1.0);
                const int mark = static_cast<int>(std::llround(phase * kPhaseTicks)) % kPhaseTicks;
                marks.push_back({mark, {event.name, point.clip->name(), static_cast<int>(i), event.always}});
            }
        }
        set_marks(motion, std::move(marks));
        return motion;
    }
    motion.clip = &clips->clip(name);  // throws, naming the file, before anything changes
    const int cycle = motion.clip->duration_ticks();
    motion.clock = ClipClock(cycle, !options.loop);
    motion.match_speed = options.match_speed;
    if (set) {
        motion.ground_speed = thousandths(set->clip(name).ground_speed);
        std::vector<std::pair<int, MotionEvent>> marks;
        for (const ClipEvent& event : set->clip(name).events) {
            // A loop's end is its start; a clip played once may have an event on its very end.
            const int tick = event_tick(event, *motion.clip, cycle);
            marks.push_back({options.loop ? tick % cycle : tick, {event.name, name, -1, event.always}});
        }
        set_marks(motion, std::move(marks));
    }
    return motion;
}

int Animator::event_tick(const ClipEvent& event, const SkeletalClip& clip, int cycle) const {
    const int tick = static_cast<int>(std::lround(event.time * static_cast<float>(kClipTicksPerSecond)));
    if (tick > cycle) {
        throw std::runtime_error("Animation set '" + set->source() + "': clip '" + clip.name() + "': event '" +
                                 event.name + "' at " + std::to_string(event.time) + " s is past its end (" +
                                 std::to_string(clip.duration()) + " s)");
    }
    return tick;
}

void Animator::set_marks(Motion& motion, std::vector<std::pair<int, MotionEvent>> marks) {
    std::stable_sort(marks.begin(), marks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [mark, event] : marks) {
        motion.marks.push_back(mark);
        motion.events.push_back(std::move(event));
    }
}

void Animator::pace(Motion& motion) const {
    const std::int64_t speed = tick_move_speed_;
    if (!motion.blend_space()) {
        motion.rate = motion.match_speed && motion.ground_speed > 0 ? speed * kWeightOne / motion.ground_speed : kWeightOne;
        motion.clock.set_speed_thousandths(speed_ * motion.rate / kWeightOne);
        return;
    }
    // Between two clips, the blend moves at the given speed by construction; beyond the ends, the
    // end clip plays faster or slower.
    const std::vector<BlendPoint>& points = motion.points;
    const BlendPoint& first = points.front();
    const BlendPoint& last = points.back();
    if (speed > last.position && last.position > 0) {
        motion.rate = speed * kWeightOne / last.position;
    } else if (speed < first.position) {
        motion.rate = speed * kWeightOne / first.position;
    } else {
        motion.rate = kWeightOne;
    }
    // The cycle lasts as long as the two neighbours' durations, weighted: they stay in step.
    const Share s = share(points, speed);
    const std::size_t a = s.first;
    const std::int64_t t = s.t;  // thousandths of the second neighbour
    const std::int64_t da = static_cast<std::int64_t>(points[a].clip->duration_ticks()) * ClipClock::kOne;
    const std::int64_t db = static_cast<std::int64_t>(points[a + 1].clip->duration_ticks()) * ClipClock::kOne;
    const std::int64_t duration = std::max<std::int64_t>(1, (da * (kWeightOne - t) + db * t) / kWeightOne);
    // The phase clock counts a cycle as kPhaseTicks ticks: per tick, cycle / duration of it.
    // (At most 1e6 x 1e3 x a few 1e3 x a few 1e3: well within 64 bits.)
    const std::int64_t cycle = motion.clock.cycle_length();
    motion.clock.set_speed_thousandths(cycle * ClipClock::kOne * speed_ * motion.rate /
                                       (duration * std::int64_t{kWeightOne} * kWeightOne));
}

bool Animator::play(const std::string& name, const PlayOptions& options) {
    if (options.layer < 0 || options.layer >= kLayers) {
        throw std::invalid_argument("Animator: no layer " + std::to_string(options.layer));
    }
    if (!options.restart && playing(name, options.layer)) {
        return false;
    }
    PlayOptions resolved = options;
    if (resolved.fade_ticks < 0) {
        resolved.fade_ticks = set ? set->fade_ticks(name) : 0;
    }
    Motion motion = make_motion(name, resolved);
    pace(motion);
    Layer& layer = layers_[static_cast<std::size_t>(options.layer)];
    const int fade = resolved.fade_ticks;
    const bool layer_idle = options.layer > 0 && layer.fade_to == 0 && layer.weight == 0;

    if (fade == 0 || layer_idle) {
        // At once within the layer (an idle upper layer fades in as a whole, below).
        layer.motions.clear();
        motion.weight = motion.previous_weight = motion.fade_from = motion.fade_to = kWeightOne;
    } else {
        for (Motion& old : layer.motions) {
            old.fade_from = old.weight;
            old.fade_to = 0;
            old.fade_ticks = fade;
            old.fade_elapsed = 0;
        }
        motion.fade_from = 0;
        motion.fade_to = kWeightOne;
        motion.fade_ticks = fade;
        if (layer.motions.size() + 1 > kMaxMotions) {
            const auto faintest = std::min_element(layer.motions.begin(), layer.motions.end(),
                                                   [](const Motion& a, const Motion& b) { return a.weight < b.weight; });
            layer.motions.erase(faintest);
        }
    }
    layer.motions.push_back(std::move(motion));

    if (options.layer > 0 && layer.fade_to != kWeightOne) {
        layer.fade_from = layer.weight;
        layer.fade_to = kWeightOne;
        layer.fade_ticks = fade;
        layer.fade_elapsed = 0;
        if (fade == 0) {
            layer.weight = layer.previous_weight = kWeightOne;
        }
    }
    return true;
}

void Animator::stop(int index, int fade_ticks) {
    Layer& layer = layers_.at(static_cast<std::size_t>(index));
    fade_ticks = std::max(0, fade_ticks);
    if (index == 0) {
        for (Motion& motion : layer.motions) {
            motion.fade_from = motion.weight;
            motion.fade_to = 0;
            motion.fade_ticks = fade_ticks;
            motion.fade_elapsed = 0;
        }
        if (fade_ticks == 0) {
            layer.motions.clear();
        }
        return;
    }
    layer.fade_from = layer.weight;
    layer.fade_to = 0;
    layer.fade_ticks = fade_ticks;
    layer.fade_elapsed = 0;
    if (fade_ticks == 0) {
        layer.weight = layer.previous_weight = 0;
        layer.motions.clear();
    }
}

void Animator::clear() {
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        layers_[i].motions.clear();
        if (i > 0) {
            layers_[i].weight = layers_[i].previous_weight = layers_[i].fade_from = layers_[i].fade_to = 0;
        }
    }
}

void Animator::set_move_speed(double metres_per_second) {
    move_speed_ = std::max<std::int64_t>(0, thousandths(metres_per_second));
}

void Animator::set_speed(double multiplier) {
    if (!(multiplier >= 0.0)) {  // also rejects NaN
        throw std::invalid_argument("animation speed must be zero or positive, got " + std::to_string(multiplier));
    }
    speed_ = thousandths(multiplier);
    for (Layer& layer : layers_) {
        for (Motion& motion : layer.motions) {
            pace(motion);
        }
    }
}

const Animator::Motion* Animator::dominant(int index) const {
    return const_cast<Animator*>(this)->dominant_motion(index);
}

Animator::Motion* Animator::dominant_motion(int index) {
    Motion* best = nullptr;
    for (Motion& motion : layers_.at(static_cast<std::size_t>(index)).motions) {
        if (best == nullptr || motion.weight >= best->weight) {
            best = &motion;
        }
    }
    return best;
}

const std::string& Animator::clip_name() const {
    static const std::string none;
    const Motion* motion = dominant(0);
    return motion != nullptr ? motion->name : none;
}

bool Animator::finished() const {
    const Motion* motion = dominant(0);
    return motion != nullptr && !motion->blend_space() && motion->clock.finished();
}

bool Animator::playing(const std::string& name, int index) const {
    const Layer& layer = layers_.at(static_cast<std::size_t>(index));
    if (layer.motions.empty() || (index > 0 && layer.fade_to == 0)) {
        return false;
    }
    const Motion& latest = layer.motions.back();
    return latest.name == name && latest.fade_to == kWeightOne && !latest.clock.finished();
}

void Animator::seek(double ticks) {
    Motion* found = dominant_motion(0);
    if (found == nullptr || found->blend_space()) {
        return;
    }
    Motion& motion = *found;
    motion.clock.set_time(std::llround(ticks * static_cast<double>(ClipClock::kOne)));
    motion.previous_time = motion.clock.time();
    motion.continuous = false;
}

float Animator::ratio(float alpha) const {
    const Motion* motion = dominant(0);
    return motion != nullptr ? motion->ratio(alpha) : 0.0f;
}

void Animator::advance(int ticks, std::vector<AnimatorEvent>* events, entt::entity entity) {
    if (graph_.active) {
        evaluate_graph(-1);  // the clips that ended this tick, and what the game set since the last tick
    }
    ticks = std::max(0, ticks);
    ticks_ += ticks;
    std::vector<std::size_t> crossed;
    previous_move_speed_ = tick_move_speed_;
    tick_move_speed_ = move_speed_;
    for (std::size_t index = 0; index < layers_.size(); ++index) {
        Layer& layer = layers_[index];
        layer.previous_weight = layer.weight;
        if (index > 0) {
            layer.fade_elapsed = std::min(layer.fade_ticks, layer.fade_elapsed + ticks);
            layer.weight = faded(layer.fade_from, layer.fade_to, layer.fade_elapsed, layer.fade_ticks);
        }
        for (Motion& motion : layer.motions) {
            motion.previous_weight = motion.weight;
            motion.previous_time = motion.clock.time();
            pace(motion);
            crossed.clear();
            motion.clock.advance(ticks, motion.marks, [&crossed](std::size_t mark) { crossed.push_back(mark); });
            motion.continuous = true;
            motion.fade_elapsed = std::min(motion.fade_ticks, motion.fade_elapsed + ticks);
            motion.weight = faded(motion.fade_from, motion.fade_to, motion.fade_elapsed, motion.fade_ticks);
            if (crossed.empty()) {
                continue;
            }
            // The weight this tick ends with; in a blend space, only the clip that weighs most fires.
            const int weight = index == 0 ? motion.weight : motion.weight * layer.weight / kWeightOne;
            int dominant = -1;
            if (motion.blend_space()) {
                const Share s = share(motion.points, tick_move_speed_);
                dominant = static_cast<int>(s.t > kWeightOne / 2 ? s.first + 1 : s.first);
            }
            for (const std::size_t mark : crossed) {
                const MotionEvent& event = motion.events[mark];
                if (event.point != dominant || weight <= 0 || (weight < kEventWeight && !event.always)) {
                    continue;
                }
                last_event_ = {event.name, event.clip, ticks_};
                if (events != nullptr) {
                    events->push_back({entity, event.name, event.clip, motion.name, static_cast<int>(index), weight});
                }
            }
        }
        // Gone once faded out at both ticks drawing blends between.
        std::erase_if(layer.motions, [](const Motion& motion) {
            return motion.fade_to == 0 && motion.weight == 0 && motion.previous_weight == 0;
        });
        if (index == 0) {
            continue;
        }
        if (layer.fade_to == 0 && layer.weight == 0 && layer.previous_weight == 0) {
            layer.motions.clear();
            continue;
        }
        // An upper-body clip played once fades out by itself, to end with the clip.
        if (layer.fade_to == kWeightOne && !layer.motions.empty()) {
            const Motion& latest = layer.motions.back();
            if (!latest.blend_space() && latest.clock.once() && latest.fade_to == kWeightOne) {
                const std::int64_t left = latest.clock.cycle_length() - latest.clock.time();
                const std::int64_t per_tick = latest.clock.speed_thousandths();
                if (left <= 0) {
                    stop(static_cast<int>(index), latest.fade_setting);
                } else if (per_tick > 0) {
                    const std::int64_t ticks_left = (left + per_tick - 1) / per_tick;
                    if (ticks_left <= latest.fade_setting) {
                        stop(static_cast<int>(index), static_cast<int>(ticks_left));
                    }
                }
            }
        }
    }
}

void Animator::pose(float alpha, PoseRequest& out) const {
    out.inputs.clear();
    out.base_weights.clear();
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const Layer& upper = layers_[1];
    const float u = upper.motions.empty()
                        ? 0.0f
                        : (static_cast<float>(upper.previous_weight) +
                           static_cast<float>(upper.weight - upper.previous_weight) * alpha) /
                              static_cast<float>(kWeightOne);
    const std::size_t joints = skeleton ? static_cast<std::size_t>(skeleton->joint_count()) : 0;
    if (u > 0.0f) {
        // Where the upper layer takes over, the whole-body layer gives way.
        out.base_weights.resize(joints);
        for (std::size_t j = 0; j < joints; ++j) {
            const float m = upper.mask.empty() ? 1.0f : upper.mask[j];
            out.base_weights[j] = 1.0f - u * m;
        }
    }
    const double move = static_cast<double>(previous_move_speed_) +
                        static_cast<double>(tick_move_speed_ - previous_move_speed_) * static_cast<double>(alpha);

    const auto add_layer = [&](const Layer& layer, float layer_weight, std::span<const float> joint_weights) {
        float total = 0.0f;
        for (const Motion& motion : layer.motions) {
            total += motion.weight_at(alpha);
        }
        if (total <= 0.0f || layer_weight <= 0.0f) {
            return;
        }
        for (const Motion& motion : layer.motions) {
            const float weight = motion.weight_at(alpha) / total * layer_weight;
            if (weight <= 1e-4f) {
                continue;
            }
            const float ratio = motion.ratio(alpha);
            if (!motion.blend_space()) {
                out.inputs.push_back({motion.clip, ratio, weight, joint_weights});
                continue;
            }
            const Neighbours n = neighbours(motion.points, move);
            const auto add_point = [&](std::size_t i, double share) {
                if (share * static_cast<double>(weight) <= 1e-4) {
                    return;
                }
                const BlendPoint& point = motion.points[i];
                const float phase = std::fmod(ratio + point.phase, 1.0f);
                out.inputs.push_back({point.clip, phase, weight * static_cast<float>(share), joint_weights});
            };
            add_point(n.first, n.first == n.second ? 1.0 : 1.0 - n.t);
            if (n.first != n.second) {
                add_point(n.second, n.t);
            }
        }
    };
    add_layer(layers_[0], 1.0f, out.base_weights);
    add_layer(upper, u, upper.mask);
}

void advance_animators(entt::registry& registry, int ticks, std::vector<AnimatorEvent>* events) {
    MOTEUR_PROFILE("animation : avance");
    for (auto [entity, animator] : registry.view<Animator>().each()) {
        animator.advance(ticks, events, entity);
    }
}

}  // namespace moteur
