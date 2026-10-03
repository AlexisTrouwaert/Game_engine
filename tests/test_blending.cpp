// Blends and transitions (milestone 5, part 6): crossfades in ticks, blend spaces in step, speed
// matched to the ground, the upper-body layer, and the description file (AnimationSet); and the
// events of the clips (part 7).

#include <doctest/doctest.h>

#include <entt/entity/registry.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/animation_data.hpp"
#include "moteur/animation_set.hpp"
#include "moteur/animator.hpp"
#include "moteur/model.hpp"
#include "moteur/paths.hpp"
#include "moteur/skeleton.hpp"

namespace {

const std::string kAssets = MOTEUR_SOURCE_ASSETS;

// root > hips > spine > chest > arm, and hips > leg: joints at rest where their parent is, so a
// joint's place in model space is the sum of the translations down to it.
moteur::SkeletonData body() {
    moteur::SkeletonData skeleton;
    const auto add = [&](const char* name, int parent) { skeleton.joints.push_back({name, parent, {}}); };
    add("root", -1);
    add("hips", 0);
    add("spine", 1);
    add("chest", 2);
    add("arm", 3);
    add("leg", 1);
    return skeleton;
}

enum Joint { kRoot, kHips, kSpine, kChest, kArm, kLeg };

// A clip of `seconds` that holds every joint at a translation along x (joint j at `x[j]`), and,
// if `move` is set, slides `move` along x from 0 to 1 over the clip (a clock to read).
moteur::ClipData held(const char* name, float seconds, std::map<int, float> x, int move = -1) {
    moteur::ClipData clip;
    clip.name = name;
    clip.duration = seconds;
    clip.tracks.resize(6);
    for (int j = 0; j < 6; ++j) {
        moteur::KeyTrack<glm::vec3>& keys = clip.tracks[static_cast<std::size_t>(j)].translations;
        const float value = x.count(j) != 0 ? x[j] : 0.0f;
        keys.times = {0.0f, seconds};
        keys.values = {glm::vec3(value, 0.0f, 0.0f), glm::vec3(j == move ? value + 1.0f : value, 0.0f, 0.0f)};
    }
    return clip;
}

const char* kSet = R"({
  "version": 1,
  "fade_ticks": 4,
  "clips": {
    "Walk": { "ground_speed": 1.0, "events": [ { "time": 0.3333, "name": "step_b" }, { "time": 0, "name": "step_a" } ] },
    "Run": { "ground_speed": 3.0, "phase": 0.25,
             "events": [ { "time": 0, "name": "step_a" }, { "time": 0.25, "name": "step_b" } ] },
    "Attack": { "fade_ticks": 2, "events": [ { "time": 0.25, "name": "impact", "always": true },
                                             { "time": 0.5, "name": "attack_end" } ] },
    "A": { "events": [ { "time": 0, "name": "a_start" } ] },
    "B": { "events": [ { "time": 0.5, "name": "b_mid" }, { "time": 1.0, "name": "b_end" } ] }
  },
  "blend_spaces": { "move": ["Run", "Idle", "Walk"] },
  "masks": { "upper": { "joint": "spine", "ramp": 1 } },
  "upper_mask": "upper"
})";

// The body, with clips: Idle (1 s, arm at 0), Walk (40 ticks), Run (30 ticks), A, B, C (1 s,
// the arm at 1, 2, 3; A's root slides), and Attack (30 ticks, arm at 10, leg at 10).
struct Body {
    moteur::SkeletonData data = body();
    moteur::Asset<moteur::Skeleton> skeleton = moteur::make_asset(moteur::Skeleton::create(data, "body"));
    moteur::Asset<moteur::ClipLibrary> clips = moteur::make_asset(moteur::ClipLibrary::create(
        data,
        {held("Idle", 1.0f, {}), held("Walk", 40.0f / 60.0f, {{kArm, 1.0f}}, kRoot),
         held("Run", 0.5f, {{kArm, 2.0f}}, kRoot), held("A", 1.0f, {{kArm, 1.0f}}, kRoot),
         held("B", 1.0f, {{kArm, 2.0f}}), held("C", 1.0f, {{kArm, 3.0f}}),
         held("Attack", 0.5f, {{kArm, 10.0f}, {kLeg, 10.0f}})},
        "body.glb"));
    moteur::Asset<moteur::AnimationSet> set = moteur::make_asset(moteur::AnimationSet::parse(kSet, "body.json"));

