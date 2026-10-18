#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "moteur/collision.hpp"
#include "moteur/movement.hpp"
#include "moteur/reflection_io.hpp"
#include "moteur/world.hpp"

// Milestone 9, part 3: the reflection. A described type is read, written, compared, linked and
// schematized from its description alone.

namespace reflection_test {

enum class Rank : std::uint8_t { Normal, Magic, Rare };
MOTEUR_REFLECT_ENUM(Rank, "normal", "magic", "rare");

struct Loot {
    int gold = 0;
};
MOTEUR_REFLECT(Loot, MOTEUR_FIELD_AS(gold, "or", {.min = 0, .max = 1000000}));

struct Attack {
    std::string name;
    float damage = 1.0f;
};
MOTEUR_REFLECT(Attack, MOTEUR_FIELD(name, {.required = true}), MOTEUR_FIELD(damage, {.min = 0.0, .max = 1000.0}));

struct Monster {
    int life = 10;
    float speed = 3.0f;
    std::string element = "physique";
    Rank rank = Rank::Normal;
    glm::vec4 tint{1.0f};
    glm::vec3 offset{0.0f};
    std::vector<Attack> attacks;
    std::vector<std::string> tags;
    moteur::DataRef<Loot> loot;
    std::uint8_t field = 0;
    double precise = 0.25;
    glm::quat turn{1.0f, 0.0f, 0.0f, 0.0f};
    int not_described = 7;  // a member left out of the description
};
MOTEUR_REFLECT(Monster,
    MOTEUR_FIELD_AS(life, "vie", {.min = 1, .max = 100000, .required = true}),
    MOTEUR_FIELD_AS(speed, "vitesse", {.min = 0.0, .max = 20.0, .unit = "m/s", .aliases = {"allure"}}),
    MOTEUR_FIELD(element),
    MOTEUR_FIELD_AS(rank, "rang"),
    MOTEUR_FIELD_AS(tint, "teinte", {.flags = moteur::kFieldDefault | moteur::kFieldColor}),
    MOTEUR_FIELD(offset),
    MOTEUR_FIELD(attacks),
    MOTEUR_FIELD(tags),
    MOTEUR_FIELD_AS(loot, "butin"),
    MOTEUR_FIELD(field),
    MOTEUR_FIELD(precise),
    MOTEUR_FIELD(turn));

struct Tag {};
MOTEUR_REFLECT(Tag);

struct TempAssets {
    std::filesystem::path root;
    TempAssets() {
        root = std::filesystem::temp_directory_path() / "moteur_reflection_test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~TempAssets() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void write(const std::string& key, const std::string& text) const {
        const std::filesystem::path path = root / key;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    std::string root_string() const { return root.string() + "/"; }
};

}  // namespace reflection_test

using namespace reflection_test;

TEST_CASE("reflection: the description of a struct becomes a TypeInfo") {
    const moteur::TypeInfo& type = moteur::type_info<Monster>();
    CHECK(type.kind == moteur::TypeKind::Struct);
    CHECK(type.name == "Monster");
    REQUIRE(type.fields.size() == 12);
    CHECK(type.fields[0].name == "vie");
    CHECK(type.fields[0].required);
    CHECK(type.fields[1].unit == "m/s");
    CHECK(type.field("allure") == &type.fields[1]);
    CHECK(type.fields[3].type->kind == moteur::TypeKind::Enum);
    CHECK(type.fields[3].type->enum_names == std::vector<std::string>{"normal", "magic", "rare"});
    CHECK(type.fields[6].type->kind == moteur::TypeKind::Array);
    CHECK(type.fields[6].type->element == &moteur::type_info<Attack>());
    CHECK(type.fields[8].type->kind == moteur::TypeKind::DataRef);
    CHECK(type.fields[9].type->bytes == 1);
    CHECK_FALSE(type.fields[9].type->is_signed);
    CHECK(moteur::find_type("Monster") == &type);
    CHECK(moteur::find_type("Attack") == &moteur::type_info<Attack>());
    CHECK(moteur::type_info<Tag>().empty_struct());
    CHECK(moteur::enum_name(Rank::Rare) == "rare");
    Rank rank = Rank::Normal;
    CHECK(moteur::enum_from_name("magic", rank));
    CHECK(rank == Rank::Magic);

    Monster m;
    m.speed = 5.5f;
    const moteur::FieldInfo& speed = type.fields[1];
    CHECK(*static_cast<float*>(speed.in(&m)) == 5.5f);
}

TEST_CASE("reflection: write then read gives the same value, bit for bit") {
    Monster m;
    m.life = 42;
    m.speed = 0.1f;  // not exact in a double: must come back as the same float
    m.element = "feu";
    m.rank = Rank::Rare;
    m.tint = {0.25f, 0.5f, 0.75f, 0.3f};
    m.offset = {1.0f / 3.0f, -2.0f, 1e-7f};
    m.attacks = {{"coup", 2.5f}, {"morsure", 7.0f}};
    m.tags = {"undead", "slow"};
    m.loot.id = "sac";
    m.field = 200;
    m.precise = 1.0 / 3.0;
    m.turn = glm::quat(0.5f, 0.5f, -0.5f, 0.5f);
    const nlohmann::json json = moteur::write_json(m, {.flags = moteur::kFieldDefault});
    CHECK(json.at("rang") == "rare");
    CHECK(json.at("butin") == "sac");
    CHECK_FALSE(json.contains("not_described"));

    // Through CBOR too, as the saves of the shipped game.
    const nlohmann::json back = nlohmann::json::from_cbor(nlohmann::json::to_cbor(json));
    Monster read;
    moteur::DataIssues issues;
    CHECK(moteur::read_json(back, read, "test", issues));
    CHECK(issues.all().empty());
    CHECK(moteur::values_equal(m, read));
    CHECK(read.speed == 0.1f);
    CHECK(read.offset.z == 1e-7f);
    CHECK(read.precise == 1.0 / 3.0);
    CHECK(read.attacks[1].name == "morsure");
    CHECK(read.not_described == 7);

    read.attacks[0].damage = 3.0f;
    CHECK_FALSE(moteur::values_equal(m, read));
}

TEST_CASE("reflection: reading reports every problem where it is, like DataReader") {
    const nlohmann::json json = nlohmann::json::parse(R"({
        "vitesse": 30,
        "rang": "epique",
        "teinte": [1, 0.5],
        "attacks": [{"damage": 2}, {"name": "x", "damage": "fort"}],
        "field": 300,
        "tags": ["a", 3],
        "typo": 1,
        "_comment": "ignored"
    })");
    Monster m;
    moteur::DataIssues issues;
    CHECK_FALSE(moteur::read_json(json, m, "data/monstres/a.json > squelette", issues));
    const std::string text = issues.text();
    CHECK(text.find("error: data/monstres/a.json > squelette > vie: required field missing") != std::string::npos);
    CHECK(text.find("> vitesse: 30.0 out of range [0.0, 20.0]") != std::string::npos);
    CHECK(text.find("> rang: 'epique' is not one of: normal, magic, rare") != std::string::npos);
    CHECK(text.find("> teinte: expected an array of 3 or 4 numbers") != std::string::npos);
    CHECK(text.find("> attacks[0] > name: required field missing") != std::string::npos);
    CHECK(text.find("> attacks[1] > damage: expected a number, found a string") != std::string::npos);
    CHECK(text.find("> field: 300 out of range [0, 255]") != std::string::npos);
    CHECK(text.find("> tags[1]: expected a string, found an integer") != std::string::npos);
    CHECK(text.find("warning: data/monstres/a.json > squelette > typo: unknown field (typo?)") != std::string::npos);
    CHECK(text.find("_comment") == std::string::npos);
    CHECK(m.life == 10);  // missing: the default stays
}

