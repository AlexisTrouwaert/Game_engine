// The animation graph in data (milestone 7, part 9): parameters, states, transitions by condition,
// end of clip, priority, triggers taken once, "from anywhere", interruption, saves, reloads.

#include <doctest/doctest.h>

#include <entt/entity/registry.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/animation_data.hpp"
#include "moteur/animation_graph.hpp"
#include "moteur/animation_set.hpp"
#include "moteur/animator.hpp"
#include "moteur/skeleton.hpp"

namespace {

moteur::SkeletonData body() {
    moteur::SkeletonData skeleton;
    skeleton.joints.push_back({"root", -1, {}});
    skeleton.joints.push_back({"spine", 0, {}});
    return skeleton;
}

moteur::ClipData held(const char* name, float seconds) {
    moteur::ClipData clip;
    clip.name = name;
    clip.duration = seconds;
    clip.tracks.resize(2);
    for (moteur::JointTrack& track : clip.tracks) {
        track.translations.times = {0.0f, seconds};
        track.translations.values = {glm::vec3(0.0f), glm::vec3(0.0f)};
    }
    return clip;
}

std::string set_json(const std::string& graph) {
    return R"({ "version": 1, "fade_ticks": 4,
                "clips": { "Walk": { "ground_speed": 1.0 }, "Run": { "ground_speed": 3.0 } },
                "blend_spaces": { "move": ["Idle", "Walk", "Run"] },
                "graph": )" + graph + "}";
}

// Idle, Walk, Run loop; Attack (30 ticks), Hit (12 ticks), Death (60 ticks) play once.
const char* kGraph = R"({
  "parameters": { "vitesse": "float", "combat": "bool", "arme": "int",
                  "attaque": "trigger", "touche": "trigger", "mort": "trigger", "retour": "trigger" },
  "layers": [
    { "name": "corps", "start": "repos",
      "states": { "repos": { "motion": "Idle" }, "course": { "motion": "Run" },
                  "garde": { "motion": "Walk" }, "mort": { "motion": "Death", "loop": false } },
      "transitions": [
        { "from": "*", "to": "mort", "when": { "param": "mort" }, "priority": 10, "interrupt": true },
        { "from": "repos", "to": "course", "when": { "param": "vitesse", "op": ">", "value": 0.5 } },
        { "from": "course", "to": "repos", "when": { "param": "vitesse", "op": "<=", "value": 0.5 } },
        { "from": "repos", "to": "garde", "when": { "all": [ { "param": "combat" },
                                                             { "param": "arme", "op": ">=", "value": 2 } ] },
          "priority": 1 },
        { "from": "garde", "to": "repos", "when": { "param": "retour" } }
      ] },
    { "name": "haut", "start": "libre",
      "states": { "libre": {}, "attaque": { "motion": "Attack", "loop": false, "interruptible": false },
                  "touche": { "motion": "Hit", "loop": false }, "enchaine": { "motion": "Attack", "loop": false } },
      "transitions": [
        { "from": "*", "to": "libre", "when": { "param": "mort" }, "fade_ticks": 4, "priority": 10, "interrupt": true, "self": true },
        { "from": "*", "to": "touche", "when": { "param": "touche" }, "priority": 5, "self": true },
        { "from": "libre", "to": "attaque", "when": { "param": "attaque" } },
        { "from": "attaque", "to": "enchaine", "when": { "param": "attaque" } },
        { "from": "attaque", "to": "libre", "when": { "end": true } },
        { "from": "touche", "to": "libre", "when": { "end": true } },
        { "from": "enchaine", "to": "libre", "when": { "end": true } }
      ] }
  ]
})";

struct Rig {
    moteur::SkeletonData data = body();
    moteur::Asset<moteur::Skeleton> skeleton = moteur::make_asset(moteur::Skeleton::create(data, "body"));
    moteur::Asset<moteur::ClipLibrary> clips = moteur::make_asset(moteur::ClipLibrary::create(
        data,
        {held("Idle", 1.0f), held("Walk", 1.0f), held("Run", 0.5f), held("Attack", 0.5f), held("Hit", 0.2f),
         held("Death", 1.0f)},
        "body.glb"));
    moteur::Asset<moteur::AnimationSet> set = moteur::make_asset(moteur::AnimationSet::parse(set_json(kGraph), "body.json"));

    moteur::Animator animator() const {
        moteur::Animator a = moteur::Animator::create(skeleton, clips, set);
        a.start_graph();
        return a;
    }
};

void run(moteur::Animator& animator, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        animator.advance();
    }
}

}  // namespace