    moteur::Animator animator(bool with_set = true) const {
        return moteur::Animator::create(skeleton, clips, with_set ? set : moteur::Asset<moteur::AnimationSet>{});
    }
};

std::vector<int> weights(const moteur::Animator& animator, int layer = 0) {
    std::vector<int> result;
    for (const moteur::Animator::Motion& motion : animator.layer(layer).motions) {
        result.push_back(motion.weight);
    }
    return result;
}

// The model-space x of a joint in the pose drawn at `alpha`.
float drawn_x(const Body& body, const moteur::Animator& animator, Joint joint, float alpha = 1.0f) {
    moteur::PoseRequest request;
    animator.pose(alpha, request);
    moteur::PoseSampler sampler;
    sampler.blend(*body.skeleton, request.inputs);
    return sampler.model()[joint][3].x;
}

// Advances `ticks` times one tick (or once `ticks` if `jump`), and gives the events by name, each
// with the tick (1 for the first advance) it fired in.
struct Fired {
    int tick;
    std::string name;
    std::string clip;
    bool operator==(const Fired&) const = default;
};

std::vector<Fired> run(moteur::Animator& animator, int ticks, int first_tick = 1, bool jump = false) {
    std::vector<Fired> fired;
    std::vector<moteur::AnimatorEvent> events;
    for (int tick = 0; tick < (jump ? 1 : ticks); ++tick) {
        events.clear();
        animator.advance(jump ? ticks : 1, &events);
        for (const moteur::AnimatorEvent& event : events) {
            fired.push_back({first_tick + tick, event.name, event.clip});
        }
    }
    return fired;
}

std::vector<std::string> names(const std::vector<Fired>& fired) {
    std::vector<std::string> result;
    for (const Fired& f : fired) {
        result.push_back(f.name);
    }
    return result;
}

}  // namespace

TEST_CASE("AnimationSet: clips, blend spaces sorted by ground speed, masks, and errors") {
    const moteur::AnimationSet set = moteur::AnimationSet::parse(kSet, "body.json");
    CHECK(set.default_fade_ticks() == 4);
    CHECK(set.fade_ticks("Attack") == 2);
    CHECK(set.fade_ticks("Idle") == 4);  // not in the file: the default
    CHECK(set.clip("Run").phase == 0.25f);
    REQUIRE(set.clip("Walk").events.size() == 2);
    CHECK(set.clip("Walk").events[0].name == "step_a");  // sorted by time
    CHECK(set.clip("Attack").events[0].always);
    CHECK_FALSE(set.clip("Attack").events[1].always);
    const moteur::BlendSpaceData* move = set.blend_space("move");
    REQUIRE(move != nullptr);
    CHECK(move->clips == std::vector<std::string>{"Idle", "Walk", "Run"});
    CHECK(move->positions == std::vector<float>{0.0f, 1.0f, 3.0f});
    CHECK(set.upper_mask() == "upper");
    CHECK(set.mask("upper")->ramp == 1);

    const auto fails = [](const std::string& json, const std::string& why) {
        CHECK_THROWS_WITH_AS(moteur::AnimationSet::parse(json, "x.json"), doctest::Contains(why), std::runtime_error);
    };
    fails(R"({"version": 2})", "Animation set 'x.json': format version 2");
    fails(R"({"version": 1, "upper_mask": "none"})", "upper_mask 'none' is not in masks");
    fails(R"({"version": 1, "blend_spaces": {"m": ["A"]}})", "blend space 'm' needs two clips");
    fails(R"({"version": 1, "blend_spaces": {"m": ["A", "B"]}})", "'A' and 'B' have the same ground speed");
    fails(R"({"version": 1, "clips": {"A": {"phase": 1.0}}})", "clip 'A'");
    fails(R"({"version": 1, )", "Animation set 'x.json' is not valid");
    fails(R"({"version": 1, "clips": {"A": {"events": [{"time": 0.1, "name": ""}]}}})", "an event needs a name");
    fails(R"({"version": 1, "clips": {"A": {"events": [{"name": "x"}]}}})", "is not valid");
}

