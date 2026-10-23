// Gameplay animation (milestone 9 bis, part 3): root motion in whole millimetres read at the tick,
// the pose kept in place; additive layers; the layers and motions of the set; event windows.

#include <doctest/doctest.h>

#include <entt/entity/registry.hpp>
#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

#include <string>
#include <vector>

#include "moteur/animation_data.hpp"
#include "moteur/animation_set.hpp"
#include "moteur/animator.hpp"
#include "moteur/fixed_timestep.hpp"
#include "moteur/skeleton.hpp"

namespace {

// root > hips > arm: joints at rest where their parent is.
moteur::SkeletonData rig() {
    moteur::SkeletonData skeleton;
    skeleton.joints.push_back({"root", -1, {}});
    skeleton.joints.push_back({"hips", 0, {}});
    skeleton.joints.push_back({"arm", 1, {}});
    return skeleton;
}

enum Joint { kRoot, kHips, kArm };

// A clip of `seconds`: each joint's translation goes linearly from `from` to `to`.
moteur::ClipData moving(const char* name, float seconds, std::vector<glm::vec3> from, std::vector<glm::vec3> to) {
    moteur::ClipData clip;
    clip.name = name;
    clip.duration = seconds;
    clip.tracks.resize(3);
    for (std::size_t j = 0; j < 3; ++j) {
        clip.tracks[j].translations.times = {0.0f, seconds};
        clip.tracks[j].translations.values = {from[j], to[j]};
    }
    return clip;
}

const char* kSet = R"({
  "version": 1,
  "fade_ticks": 0,
  "clips": {
    "Dash": { "root_motion": 2.0, "events": [ { "time": 0.1, "end": 0.3, "name": "dodge" }, { "time": 0.2, "name": "halfway" } ] },
    "Walk": { "root_motion": true }
  },
  "masks": { "arms": { "joint": "arm" } },
  "layers": [ { "name": "body" }, { "name": "arms", "mask": "arms" }, { "name": "shake", "additive": true } ],
  "max_motions": 2,
  "root_joint": "root"
})";

struct Rig {
    moteur::SkeletonData data = rig();
    moteur::Asset<moteur::Skeleton> skeleton = moteur::make_asset(moteur::Skeleton::create(data, "rig"));
    moteur::Asset<moteur::ClipLibrary> clips = moteur::make_asset(moteur::ClipLibrary::create(
        data,
        {// Dash: the root goes 0.6 m forward (z) and 0.1 m to the side in 0.5 s, and up 0.2 m (kept in the pose).
         moving("Dash", 0.5f, {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}}, {{0.1f, 0.2f, 0.6f}, {0, 1, 0}, {0, 0, 0}}),
         // Walk: a loop of 1 s, the root 1 m forward.
         moving("Walk", 1.0f, {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}}, {{0, 0, 1.0f}, {0, 1, 0}, {0, 0, 0}}),
         // Idle: still, the arm at x = 1.
         moving("Idle", 1.0f, {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}}, {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}}),
         // Shake: the arm goes from x = 0 to x = 0.5 (its difference from its first pose: +0.5 at the end).
         moving("Shake", 0.5f, {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}}, {{0, 0, 0}, {0, 1, 0}, {0.5f, 0, 0}}),
         // Reach: the arm held at x = 3.
         moving("Reach", 0.5f, {{0, 0, 0}, {0, 1, 0}, {3, 0, 0}}, {{0, 0, 0}, {0, 1, 0}, {3, 0, 0}})},
        "rig.glb"));
    moteur::Asset<moteur::AnimationSet> set = moteur::make_asset(moteur::AnimationSet::parse(kSet, "rig.json"));

    moteur::Animator animator() const { return moteur::Animator::create(skeleton, clips, set); }

    // The joint's model-space position in the pose drawn at the current tick.
    glm::vec3 drawn(const moteur::Animator& animator, Joint joint) const {
        moteur::PoseRequest request;
        animator.pose(1.0f, request);
        moteur::PoseSampler sampler;
        sampler.blend(*skeleton, request.inputs);
        return glm::vec3(sampler.model()[joint][3]);
    }
};

}  // namespace

