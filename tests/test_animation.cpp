#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "sprite_animation.hpp"

namespace {

// Four frames: a (2 ticks), b (1), c (3), d (2) = 8 ticks.
bac_a_sable::AnimationClip make_clip(bac_a_sable::PlayMode mode, std::vector<bac_a_sable::AnimationEvent> events = {}) {
    return bac_a_sable::AnimationClip("test", {{"a", 2}, {"b", 1}, {"c", 3}, {"d", 2}}, mode, std::move(events));
}

// The frame shown after each of `ticks` single-tick advances, as letters.
std::string frames_over(bac_a_sable::AnimationPlayer& player, int ticks) {
    std::string shown;
    for (int i = 0; i < ticks; ++i) {
        player.advance(1);
        shown += player.region();
    }
    return shown;
}

std::vector<std::string> names(const std::vector<const bac_a_sable::AnimationEvent*>& fired) {
    std::vector<std::string> result;
    for (const bac_a_sable::AnimationEvent* event : fired) {
        result.push_back(event->name);
    }
    return result;
}

}  // namespace

TEST_CASE("AnimationClip rejects impossible clips") {
    using bac_a_sable::AnimationClip;
    CHECK_THROWS_AS(AnimationClip("empty", {}), std::invalid_argument);
    CHECK_THROWS_AS(AnimationClip("zero", {{"a", 0}}), std::invalid_argument);
    CHECK_THROWS_AS(AnimationClip("event", {{"a", 1}}, bac_a_sable::PlayMode::Loop, {{1, "hit"}}), std::invalid_argument);
    CHECK_THROWS_AS(AnimationClip("event", {{"a", 1}}, bac_a_sable::PlayMode::Loop, {{-1, "hit"}}), std::invalid_argument);
}

TEST_CASE("AnimationClip cycle length") {
    CHECK(make_clip(bac_a_sable::PlayMode::Once).cycle_ticks() == 8);
    CHECK(make_clip(bac_a_sable::PlayMode::Loop).cycle_ticks() == 8);
    // There and back without repeating the end frames: a b c d c b = 2+1+3+2+3+1.
    const bac_a_sable::AnimationClip ping_pong = make_clip(bac_a_sable::PlayMode::PingPong);
    CHECK(ping_pong.cycle_ticks() == 12);
    CHECK(ping_pong.step_count() == 6);
    CHECK(bac_a_sable::AnimationClip("one", {{"a", 5}}, bac_a_sable::PlayMode::PingPong).cycle_ticks() == 5);
}

TEST_CASE("AnimationPlayer shows the expected frame at each tick") {
    const bac_a_sable::AnimationClip loop = make_clip(bac_a_sable::PlayMode::Loop);
    bac_a_sable::AnimationPlayer player(loop);
    CHECK(player.region() == "a");  // before any advance
    // Ticks 1..16. Frame a covers [0, 2), b [2, 3), c [3, 6), d [6, 8).
    CHECK(frames_over(player, 16) == "abcccddaabcccdda");
}

TEST_CASE("AnimationPlayer plays a Once clip to its last frame and stays there") {
    const bac_a_sable::AnimationClip once = make_clip(bac_a_sable::PlayMode::Once);
    bac_a_sable::AnimationPlayer player(once);
    CHECK(frames_over(player, 7) == "abcccdd");
    CHECK_FALSE(player.finished());
    CHECK(frames_over(player, 1) == "d");
    CHECK(player.finished());
    CHECK(frames_over(player, 5) == "ddddd");
    CHECK(player.time() == 8 * bac_a_sable::AnimationPlayer::kSpeedOne);  // stays at the end
}

TEST_CASE("AnimationPlayer ping-pong does not repeat the end frames") {
    const bac_a_sable::AnimationClip clip("pp", {{"a", 1}, {"b", 1}, {"c", 1}}, bac_a_sable::PlayMode::PingPong);
    bac_a_sable::AnimationPlayer player(clip);
    CHECK(player.region() == "a");
    CHECK(frames_over(player, 8) == "bcbabcba");
}