TEST_CASE("Animation graph: the file is checked, each error names its place") {
    const auto bad = [](const std::string& graph, const char* why) {
        CHECK_THROWS_WITH_AS(moteur::AnimationSet::parse(set_json(graph), "x.json"), doctest::Contains(why), std::runtime_error);
    };
    const std::string layer = R"("states": { "a": { "motion": "Idle" } }, "start": "a")";
    bad(R"({ "layers": [] })", "one or two layers");
    bad(R"({ "layers": [ { "states": { "a": {} }, "start": "b" } ] })", "layers[0] > start: 'b' is not a state");
    bad(R"({ "parameters": { "p": "float" }, "layers": [ { )" + layer +
            R"(, "transitions": [ { "from": "a", "to": "a", "when": { "param": "q", "op": ">", "value": 1 } } ] } ] })",
        "layers[0] > transitions[0] > when > param: 'q' is not a parameter");
    bad(R"({ "parameters": { "t": "trigger" }, "layers": [ { )" + layer +
            R"(, "transitions": [ { "from": "a", "to": "a", "when": { "param": "t", "op": "==", "value": 1 } } ] } ] })",
        "is a trigger");
    bad(R"({ "parameters": { "v": "float" }, "layers": [ { )" + layer +
            R"(, "transitions": [ { "from": "a", "to": "a", "when": { "param": "v" } } ] } ] })",
        "is a number: give op and value");
    bad(R"({ "parameters": { "v": "float" }, "layers": [ { )" + layer +
            R"(, "transitions": [ { "from": "a", "to": "a", "when": { "param": "v", "op": "=>", "value": 1 } } ] } ] })",
        "'=>' is not one of");
    bad(R"({ "parameters": { "v": "chaine" }, "layers": [ { )" + layer + "} ] }", "not float, int, bool or trigger");
    bad(R"({ "layers": [ { )" + layer + R"(, "transitions": [ { "from": "z", "to": "a" } ] } ] })", "from: 'z' is not a state");

    // A state whose motion the clips do not have: refused when the graph starts.
    const Rig rig;
    moteur::Asset<moteur::AnimationSet> set = moteur::make_asset(moteur::AnimationSet::parse(
        set_json(R"({ "layers": [ { "states": { "a": { "motion": "Fly" } }, "start": "a" } ] })"), "fly.json"));
    moteur::Animator animator = moteur::Animator::create(rig.skeleton, rig.clips, set);
    CHECK_THROWS_WITH_AS(animator.start_graph(), doctest::Contains("no clip or blend space 'Fly'"), std::runtime_error);
    moteur::Animator plain = moteur::Animator::create(rig.skeleton, rig.clips);
    CHECK_THROWS_AS(plain.start_graph(), std::runtime_error);
}

TEST_CASE("Animation graph: start states, transitions by condition, priority") {
    const Rig rig;
    moteur::Animator a = rig.animator();
    CHECK(a.graph_active());
    CHECK(a.graph_state(0) == "repos");
    CHECK(a.graph_state(1) == "libre");
    CHECK(a.playing("Idle"));
    CHECK(a.graph_history().empty());

    CHECK(a.set_parameter("vitesse", 2.0));
    CHECK(a.graph_state(0) == "course");  // at once: the value changed
    CHECK(a.playing("Run"));
    CHECK(a.set_parameter("vitesse", 0.5));
    CHECK(a.graph_state(0) == "repos");  // <= 0.5
    CHECK_FALSE(a.set_parameter("inconnu", 1.0));

    // All of: combat and arme >= 2 (an int, rounded).
    a.set_parameter("combat", true);
    CHECK(a.graph_state(0) == "repos");
    a.set_parameter("arme", 1.6);  // rounded to 2
    CHECK(a.parameter("arme") == 2.0);
    CHECK(a.graph_state(0) == "garde");

    // Priority: back in "repos" with both "course" (first in the file) and "garde" (priority 1)
    // holding, "garde" wins; the chain garde -> repos -> garde happens in one evaluation.
    moteur::Animator b = rig.animator();
    b.set_parameter("combat", true);
    b.set_parameter("arme", 3);
    CHECK(b.graph_state(0) == "garde");
    b.set_parameter("vitesse", 2.0);  // nothing from "garde"
    CHECK(b.graph_state(0) == "garde");
    CHECK(b.fire("retour"));
    CHECK(b.graph_state(0) == "garde");
    REQUIRE(b.graph_history().size() >= 2);
    CHECK(b.graph_history()[b.graph_history().size() - 2].to == "repos");
    // Without it, file order: "course".
    moteur::Animator c = rig.animator();
    c.set_parameter("vitesse", 2.0);
    CHECK(c.graph_state(0) == "course");
}