TEST_CASE("root motion: the clip's root curve in millimetres, taken at the tick; the pose stays in place") {
    const Rig r;
    const std::vector<glm::ivec3>& track = r.clips->root_track(*r.skeleton, r.clips->clip("Dash"), kRoot);
    const int ticks = r.clips->clip("Dash").duration_ticks();
    REQUIRE(track.size() == static_cast<std::size_t>(ticks) + 1);
    CHECK(track.front() == glm::ivec3(0));
    CHECK(track.back() == glm::ivec3(100, 0, 600));  // the height is not root motion
    CHECK(&r.clips->root_track(*r.skeleton, r.clips->clip("Dash"), kRoot) == &track);  // kept

    moteur::Animator a = r.animator();
    a.play("Dash", false);
    glm::ivec3 total(0);
    for (int t = 0; t < ticks + 5; ++t) {
        a.advance();
        total += a.take_root_motion();
        CHECK(a.root_motion() == glm::ivec3(0));  // taken
        // The root drawn stays where the clip starts on the plane (its height moves).
        const glm::vec3 root = r.drawn(a, kRoot);
        CHECK(root.x == doctest::Approx(0.0f));
        CHECK(root.z == doctest::Approx(0.0f));
    }
    CHECK(total == glm::ivec3(200, 0, 1200));  // the curve times the set's 2.0, the clip played once
    CHECK(r.drawn(a, kRoot).y == doctest::Approx(0.2f).epsilon(0.01));

    // A loop goes on cycle after cycle; two runs are the same to the millimetre.
    moteur::Animator b = r.animator();
    moteur::Animator c = r.animator();
    b.play("Walk", true);
    c.play("Walk", true);
    glm::ivec3 walked(0);
    for (int t = 0; t < 150; ++t) {  // 2.5 s
        b.advance();
        c.advance();
        const glm::ivec3 step = b.take_root_motion();
        CHECK(step == c.take_root_motion());
        walked += step;
    }
    CHECK(walked.z == doctest::Approx(2500).epsilon(0.002));
}

TEST_CASE("root motion: saved and loaded with the animator, crossfaded by weight") {
    const Rig r;
    moteur::Animator a = r.animator();
    a.play("Walk", true);
    for (int t = 0; t < 10; ++t) {
        a.advance();
    }
    const glm::ivec3 pending = a.root_motion();  // not taken yet
    CHECK(pending.z > 0);
    moteur::Animator loaded = r.animator();
    loaded.load_state(a.save_state());
    CHECK(loaded.root_motion() == pending);
    a.advance();
    loaded.advance();
    CHECK(a.take_root_motion() == loaded.take_root_motion());

    // Half the weight: half the move (a crossfade from a clip without root motion).
    moteur::Animator half = r.animator();
    half.play("Idle", true);
    half.advance();
    half.play("Walk", moteur::PlayOptions{0, 60, true, true, false});
    moteur::Animator full = r.animator();
    full.play("Walk", true);
    for (int t = 0; t < 30; ++t) {
        half.advance();
        full.advance();
    }
    const int h = half.take_root_motion().z;
    const int f = full.take_root_motion().z;
    CHECK(h > 0);
    CHECK(h < f / 2);  // the weight grew from 0 to a half over these ticks
}