TEST_CASE("AnimationPlayer at speed x2 plays twice as fast") {
    const bac_a_sable::AnimationClip once = make_clip(bac_a_sable::PlayMode::Once);
    bac_a_sable::AnimationPlayer player(once);
    player.set_speed(2.0);
    CHECK(frames_over(player, 3) == "bcd");
    CHECK_FALSE(player.finished());
    player.advance(1);
    CHECK(player.finished());  // 8 ticks of clip in 4 ticks
}

TEST_CASE("AnimationPlayer at half speed plays twice as slowly") {
    const bac_a_sable::AnimationClip loop = make_clip(bac_a_sable::PlayMode::Loop);
    bac_a_sable::AnimationPlayer player(loop);
    player.set_speed(0.5);
    CHECK(frames_over(player, 8) == "aaabbccc");
}

TEST_CASE("AnimationPlayer speed: zero pauses, negative is refused") {
    const bac_a_sable::AnimationClip loop = make_clip(bac_a_sable::PlayMode::Loop);
    bac_a_sable::AnimationPlayer player(loop);
    player.set_speed(0.0);
    CHECK(frames_over(player, 10) == "aaaaaaaaaa");
    CHECK_THROWS_AS(player.set_speed(-1.0), std::invalid_argument);
    CHECK(player.speed() == 0.0);
}

TEST_CASE("AnimationPlayer fires each event exactly once, in order") {
    const bac_a_sable::AnimationClip loop = make_clip(bac_a_sable::PlayMode::Loop, {{0, "start"}, {2, "hit"}, {3, "end"}});
    bac_a_sable::AnimationPlayer player(loop);
    std::vector<const bac_a_sable::AnimationEvent*> fired;

    player.advance(1, &fired);  // the first frame counts as reached
    CHECK(names(fired) == std::vector<std::string>{"start"});

    fired.clear();
    player.advance(1, &fired);  // tick 2: b
    CHECK(fired.empty());
    player.advance(1, &fired);  // tick 3: c
    CHECK(names(fired) == std::vector<std::string>{"hit"});

    fired.clear();
    for (int i = 0; i < 5; ++i) {  // ticks 4..8: d at 6, a again at 8
        player.advance(1, &fired);
    }
    CHECK(names(fired) == std::vector<std::string>{"end", "start"});
}

TEST_CASE("AnimationPlayer fires the events of skipped frames and loops") {
    const bac_a_sable::AnimationClip loop = make_clip(bac_a_sable::PlayMode::Loop, {{1, "b"}, {2, "c"}});
    std::vector<const bac_a_sable::AnimationEvent*> fired;

    // One big step of 2.5 loops (20 ticks): b and c three times (the third b at 18, c at 19).
    bac_a_sable::AnimationPlayer big(loop);
    big.advance(20, &fired);
    CHECK(names(fired) == std::vector<std::string>{"b", "c", "b", "c", "b", "c"});

    // The same 20 ticks one by one give exactly the same list.
    std::vector<const bac_a_sable::AnimationEvent*> small_fired;
    bac_a_sable::AnimationPlayer small(loop);
    for (int i = 0; i < 20; ++i) {
        small.advance(1, &small_fired);
    }
    CHECK(names(small_fired) == names(fired));
    CHECK(small.frame_index() == big.frame_index());
}

TEST_CASE("AnimationPlayer events with a fractional speed are neither lost nor doubled") {
    const bac_a_sable::AnimationClip loop = make_clip(bac_a_sable::PlayMode::Loop, {{0, "a"}, {1, "b"}, {2, "c"}, {3, "d"}});
    bac_a_sable::AnimationPlayer player(loop);
    player.set_speed(0.7);
    std::vector<const bac_a_sable::AnimationEvent*> fired;
    // 80 ticks at x0.7 = 56 clip ticks = 7 loops exactly.
    for (int i = 0; i < 80; ++i) {
        player.advance(1, &fired);
    }
    CHECK(fired.size() == 4 * 7 + 1);  // the last a, at 56, is reached on the 80th tick
    for (std::size_t i = 0; i < fired.size(); ++i) {
        CHECK(fired[i]->frame == static_cast<int>(i % 4));
    }
}