TEST_CASE("reflection: older names are read, a colour may omit its alpha, enumerations by number") {
    Monster m;
    moteur::DataIssues issues;
    CHECK(moteur::read_json(nlohmann::json::parse(R"({"vie": 5, "allure": 4.5, "teinte": [0.1, 0.2, 0.3], "rang": 2})"),
                            m, "x", issues));
    CHECK(issues.all().empty());
    CHECK(m.speed == 4.5f);
    CHECK(m.tint == glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    CHECK(m.rank == Rank::Rare);

    // Both names: the current one wins, the older one is reported.
    issues.clear();
    CHECK(moteur::read_json(nlohmann::json::parse(R"({"vie": 5, "vitesse": 2, "allure": 4.5})"), m, "x", issues));
    CHECK(m.speed == 2.0f);
    CHECK(issues.warnings() == 1);
}

TEST_CASE("reflection: a data table read and linked by the description, as by hand") {
    TempAssets assets;
    assets.write("data/butins/a.json", R"({"version": 1, "rows": {"sac": {"or": 12}}})");
    assets.write("data/monstres/a.json", R"({"version": 1, "defaults": {"vitesse": 2},
        "rows": {"squelette": {"vie": 30, "butin": "sac", "rang": "magic"},
                 "chef": {"base": "squelette", "vie": 90},
                 "perdu": {"vie": 1, "butin": "rien"}}})");
    moteur::DataTables tables;
    moteur::add_reflected_table<Loot>(tables, "butins", "data/butins");
    moteur::add_reflected_table<Monster>(tables, "monstres", "data/monstres", [](Monster& m, moteur::DataReader& row) {
        if (m.life > 50 && m.rank == Rank::Normal) {
            row.error("vie", "a normal monster has at most 50 life");
        }
    });
    const moteur::DataIssues& issues = tables.load_all(assets.root_string());
    CHECK(issues.errors() == 1);
    CHECK(issues.text().find("data/monstres/a.json > perdu > butin: 'rien' does not exist in the table butins") !=
          std::string::npos);
    CHECK(issues.text().find("data/monstres/a.json > chef > vie: a normal monster has at most 50 life") == std::string::npos);
    // "chef" inherits "magic": the game's check passes. A link error leaves only that reference
    // unresolved: the rows are there.
    const moteur::DataTable<Monster>& monsters = tables.get<Monster>();
    REQUIRE(monsters.size() == 3);
    const Monster& chef = monsters.at("chef");
    CHECK(chef.life == 90);
    CHECK(chef.speed == 2.0f);
    CHECK(chef.rank == Rank::Magic);
    REQUIRE(chef.loot.resolved());
    CHECK(chef.loot->gold == 12);
}