TEST_CASE("layers from the set: masked layers take over, an additive layer adds its difference") {
    const Rig r;
    moteur::Animator a = r.animator();
    REQUIRE(a.layer_count() == 3);
    CHECK_FALSE(a.layer(1).additive);
    CHECK(a.layer(2).additive);
    a.play("Idle", true);
    a.advance();
    CHECK(r.drawn(a, kArm).x == doctest::Approx(1.0f).epsilon(0.001));  // Idle's arm (keys quantized by ozz)

    // The arms layer replaces the arm.
    a.play("Reach", moteur::PlayOptions{1, 0, true, true, false});
    a.advance();
    CHECK(r.drawn(a, kArm).x == doctest::Approx(3.0f).epsilon(0.001));

    // The additive layer adds Shake's difference (+0.25 halfway) over it, without replacing it.
    a.play("Shake", moteur::PlayOptions{2, 0, false, true, false});
    for (int t = 0; t < 15; ++t) {
        a.advance();
    }
    CHECK(r.drawn(a, kArm).x == doctest::Approx(3.25f).epsilon(0.02));
    // The layer below goes on: Reach is still what layer 1 plays.
    CHECK(a.playing("Reach", 1));

    // Beyond the set's max_motions (2), the faintest motion goes.
    moteur::Animator b = r.animator();
    b.play("Idle", moteur::PlayOptions{0, 10, true, true, false});
    b.advance();
    b.play("Walk", moteur::PlayOptions{0, 10, true, true, false});
    b.advance();
    b.play("Dash", moteur::PlayOptions{0, 10, false, true, false});
    CHECK(b.layer(0).motions.size() == 2);

    // A state saved with fewer layers than the set has now loads, the others idle.
    nlohmann::json state = a.save_state();
    state["layers"].erase(2);
    moteur::Animator c = r.animator();
    c.load_state(state);
    CHECK(c.layer(2).motions.empty());
}

TEST_CASE("event windows: a start and an end, and whether a clip is inside") {
    const Rig r;
    moteur::Animator a = r.animator();
    a.play("Dash", false);
    std::vector<std::pair<int, moteur::AnimatorEvent>> fired;
    std::vector<int> inside;
    for (int t = 1; t <= 30; ++t) {
        std::vector<moteur::AnimatorEvent> events;
        a.advance(1, &events);
        for (const moteur::AnimatorEvent& e : events) {
            fired.emplace_back(t, e);
        }
        if (a.in_window("dodge")) {
            inside.push_back(t);
        }
    }
    const int begin = static_cast<int>(moteur::ticks_from_seconds(0.1));
    const int end = static_cast<int>(moteur::ticks_from_seconds(0.3));
    REQUIRE(fired.size() == 3);
    CHECK(fired[0].second.name == "dodge");
    CHECK(fired[0].second.phase == moteur::AnimatorEvent::Phase::Begin);
    CHECK(fired[1].second.name == "halfway");
    CHECK(fired[1].second.phase == moteur::AnimatorEvent::Phase::Instant);
    CHECK(fired[2].second.name == "dodge");
    CHECK(fired[2].second.phase == moteur::AnimatorEvent::Phase::End);
    CHECK(fired[0].first == begin);
    CHECK(fired[2].first == end);
    REQUIRE_FALSE(inside.empty());
    CHECK(inside.front() == begin);
    CHECK(inside.back() == end - 1);
    CHECK(inside.size() == static_cast<std::size_t>(end - begin));
    CHECK_FALSE(a.in_window("halfway"));  // an instant is no window
}

TEST_CASE("animation set: layers, motions, root motion and windows are checked") {
    const auto error = [](const char* text) {
        try {
            moteur::AnimationSet::parse(text, "set.json");
        } catch (const std::runtime_error& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    CHECK(error(R"({"version": 1, "layers": [{"name": "a", "additive": true}]})").find("first one") != std::string::npos);
    CHECK(error(R"({"version": 1, "layers": [{"name": "a"}, {"name": "b", "mask": "none"}]})").find("not in masks") != std::string::npos);
    CHECK(error(R"({"version": 1, "max_motions": 0})").find("max_motions") != std::string::npos);
    CHECK(error(R"({"version": 1, "clips": {"A": {"root_motion": -1}}})").find("root_motion") != std::string::npos);
    CHECK(error(R"({"version": 1, "clips": {"A": {"events": [{"time": 0.5, "end": 0.2, "name": "w"}]}}})").find("ends before") !=
          std::string::npos);
    // Without "layers": the whole body and the upper body, as before.
    const moteur::AnimationSet plain = moteur::AnimationSet::parse(R"({"version": 1})", "plain.json");
    REQUIRE(plain.layers().size() == 2);
    CHECK(plain.max_motions() == 4);
    CHECK(plain.root_joint() == "root");
}