TEST_CASE("Animation graph: triggers are taken once, end of clip, interruption, from anywhere") {
    const Rig rig;
    moteur::Animator a = rig.animator();
    // Taken: libre -> attaque; the same trigger does not also take attaque -> enchaine.
    CHECK(a.fire("attaque"));
    CHECK(a.graph_state(1) == "attaque");
    CHECK(a.playing("Attack", 1));
    // Not interruptible: a second trigger does nothing until the clip ends.
    CHECK_FALSE(a.fire("attaque"));
    CHECK(a.graph_state(1) == "attaque");
    // A trigger no transition takes is dropped: it does not fire later by itself.
    run(a, 40);  // past the end of Attack (30 ticks): attaque -> libre by "end"
    CHECK(a.graph_state(1) == "libre");
    CHECK_FALSE(a.fire("inconnu"));

    // "From anywhere", again from the start with "self": the hit restarts.
    CHECK(a.fire("touche"));
    CHECK(a.graph_state(1) == "touche");
    run(a, 5);
    CHECK(a.fire("touche"));
    CHECK(a.layer(1).motions.back().clock.time() == 0);  // from its start
    // An interrupting transition cuts a state that is not interruptible.
    a.fire("attaque");
    run(a, 15);  // touche ends (12 ticks): libre
    CHECK(a.graph_state(1) == "libre");
    CHECK(a.fire("attaque"));
    CHECK(a.fire("mort"));  // one trigger, both layers: the body falls, the upper body lets go
    CHECK(a.graph_state(0) == "mort");
    CHECK(a.graph_state(1) == "libre");
    CHECK(a.layer(1).fade_to == 0);
    run(a, 80);
    CHECK(a.graph_state(0) == "mort");  // no way out: stays on the ground
    CHECK(a.finished());

    const std::vector<moteur::Animator::GraphChange>& history = a.graph_history();
    REQUIRE_FALSE(history.empty());
    CHECK(history.back().from == "attaque");
    CHECK(history.back().to == "libre");
}

TEST_CASE("Animation graph: the same parameters give the same states, saved and given back exactly") {
    const Rig rig;
    const auto script = [](moteur::Animator& a, int tick) {
        if (tick % 37 == 5) a.fire("attaque");
        if (tick % 53 == 11) a.fire("touche");
        a.set_parameter("vitesse", (tick / 20) % 3 == 1 ? 2.0 : 0.0);
        a.set_parameter("combat", (tick / 90) % 2 == 1);
        if (tick == 400) a.fire("mort");
    };
    moteur::Animator first = rig.animator();
    moteur::Animator second = rig.animator();
    moteur::Animator saved_then_loaded = rig.animator();
    for (int tick = 0; tick < 450; ++tick) {
        script(first, tick);
        script(second, tick);
        first.advance();
        second.advance();
        if (tick == 200) {
            saved_then_loaded.load_state(nlohmann::json::parse(first.save_state().dump()));
        } else if (tick > 200) {
            script(saved_then_loaded, tick);
            saved_then_loaded.advance();
        }
    }
    CHECK(first.save_state() == second.save_state());
    CHECK(first.save_state() == saved_then_loaded.save_state());
    REQUIRE(first.graph_history().size() == second.graph_history().size());
    for (std::size_t i = 0; i < first.graph_history().size(); ++i) {
        CHECK(first.graph_history()[i].tick == second.graph_history()[i].tick);
        CHECK(first.graph_history()[i].to == second.graph_history()[i].to);
    }
    CHECK(first.graph_state(0) == "mort");
}

TEST_CASE("Animation graph: a file read again keeps the states by name; a broken one waits") {
    Rig rig;
    moteur::Animator a = rig.animator();
    a.set_parameter("vitesse", 2.0);
    CHECK(a.graph_state(0) == "course");
    // The threshold moved in the file: the running character uses it at once.
    std::string changed = kGraph;
    for (const std::string op : {">", "<="}) {  // both thresholds of the walk
        const std::string from = R"("op": ")" + op + R"(", "value": 0.5)";
        changed.replace(changed.find(from), from.size(), R"("op": ")" + op + R"(", "value": 2.5)");
    }
    *rig.set = moteur::AnimationSet::parse(set_json(changed), "body.json");
    a.advance();
    CHECK(a.graph_state(0) == "repos");  // vitesse 2 <= 2.5 now
    CHECK(a.parameter("vitesse") == 2.0);  // kept by name
    // A state naming a clip that does not exist: the graph waits, the character goes on.
    std::string broken = changed;
    broken.replace(broken.find(R"("motion": "Run")"), 15, R"("motion": "Fly")");
    *rig.set = moteur::AnimationSet::parse(set_json(broken), "body.json");
    a.advance();
    CHECK_FALSE(a.graph_error().empty());
    CHECK_FALSE(a.set_parameter("vitesse", 0.0));
    *rig.set = moteur::AnimationSet::parse(set_json(kGraph), "body.json");
    a.advance();
    CHECK(a.graph_error().empty());
    CHECK(a.set_parameter("vitesse", 3.0));
    CHECK(a.graph_state(0) == "course");
}