TEST_CASE("Animator: mask weights, ramped down from a joint") {
    const moteur::SkeletonData skeleton = body();
    CHECK(moteur::Animator::mask_weights(skeleton, {"spine", 1}) == std::vector<float>{0.0f, 0.0f, 0.5f, 1.0f, 1.0f, 0.0f});
    CHECK(moteur::Animator::mask_weights(skeleton, {"hips", 0}) == std::vector<float>{0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    CHECK_THROWS_WITH_AS(moteur::Animator::mask_weights(skeleton, {"neck", 0}), doctest::Contains("'neck'"),
                         std::runtime_error);
    // A set whose mask names a joint the skeleton lacks is refused when the Animator is made.
    Body other;
    other.set = moteur::make_asset(moteur::AnimationSet::parse(
        R"({"version": 1, "masks": {"m": {"joint": "neck"}}, "upper_mask": "m"})", "neck.json"));
    CHECK_THROWS_WITH_AS(other.animator(), doctest::Contains("animation set 'neck.json', skeleton 'body'"),
                         std::runtime_error);
}

TEST_CASE("Animator: a crossfade, tick by tick, and what drawing sees between ticks") {
    const Body body;
    moteur::Animator animator = body.animator();
    animator.play("A", {.fade_ticks = 0});
    CHECK(weights(animator) == std::vector<int>{1000});
    CHECK(animator.play("B"));  // the set's 4 ticks
    CHECK(weights(animator) == std::vector<int>{1000, 0});
    const std::vector<std::vector<int>> expected = {{750, 250}, {500, 500}, {250, 750}, {0, 1000}};
    for (const std::vector<int>& step : expected) {
        animator.advance();
        CHECK(weights(animator) == step);
    }
    CHECK(animator.clip_name() == "B");
    // Drawn half way between the ticks 1 and 2 of the fade: 0.625 A (arm at 1), 0.375 B (arm at 2).
    moteur::Animator replay = body.animator();
    replay.play("A", {.fade_ticks = 0});
    replay.play("B");
    replay.advance();
    replay.advance();
    CHECK(drawn_x(body, replay, kArm, 0.5f) - drawn_x(body, replay, kRoot, 0.5f) == doctest::Approx(1.375f).epsilon(1e-3));
    // Faded out at both ticks drawing blends between: gone.
    animator.advance();
    CHECK(weights(animator) == std::vector<int>{1000});
    CHECK(animator.layer(0).motions[0].name == "B");
}

TEST_CASE("Animator: an interrupted crossfade fades out from where it was; weights add up to one") {
    const Body body;
    moteur::Animator animator = body.animator();
    animator.play("A", {.fade_ticks = 0});
    animator.play("B");
    animator.advance();
    animator.advance();
    CHECK(weights(animator) == std::vector<int>{500, 500});
    animator.play("C");
    animator.advance();
    CHECK(weights(animator) == std::vector<int>{375, 375, 250});
    animator.advance();
    CHECK(weights(animator) == std::vector<int>{250, 250, 500});
    for (int tick = 0; tick < 3; ++tick) {
        animator.advance();
    }
    CHECK(weights(animator) == std::vector<int>{1000});
    // Drawn: the arm of C, not a sum.
    CHECK(drawn_x(body, animator, kArm) == doctest::Approx(3.0f).epsilon(1e-3));

    // Four motions at most: a fifth pushes the faintest out.
    for (const char* name : {"A", "B", "Idle", "A"}) {
        animator.play(name, {.restart = true});
        animator.advance();
    }
    CHECK(animator.layer(0).motions.size() == moteur::Animator::kMaxMotions);
}

TEST_CASE("Animator: asking again for what plays does not restart it") {
    const Body body;
    moteur::Animator animator = body.animator(false);
    CHECK(animator.play("A"));
    for (int tick = 0; tick < 10; ++tick) {
        animator.advance();
    }
    CHECK_FALSE(animator.play("A"));
    CHECK(animator.dominant()->clock.time() == 10000);
    CHECK(animator.play("A", {.restart = true}));
    CHECK(animator.dominant()->clock.time() == 0);
    // Without a set, no crossfade by default.
    CHECK(animator.play("B"));
    CHECK(weights(animator) == std::vector<int>{1000});
    // A clip played once and finished can be asked for again.
    animator.play("Attack", {.loop = false});
    for (int tick = 0; tick < 30; ++tick) {
        animator.advance();
    }
    CHECK(animator.finished());
    CHECK(animator.play("Attack", {.loop = false}));
}

TEST_CASE("Animator: a blend space picks two neighbours and plays them in step") {
    const Body body;
    moteur::Animator animator = body.animator();
    animator.play("move", {.fade_ticks = 0});
    moteur::PoseRequest request;

    // Half way from Walk (1 m/s) to Run (3 m/s): half each, at the same phase (Run shifted by its 0.25).
    animator.set_move_speed(2.0);
    animator.advance();
    animator.pose(1.0f, request);
    REQUIRE(request.inputs.size() == 2);
    CHECK(request.inputs[0].clip->name() == "Walk");
    CHECK(request.inputs[1].clip->name() == "Run");
    CHECK(request.inputs[0].weight == doctest::Approx(0.5f));
    CHECK(request.inputs[1].weight == doctest::Approx(0.5f));
    CHECK(request.inputs[1].ratio == doctest::Approx(request.inputs[0].ratio + 0.25f));
    // The cycle lasts as long as the two durations weighted: (40 + 30) / 2 = 35 ticks.
    for (int tick = 1; tick < 35; ++tick) {
        animator.advance();
    }
    const float phase = animator.dominant()->ratio(1.0f);
    CHECK(std::min(phase, 1.0f - phase) < 1e-4f);

    // A quarter of the way from Idle to Walk.
    animator.set_move_speed(0.25);
    animator.advance();
    animator.pose(1.0f, request);
    REQUIRE(request.inputs.size() == 2);
    CHECK(request.inputs[0].clip->name() == "Idle");
    CHECK(request.inputs[0].weight == doctest::Approx(0.75f));
    CHECK(request.inputs[1].weight == doctest::Approx(0.25f));
    // Between the two ticks, the speed (and so the weights) moves smoothly: at alpha 0.5, 1.125 m/s.
    animator.set_move_speed(2.0);
    animator.advance();
    animator.pose(0.5f, request);
    CHECK(request.inputs[0].clip->name() == "Walk");
    CHECK(request.inputs[0].weight == doctest::Approx(1.0f - 0.0625f));

    // Faster than Run: Run alone, played faster (x1.5 at 4.5 m/s).
    animator.set_move_speed(4.5);
    animator.advance();
    animator.pose(1.0f, request);
    REQUIRE(request.inputs.size() == 1);
    CHECK(request.inputs[0].clip->name() == "Run");
    CHECK(animator.dominant()->rate == 1500);
    const std::int64_t before = animator.dominant()->clock.time();
    animator.advance();
    CHECK(animator.dominant()->clock.time() - before == 1'000'000 * 3 / (2 * 30));  // 1.5 / 30 of a cycle
}

TEST_CASE("Animator: a clip with a ground speed plays at the pace of the feet") {
    const Body body;
    moteur::Animator matched = body.animator();
    moteur::Animator free = body.animator();
    matched.play("Walk", {.fade_ticks = 0, .match_speed = true});
    free.play("Walk", {.fade_ticks = 0});
    matched.set_move_speed(1.5);  // Walk moves at 1 m/s
    free.set_move_speed(1.5);
    for (int tick = 0; tick < 10; ++tick) {
        matched.advance();
        free.advance();
    }
    CHECK(matched.dominant()->clock.time() == 15000);
    CHECK(free.dominant()->clock.time() == 10000);
    matched.set_move_speed(0.0);  // standing still: the walk stops
    matched.advance();
    CHECK(matched.dominant()->clock.time() == 15000);
}

TEST_CASE("Animator: the upper layer attacks while the legs walk, then fades out by itself") {
    const Body body;
    moteur::Animator animator = body.animator();
    animator.play("B", {.fade_ticks = 0});  // arm at 2, leg at 0
    CHECK(animator.play("Attack", {.layer = 1, .loop = false}));  // 30 ticks, the set's 2-tick fade
    CHECK_FALSE(animator.play("Attack", {.layer = 1, .loop = false}));  // clicked again: goes on
    CHECK(animator.playing("Attack", 1));
    animator.advance();
    CHECK(animator.layer(1).weight == 500);
    animator.advance();
    CHECK(animator.layer(1).weight == 1000);
    // Mask from the spine, ramp 1: the arm (under the chest) attacks, the leg walks.
    CHECK(drawn_x(body, animator, kArm) == doctest::Approx(10.0f).epsilon(1e-3));
    CHECK(drawn_x(body, animator, kLeg) == doctest::Approx(0.0f).epsilon(1e-3));
    // Two ticks before its end, the layer starts fading out, and is empty once it has.
    for (int tick = 2; tick < 28; ++tick) {
        animator.advance();
    }
    CHECK(animator.layer(1).fade_to == 0);
    CHECK_FALSE(animator.playing("Attack", 1));
    for (int tick = 28; tick < 31; ++tick) {
        animator.advance();
    }
    CHECK(animator.layer(1).weight == 0);
    CHECK(animator.layer(1).motions.empty());
    CHECK(drawn_x(body, animator, kArm) == doctest::Approx(2.0f).epsilon(1e-3));
    CHECK(animator.clip_name() == "B");
}

TEST_CASE("Animator: the same weights and times in one big step or tick by tick") {
    const Body body;
    moteur::Animator stepped = body.animator();
    moteur::Animator jumped = body.animator();
    for (moteur::Animator* animator : {&stepped, &jumped}) {
        animator->play("A", {.fade_ticks = 0});
        animator->play("C", {.fade_ticks = 7});
        animator->play("Attack", {.layer = 1, .loop = false});
        animator->set_speed(1.3);
    }
    for (int tick = 0; tick < 5; ++tick) {
        stepped.advance();
    }
    jumped.advance(5);
    CHECK(weights(stepped) == weights(jumped));
    CHECK(stepped.layer(1).weight == jumped.layer(1).weight);
    CHECK(stepped.dominant()->clock.time() == jumped.dominant()->clock.time());
}

TEST_CASE("PoseSampler: blending takes the short way between opposite quaternions") {
    moteur::SkeletonData skeleton;
    skeleton.joints.push_back({"joint", -1, {}});
    const glm::quat turn = glm::angleAxis(glm::radians(60.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const auto clip = [&](const char* name, glm::quat rotation) {
        moteur::ClipData data;
        data.name = name;
        data.duration = 1.0f;
        data.tracks.resize(1);
        data.tracks[0].rotations.times = {0.0f, 1.0f};
        data.tracks[0].rotations.values = {rotation, rotation};
        return data;
    };
    const moteur::Skeleton rig = moteur::Skeleton::create(skeleton, "one");
    // The same rotation twice, written with opposite signs: blended half and half, still that rotation.
    const moteur::ClipLibrary clips = moteur::ClipLibrary::create(skeleton, {clip("q", turn), clip("minus q", -turn)}, "q");
    const moteur::PoseInput inputs[] = {{&clips.clip("q"), 0.5f, 0.5f, {}}, {&clips.clip("minus q"), 0.5f, 0.5f, {}}};
    moteur::PoseSampler sampler;
    sampler.blend(rig, inputs);
    const glm::vec3 x(sampler.model()[0] * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    const glm::vec3 expected = turn * glm::vec3(1.0f, 0.0f, 0.0f);
    CHECK(glm::length(x - expected) < 1e-3f);
}

TEST_CASE("KayKit: the ground speeds and phases of the description match the clips") {
    const std::string knight = kAssets + "models/characters/kaykit_adventurers/Knight.glb";
    if (!std::filesystem::exists(knight)) {
        return;
    }
    moteur::GltfOptions options;
    options.meshes = false;
    const moteur::ModelData data = moteur::load_gltf(knight, options);
    const moteur::Skeleton skeleton = moteur::Skeleton::create(data.skeleton, "Knight");
    const moteur::ClipLibrary clips = moteur::ClipLibrary::create(data.skeleton, data.clips, "Knight");
    const moteur::FileData file = moteur::read_file(kAssets + "animations/kaykit.json");
    const moteur::AnimationSet set =
        moteur::AnimationSet::parse({static_cast<const char*>(file.data()), file.size()}, "kaykit.json");
    const std::vector<int> feet = {data.skeleton.find("foot.l"), data.skeleton.find("foot.r")};
    CHECK_NOTHROW(moteur::Animator::mask_weights(data.skeleton, *set.mask(set.upper_mask())));

    const moteur::BlendSpaceData& locomotion = *set.blend_space("locomotion");
    float left_lands = -1.0f;  // in the blend space's cycle, for the first clip that moves
    for (const std::string& name : locomotion.clips) {
        const moteur::Stride stride = moteur::measure_stride(skeleton, clips.clip(name), feet);
        const float expected = set.clip(name).ground_speed;
        CAPTURE(name);
        CHECK(stride.ground_speed == doctest::Approx(expected).epsilon(0.03));
        if (expected <= 0.0f) {
            continue;
        }
        // Feet in step: the left foot lands at the same point of the cycle in every moving clip.
        const float phase = std::fmod(stride.touch_down[0] - set.clip(name).phase + 1.0f, 1.0f);
        if (left_lands < 0.0f) {
            left_lands = phase;
        } else {
            const float gap = std::fabs(phase - left_lands);
            CHECK(std::min(gap, 1.0f - gap) < 0.02f);
        }
    }
}

TEST_CASE("KayKit: the planted foot does not drift, from walk to run") {
    const std::string knight = kAssets + "models/characters/kaykit_adventurers/Knight.glb";
    if (!std::filesystem::exists(knight)) {
        return;
    }
    moteur::GltfOptions options;
    options.meshes = false;
    const moteur::ModelData data = moteur::load_gltf(knight, options);
    const moteur::FileData file = moteur::read_file(kAssets + "animations/kaykit.json");
    const auto skeleton = moteur::make_asset(moteur::Skeleton::create(data.skeleton, "Knight"));
    const auto clips = moteur::make_asset(moteur::ClipLibrary::create(data.skeleton, data.clips, "Knight"));
    const auto set = moteur::make_asset(
        moteur::AnimationSet::parse({static_cast<const char*>(file.data()), file.size()}, "kaykit.json"));
    const int feet[2] = {data.skeleton.find("foot.l"), data.skeleton.find("foot.r")};

    for (const float speed : {0.82f, 1.5f, 2.5f, 3.47f, 4.2f}) {
        moteur::Animator animator = moteur::Animator::create(skeleton, clips, set);
        animator.play("locomotion", {.fade_ticks = 0});
        animator.set_move_speed(speed);
        // Straight along +Z (the knight's forward) at `speed`, one pose per tick, over 4 seconds.
        std::vector<glm::vec3> track[2];
        moteur::PoseRequest request;
        moteur::PoseSampler sampler;
        for (int tick = 0; tick < 240; ++tick) {
            animator.advance();
            animator.pose(1.0f, request);
            sampler.blend(*skeleton, request.inputs);
            const glm::vec3 root(0.0f, 0.0f, speed * static_cast<float>(tick + 1) / 60.0f);
            for (int k = 0; k < 2; ++k) {
                track[k].push_back(root + glm::vec3(sampler.model()[static_cast<std::size_t>(feet[k])][3]));
            }
        }
        // On the floor (within a tenth of its rise, as measure_stride): how fast it moves, on average
        // (`slide`), and how far it drifts along the way, forwards or backwards (`drift`). Matching the
        // pace cancels the drift; the rest of the slide is the clip's own (a foot that rolls, turns).
        double slide = 0.0;
        double drift = 0.0;
        int count = 0;
        for (const std::vector<glm::vec3>& foot : track) {
            float low = foot[0].y;
            float high = foot[0].y;
            for (const glm::vec3& p : foot) {
                low = std::min(low, p.y);
                high = std::max(high, p.y);
            }
            for (std::size_t i = 60; i + 1 < foot.size(); ++i) {  // after the first second
                if (foot[i].y <= low + 0.1f * (high - low) && foot[i + 1].y <= low + 0.1f * (high - low)) {
                    slide += glm::length(glm::vec2(foot[i + 1].x - foot[i].x, foot[i + 1].z - foot[i].z)) * 60.0f;
                    drift += (foot[i + 1].z - foot[i].z) * 60.0f;
                    ++count;
                }
            }
        }
        REQUIRE(count > 0);
        MESSAGE("at " << speed << " m/s: the planted foot slides at " << slide / count << " m/s, drifts at "
                      << drift / count << " m/s");
        CHECK(std::fabs(drift / count) < 0.1 * speed);  // 3 to 5 % for a clip alone, up to 7.5 % in a blend
        CHECK(slide / count < 0.4 * speed);
    }
}

TEST_CASE("Events: exactly once per loop, at 0 and at the end of a loop, whatever the steps") {
    const Body body;
    moteur::Animator stepped = body.animator();
    stepped.play("A", {.fade_ticks = 0});  // a_start at 0, 60 ticks
    const std::vector<Fired> a = run(stepped, 180);
    CHECK(a == std::vector<Fired>{{1, "a_start", "A"}, {60, "a_start", "A"}, {120, "a_start", "A"}, {180, "a_start", "A"}});

    // One big step, and x3 speed: the same events.
    moteur::Animator jumped = body.animator();
    jumped.play("A", {.fade_ticks = 0});
    CHECK(names(run(jumped, 180, 1, true)) == names(a));
    moteur::Animator fast = body.animator();
    fast.set_speed(3.0);
    fast.play("A", {.fade_ticks = 0});
    CHECK(names(run(fast, 60)) == names(a));

    // An event at the end of a loop is its start: once per loop, not twice.
    moteur::Animator b = body.animator();
    b.play("B", {.fade_ticks = 0});
    CHECK(run(b, 120) ==
          std::vector<Fired>{{1, "b_end", "B"}, {30, "b_mid", "B"}, {60, "b_end", "B"}, {90, "b_mid", "B"}, {120, "b_end", "B"}});

    // Moving the clock (tools) does not fire what it skips.
    b.seek(10.0);
    CHECK(run(b, 19).empty());
    CHECK(names(run(b, 1)) == std::vector<std::string>{"b_mid"});
}

TEST_CASE("Events: a clip played once fires its last event once, at its very end") {
    const Body body;
    moteur::Animator animator = body.animator();
    animator.play("Attack", {.fade_ticks = 0, .loop = false});  // 30 ticks: impact at 15, attack_end at 30
    CHECK(run(animator, 40) == std::vector<Fired>{{15, "impact", "Attack"}, {30, "attack_end", "Attack"}});
}

TEST_CASE("Events: during a crossfade, a clip fires from half its weight; 'always' events whatever it weighs") {
    const Body body;
    moteur::Animator animator = body.animator();
    animator.play("A", {.fade_ticks = 0});
    run(animator, 57);
    animator.play("B");  // 4 ticks: A weighs 750, 500, 250 at the ticks 58, 59, 60
    const std::vector<Fired> fade = run(animator, 40, 58);
    // No a_start at 60 (A weighs 250), no b_end at 58 (B's start, while it weighs 250).
    CHECK(fade == std::vector<Fired>{{87, "b_mid", "B"}});

    // Fading in over 40 ticks: Walk's step at 0 is lost (25), its step at 20 fires (500); the
    // attack's impact at 15 fires at 375, being "always".
    moteur::Animator walk = body.animator();
    walk.play("Walk", {.fade_ticks = 40});
    CHECK(run(walk, 20) == std::vector<Fired>{{20, "step_b", "Walk"}});
    moteur::Animator attack = body.animator();
    attack.play("Attack", {.fade_ticks = 40, .loop = false});
    CHECK(run(attack, 20) == std::vector<Fired>{{15, "impact", "Attack"}});
}

TEST_CASE("Events: in a blend space, only the clip that weighs most fires") {
    const Body body;
    // 2 m/s: half Walk, half Run; Walk leads (ties go to the slower clip). Its steps at 0 and 0.5
    // of the cycle.
    moteur::Animator walk = body.animator();
    walk.play("move", {.fade_ticks = 0});
    walk.set_move_speed(2.0);
    const std::vector<Fired> walking = run(walk, 70);
    CHECK(names(walking) == std::vector<std::string>{"step_a", "step_b", "step_a", "step_b"});
    for (const Fired& f : walking) {
        CHECK(f.clip == "Walk");
    }
    // 2.5 m/s: Run leads; its steps are at 0.75 and 0.25 of the cycle (its phase is 0.25).
    moteur::Animator run_ = body.animator();
    run_.play("move", {.fade_ticks = 0});
    run_.set_move_speed(2.5);
    const std::vector<Fired> running = run(run_, 70);
    CHECK(names(running) == std::vector<std::string>{"step_b", "step_a", "step_b", "step_a"});
    for (const Fired& f : running) {
        CHECK(f.clip == "Run");
    }
    CHECK(running[0].tick == 9);  // a quarter of a 32.5-tick cycle: the 9th tick
}

TEST_CASE("Events: the upper layer's, with its weight; an event past its clip's end is refused") {
    const Body body;
    entt::registry registry;
    const entt::entity walker = registry.create();
    const entt::entity idle = registry.create();
    moteur::Animator& a = registry.emplace<moteur::Animator>(walker, body.animator());
    a.play("B", {.fade_ticks = 0});
    a.play("Attack", {.layer = 1, .loop = false});
    registry.emplace<moteur::Animator>(idle, body.animator()).play("Idle", {.fade_ticks = 0});
    std::vector<moteur::AnimatorEvent> events;
    for (int tick = 0; tick < 15; ++tick) {
        moteur::advance_animators(registry, 1, &events);
    }
    REQUIRE(events.size() == 2);
    CHECK(events[0].entity == walker);
    CHECK(events[0].name == "b_end");
    CHECK(events[1].name == "impact");
    CHECK(events[1].layer == 1);
    CHECK(events[1].motion == "Attack");
    CHECK(events[1].weight == 1000);

    Body broken;
    broken.set = moteur::make_asset(moteur::AnimationSet::parse(
        R"({"version": 1, "clips": {"Run": {"events": [{"time": 0.6, "name": "late"}]}}})", "late.json"));
    moteur::Animator animator = broken.animator();
    CHECK_THROWS_WITH_AS(animator.play("Run"),
                         doctest::Contains("Animation set 'late.json': clip 'Run': event 'late' at 0.600000 s is past its end"),
                         std::runtime_error);
}

TEST_CASE("KayKit: every event of the description is within its clip") {
    const std::string knight = kAssets + "models/characters/kaykit_adventurers/Knight.glb";
    if (!std::filesystem::exists(knight)) {
        return;
    }
    moteur::GltfOptions options;
    options.meshes = false;
    const moteur::ModelData data = moteur::load_gltf(knight, options);
    const auto skeleton = moteur::make_asset(moteur::Skeleton::create(data.skeleton, "Knight"));
    const auto clips = moteur::make_asset(moteur::ClipLibrary::create(data.skeleton, data.clips, "Knight"));
    const moteur::FileData file = moteur::read_file(kAssets + "animations/kaykit.json");
    const auto set = moteur::make_asset(
        moteur::AnimationSet::parse({static_cast<const char*>(file.data()), file.size()}, "kaykit.json"));
    moteur::Animator animator = moteur::Animator::create(skeleton, clips, set);
    for (const std::string& name : clips->names()) {
        CAPTURE(name);
        CHECK_NOTHROW(animator.play(name, {.restart = true}));
    }
    // The steps of the walk land on the feet: a step when a foot touches down (measure_stride).
    const std::vector<int> feet = {data.skeleton.find("foot.l"), data.skeleton.find("foot.r")};
    for (const char* name : {"Walking_A", "Running_A"}) {
        const moteur::SkeletalClip& clip = clips->clip(name);
        const moteur::Stride stride = moteur::measure_stride(*skeleton, clip, feet);
        for (const moteur::ClipEvent& event : set->clip(name).events) {
            const int foot = event.name == "step_left" ? 0 : 1;
            const float ratio = event.time / clip.duration();
            const float gap = std::fabs(ratio - stride.touch_down[static_cast<std::size_t>(foot)]);
            CAPTURE(event.name);
            CHECK(std::min(gap, 1.0f - gap) < 0.02f);
        }
    }
}