TEST_CASE("AnimationPlayer Once clip fires its events once, never after the end") {
    const bac_a_sable::AnimationClip once = make_clip(bac_a_sable::PlayMode::Once, {{0, "start"}, {3, "last"}});
    bac_a_sable::AnimationPlayer player(once);
    std::vector<const bac_a_sable::AnimationEvent*> fired;
    player.advance(100, &fired);
    CHECK(names(fired) == std::vector<std::string>{"start", "last"});
    fired.clear();
    player.advance(100, &fired);
    CHECK(fired.empty());

    player.restart();
    player.advance(1, &fired);
    CHECK(names(fired) == std::vector<std::string>{"start"});
}

TEST_CASE("AnimationPlayer set_time moves without firing the current frame") {
    const bac_a_sable::AnimationClip loop = make_clip(bac_a_sable::PlayMode::Loop, {{2, "hit"}});
    bac_a_sable::AnimationPlayer player(loop);
    player.set_time(4 * bac_a_sable::AnimationPlayer::kSpeedOne);  // inside c
    CHECK(player.region() == "c");
    std::vector<const bac_a_sable::AnimationEvent*> fired;
    for (int i = 0; i < 8; ++i) {  // up to tick 12: the next c starts at 11
        player.advance(1, &fired);
    }
    CHECK(names(fired) == std::vector<std::string>{"hit"});
}

TEST_CASE("AnimationLibrary reads clips from JSON") {
    const char* json = R"({
        "version": 1,
        "clips": {
            "walk": { "frame_ticks": 6, "frames": ["w0", "w1", { "region": "w2", "ticks": 12 }],
                      "events": [{ "frame": 1, "name": "step" }] },
            "die": { "mode": "once", "frames": ["d0", "d1"] },
            "idle": { "mode": "ping_pong", "frames": ["i0", "i1", "i2"] }
        }
    })";
    const bac_a_sable::AnimationLibrary library = bac_a_sable::AnimationLibrary::parse(json, "test.json");
    CHECK(library.names() == std::vector<std::string>{"die", "idle", "walk"});

    const bac_a_sable::AnimationClip& walk = library.clip("walk");
    CHECK(walk.mode() == bac_a_sable::PlayMode::Loop);
    REQUIRE(walk.frames().size() == 3);
    CHECK(walk.frames()[0].ticks == 6);
    CHECK(walk.frames()[2].region == "w2");
    CHECK(walk.frames()[2].ticks == 12);
    CHECK(walk.cycle_ticks() == 24);
    REQUIRE(walk.events().size() == 1);
    CHECK(walk.events()[0].name == "step");

    CHECK(library.clip("die").mode() == bac_a_sable::PlayMode::Once);
    CHECK(library.clip("die").frames()[0].ticks == 1);
    CHECK(library.clip("idle").mode() == bac_a_sable::PlayMode::PingPong);
    CHECK_THROWS_AS(library.clip("run"), std::runtime_error);
}

TEST_CASE("AnimationLibrary reports invalid files with their name") {
    const auto error_of = [](const char* json) {
        try {
            bac_a_sable::AnimationLibrary::parse(json, "bad.json");
        } catch (const std::runtime_error& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    CHECK(error_of("{").find("bad.json") != std::string::npos);
    CHECK(error_of(R"({"version": 2, "clips": {}})").find("version 2") != std::string::npos);
    CHECK(error_of(R"({"version": 1, "clips": {"x": {"mode": "bounce", "frames": ["a"]}}})").find("bounce") !=
          std::string::npos);
    CHECK(error_of(R"({"version": 1, "clips": {"x": {"frames": []}}})").find("no frames") != std::string::npos);
    CHECK(error_of(R"({"version": 1, "clips": {"x": {"frames": ["a"], "events": [{"frame": 3, "name": "e"}]}}})")
              .find("frame 3") != std::string::npos);
}