TEST_CASE("reflection: skip_defaults writes only what differs") {
    Monster m;
    m.life = 11;
    m.tags = {"x"};
    const nlohmann::json json = moteur::write_json(m, {.flags = moteur::kFieldDefault, .skip_defaults = true});
    CHECK(json == nlohmann::json::parse(R"({"vie": 11, "tags": ["x"]})"));
}

TEST_CASE("reflection: the engine's components are described") {
    moteur::Mover mover;
    mover.speed = 4.25f;
    mover.mode = moteur::MoveMode::FollowField;
    mover.path = {{1.0f, 0.5f, 2.0f}, {3.0f, 0.0f, 4.0f}};
    mover.state = moteur::MoveState::Blocked;
    const nlohmann::json json = moteur::write_json(mover);  // the saved fields
    CHECK(json.at("mode") == "follow_field");
    CHECK(json.at("state") == "blocked");
    CHECK(json.at("path").size() == 2);
    moteur::Mover back;
    moteur::DataIssues issues;
    CHECK(moteur::read_json(json, back, "mover", issues, {.flags = moteur::kFieldSave}));
    CHECK(moteur::values_equal(mover, back));

    // A save of milestone 7, with its short keys and its points on the plane, still reads (through
    // the "mover" component's upgrade: [x, z] becomes [x, 0, z]).
    const nlohmann::json old = nlohmann::json::parse(
        R"({"speed": 3, "turn": 10, "stop": 0.5, "field": 1, "mode": "to_point", "goal": [1, 2], "direction": [0, 0],
            "path": [], "next": 0, "status": "partial", "needs_path": true, "state": "moving", "facing": [0, 1],
            "actual": 2.5, "start": [0, 0], "slow": 3})");
    moteur::Mover legacy;
    issues.clear();
    CHECK(moteur::read_json(moteur::upgrade_mover(old), legacy, "mover", issues, {.flags = moteur::kFieldSave}));
    CHECK(issues.all().empty());
    CHECK(legacy.goal == glm::vec3(1.0f, 0.0f, 2.0f));
    CHECK(legacy.turn_rate == 10.0f);
    CHECK(legacy.path_status == moteur::PathStatus::Partial);
    CHECK(legacy.slow_ticks == 3);

    CHECK(moteur::type_info<moteur::Transform>().field("p") != nullptr);
    CHECK(moteur::type_info<moteur::Collider>().fields.size() == 6);
    CHECK((moteur::type_info<moteur::LightSource>().fields[0].flags & moteur::kFieldColor) != 0);
    CHECK(moteur::type_info<moteur::Hidden>().empty_struct());
}

TEST_CASE("reflection: a JSON schema of the data") {
    const nlohmann::json schema = moteur::json_schema(moteur::type_info<Monster>());
    CHECK(schema.at("$schema") == "https://json-schema.org/draft/2020-12/schema");
    CHECK(schema.at("type") == "object");
    CHECK(schema.at("required") == nlohmann::json::array({"vie"}));
    const nlohmann::json& speed = schema.at("properties").at("vitesse");
    CHECK(speed.at("minimum") == 0.0);
    CHECK(speed.at("maximum") == 20.0);
    CHECK(speed.at("description") == "(m/s)");
    CHECK(schema.at("properties").at("allure").at("deprecated") == true);
    CHECK(schema.at("properties").at("rang").at("$ref") == "#/$defs/Rank");
    CHECK(schema.at("$defs").at("Rank").at("enum").size() == 3);
    CHECK(schema.at("$defs").at("Attack").at("required") == nlohmann::json::array({"name"}));
    CHECK(schema.at("properties").at("field").at("minimum") == 0);
}
