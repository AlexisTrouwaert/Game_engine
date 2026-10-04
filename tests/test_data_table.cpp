#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "moteur/data_table.hpp"

// Milestone 7, part 2: data tables. The test tables are written into a temporary assets directory,
// so that a test can change a file and reload.

namespace {

struct Loot {
    int gold = 0;
};

struct Monster {
    int life = 0;
    float speed = 0.0f;
    std::string element;
    int kind = 0;
    glm::vec4 tint{1.0f};
    moteur::DataRef<Loot> loot;
    moteur::DataRef<Monster> summons;  // a reference into its own table
};

Loot parse_loot(moteur::DataReader& row) {
    Loot loot;
    loot.gold = row.integer("or", 0, 1000000);
    return loot;
}

Monster parse_monster(moteur::DataReader& row) {
    Monster m;
    m.life = row.integer("vie", 1, 100000);
    m.speed = row.number("vitesse", 0.0f, 20.0f, 3.0f);
    m.element = row.text("element", std::string("physique"));
    m.kind = row.choice("rang", {"normal", "magique", "rare"}, 0);
    m.tint = row.color("teinte", glm::vec4(1.0f));
    m.loot = row.reference<Loot>("butin", true);
    m.summons = row.reference<Monster>("invoque", true);
    return m;
}

void link_monster(Monster& m, moteur::DataLinker& linker) {
    linker.resolve(m.loot, "butin", true);
    linker.resolve(m.summons, "invoque", true);
}

// A temporary assets directory, removed at the end.
struct TempAssets {
    std::filesystem::path root;
    TempAssets() {
        static int counter = 0;
        root = std::filesystem::temp_directory_path() / ("moteur_data_test_" + std::to_string(++counter));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~TempAssets() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void write(const std::string& key, const std::string& text) const {
        const std::filesystem::path path = root / key;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    }
    void remove(const std::string& key) const { std::filesystem::remove(root / key); }
    std::string str() const {
        const std::u8string text = (root / "").u8string();
        return std::string(text.begin(), text.end());
    }
};

const char* const kMonstersA = R"({
  "version": 1,
  "defaults": { "vitesse": 2.5 },
  "rows": {
    "squelette": { "vie": 30, "teinte": [0.9, 0.9, 0.8] },
    "squelette_guerrier": { "base": "squelette", "vie": 50, "rang": "magique", "butin": "commun" },
    "_note": "a comment"
  }
})";
const char* const kMonstersB = R"({
  "version": 1,
  "rows": {
    "necromancien": { "vie": 80, "element": "neant", "invoque": "squelette", "_comment": "ignored" },
    "archer": { "base": "squelette_guerrier", "vitesse": 4 }
  }
})";
const char* const kLoot = R"({ "version": 1, "rows": { "commun": { "or": 5 }, "rare": { "or": 100 } } })";

void register_tables(moteur::DataTables& tables) {
    tables.add<Loot>("butin", "data/butin", parse_loot);
    tables.add<Monster>("monstres", "data/monstres", parse_monster, link_monster);
}

TempAssets standard_assets() {
    TempAssets assets;
    assets.write("data/monstres/a.json", kMonstersA);
    assets.write("data/monstres/b.json", kMonstersB);
    assets.write("data/butin/butin.json", kLoot);
    return assets;
}

bool has_issue(const moteur::DataIssues& issues, const std::string& where_part, const std::string& message_part) {
    for (const moteur::DataIssue& issue : issues.all()) {
        if (issue.where.find(where_part) != std::string::npos && issue.message.find(message_part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("DataReader reads typed fields and reports every problem with its place") {
    const nlohmann::json row = nlohmann::json::parse(R"({
        "vie": 0, "vitesse": "vite", "rang": "legendaire", "teinte": [1, 2], "nom": "Ossu", "typo": 1, "_c": 2,
        "liste": ["a", 3], "objet": { "x": 1, "y": 2 }
    })");
    moteur::DataIssues issues;
    moteur::DataReader reader(row, "monstres.json > ossu", issues);
    CHECK(reader.integer("vie", 1, 10) == 1);           // out of bounds: clamped, error
    CHECK(reader.number("vitesse", 0.0f, 10.0f, 3.0f) == 3.0f);  // wrong type: fallback, error
    CHECK(reader.choice("rang", {"normal", "rare"}, 0) == 0);
    CHECK(reader.color("teinte") == glm::vec4(1.0f));
    CHECK(reader.text("nom") == "Ossu");
    CHECK(reader.text("absent", std::string("x")) == "x");  // optional: no error
    CHECK(reader.integer("obligatoire", 0, 5) == 0);   // required and missing: error
    CHECK(reader.texts("liste") == std::vector<std::string>{"a"});
    moteur::DataReader object = reader.object("objet");
    CHECK(object.integer("x", 0, 9) == 1);
    object.finish();  // "y" unknown
    reader.finish();  // "typo" unknown, "_c" a comment
    CHECK(issues.errors() == 6);
    CHECK(issues.warnings() == 2);
    CHECK(has_issue(issues, "monstres.json > ossu > vie", "hors des bornes [1, 10]"));
    CHECK(has_issue(issues, "ossu > vitesse", "attendu un nombre, trouvé un texte"));
    CHECK(has_issue(issues, "ossu > rang", "« legendaire » n'est pas l'un de : normal, rare"));
    CHECK(has_issue(issues, "ossu > teinte", "3 ou 4 nombres"));
    CHECK(has_issue(issues, "ossu > obligatoire", "champ obligatoire absent"));
    CHECK(has_issue(issues, "ossu > liste[1]", "attendu un texte"));
    CHECK(has_issue(issues, "ossu > typo", "champ inconnu"));
    CHECK(has_issue(issues, "ossu > objet > y", "champ inconnu"));
    CHECK_FALSE(has_issue(issues, "_c", ""));
}

TEST_CASE("DataTables read a directory per table, with defaults, inheritance and references") {
    const TempAssets assets = standard_assets();
    moteur::DataTables tables;
    register_tables(tables);
    const moteur::DataIssues& issues = tables.load_all(assets.str());
    INFO(issues.text());
    REQUIRE_FALSE(issues.has_errors());
    CHECK(issues.warnings() == 0);

    const moteur::DataTable<Monster>& monsters = tables.get<Monster>();
    REQUIRE(monsters.size() == 4);
    // Indices in the order of the identifiers at the first load.
    CHECK(monsters.id(0) == "archer");
    CHECK(monsters.id(3) == "squelette_guerrier");

    const Monster& skeleton = monsters.at("squelette");
    CHECK(skeleton.life == 30);
    CHECK(skeleton.speed == 2.5f);  // the file's defaults
    CHECK(skeleton.tint == glm::vec4(0.9f, 0.9f, 0.8f, 1.0f));
    const Monster& warrior = monsters.at("squelette_guerrier");
    CHECK(warrior.life == 50);
    CHECK(warrior.speed == 2.5f);   // from its base
    CHECK(warrior.kind == 1);
    REQUIRE(warrior.loot.resolved());
    CHECK(warrior.loot->gold == 5);
    const Monster& archer = monsters.at("archer");  // two levels, across files
    CHECK(archer.life == 50);
    CHECK(archer.speed == 4.0f);
    CHECK(archer.loot->gold == 5);
    const Monster& necro = monsters.at("necromancien");
    CHECK(necro.speed == 3.0f);     // no base, no defaults in its file: the reader's fallback
    CHECK(necro.element == "neant");
    REQUIRE(necro.summons.resolved());
    CHECK(&*necro.summons == &skeleton);
    CHECK_FALSE(skeleton.loot.resolved());  // optional and absent
    CHECK(monsters.resolved(*monsters.index_of("archer"))["vie"] == 50);
    CHECK_FALSE(monsters.resolved(*monsters.index_of("necromancien")).contains("_comment"));
    CHECK(monsters.find("liche") == nullptr);
    CHECK_THROWS_AS(monsters.at("liche"), std::out_of_range);
    CHECK(monsters.files() == std::vector<std::string>{"data/monstres/a.json", "data/monstres/b.json"});
}

TEST_CASE("DataTables refuse broken tables with messages naming file, row and field") {
    TempAssets assets = standard_assets();
    assets.write("data/monstres/c.json", R"({ "version": 1, "rows": {
        "boucle_a": { "base": "boucle_b", "vie": 1 }, "boucle_b": { "base": "boucle_a", "vie": 1 },
        "orphelin": { "base": "fantome", "vie": 1 },
        "squelette": { "vie": 1 },
        "voleur": { "vie": 10, "butin": "epique" },
        "lent": { "vie": 10, "vitesse": 99, "vitess": 1 }
    } })");
    assets.write("data/butin/casse.json", "{ pas du json");
    moteur::DataTables tables;
    register_tables(tables);
    const moteur::DataIssues& issues = tables.load_all(assets.str());
    INFO(issues.text());
    CHECK(has_issue(issues, "c.json > boucle_", "héritage circulaire"));
    CHECK(has_issue(issues, "c.json > orphelin > base", "« fantome » n'existe pas"));
    CHECK(has_issue(issues, "c.json > squelette", "déjà défini dans data/monstres/a.json"));
    CHECK(has_issue(issues, "c.json > lent > vitesse", "hors des bornes"));
    CHECK(has_issue(issues, "c.json > lent > vitess", "champ inconnu"));
    CHECK(has_issue(issues, "data/butin/casse.json", "JSON invalide"));
    // A row with an error refuses its whole table; the other one loads, and its reference to the
    // missing table's row is reported.
    CHECK_FALSE(tables.get<Monster>().loaded());
    CHECK(tables.get<Loot>().loaded());

    TempAssets other = standard_assets();
    other.write("data/monstres/c.json", R"({ "version": 1, "rows": { "voleur": { "vie": 10, "butin": "epique" } } })");
    moteur::DataTables second;
    register_tables(second);
    const moteur::DataIssues& linked = second.load_all(other.str());
    CHECK(has_issue(linked, "c.json > voleur > butin", "« epique » n'existe pas dans la table butin"));
}

TEST_CASE("A hot reload keeps every row at its index, refuses removals and errors") {
    const TempAssets assets = standard_assets();
    moteur::DataTables tables;
    register_tables(tables);
    REQUIRE_FALSE(tables.load_all(assets.str()).has_errors());
    moteur::DataTable<Monster>& monsters = tables.get<Monster>();
    const Monster* warrior_before = &monsters.at("squelette_guerrier");
    const std::uint32_t warrior_index = *monsters.index_of("squelette_guerrier");
    const moteur::DataRef<Monster> held = monsters.ref("squelette_guerrier");  // an entity's reference

    // A changed value and a new row ("abomination", first in alphabetical order): added at the end.
    assets.write("data/monstres/a.json", std::string(kMonstersA).replace(std::string(kMonstersA).find("\"vie\": 50"), 9, "\"vie\": 70"));
    assets.write("data/monstres/d.json", R"({ "version": 1, "rows": { "abomination": { "vie": 500, "invoque": "abomination" } } })");
    REQUIRE(tables.reload_file(assets.str(), "data/monstres/d.json") == &monsters);
    INFO(tables.last_issues().text());
    CHECK(monsters.revision() == 1);
    CHECK(monsters.size() == 5);
    CHECK(*monsters.index_of("squelette_guerrier") == warrior_index);
    CHECK(held->life == 70);  // the held reference sees the new value
    CHECK(&*held == &monsters[warrior_index]);
    CHECK(*monsters.index_of("abomination") == 4);
    CHECK(monsters.sorted().front() == 4);  // the sorted order puts it first
    const Monster& abomination = monsters.at("abomination");
    REQUIRE(abomination.summons.resolved());
    CHECK(&*abomination.summons == &abomination);  // a reference to a new row of its own table
    (void)warrior_before;

    // A removed row: refused, the previous content stays.
    assets.remove("data/monstres/b.json");
    CHECK(tables.reload(assets.str(), "monstres") == false);
    CHECK(has_issue(tables.last_issues(), "necromancien", "ligne retirée"));
    CHECK(monsters.size() == 5);
    CHECK(monsters.revision() == 1);
    assets.write("data/monstres/b.json", kMonstersB);

    // An error: refused too.
    assets.write("data/monstres/a.json", R"({ "version": 1, "rows": { "squelette": { "vie": -1 }, "squelette_guerrier": { "vie": 1 } } })");
    CHECK_FALSE(tables.reload(assets.str(), "monstres"));
    CHECK(held->life == 70);
    // A file outside any table: nothing.
    CHECK(tables.reload_file(assets.str(), "maps/zigzag.json") == nullptr);
}

TEST_CASE("The compiled form gives exactly the same rows") {
    const TempAssets assets = standard_assets();
    const std::string compiled = assets.str() + "compiled/";
    moteur::DataIssues issues;
    {
        moteur::DataTables tables;
        register_tables(tables);
        REQUIRE(tables.compile(assets.str(), compiled, issues));
    }
    moteur::DataTables from_json;
    register_tables(from_json);
    REQUIRE_FALSE(from_json.load_all(assets.str()).has_errors());
    moteur::DataTables from_binary;
    register_tables(from_binary);
    from_binary.set_source(moteur::DataSource::Compiled, compiled);
    const moteur::DataIssues& binary_issues = from_binary.load_all(assets.str());
    INFO(binary_issues.text());
    REQUIRE_FALSE(binary_issues.has_errors());
    const auto& a = from_json.get<Monster>();
    const auto& b = from_binary.get<Monster>();
    REQUIRE(a.size() == b.size());
    for (std::uint32_t i = 0; i < a.size(); ++i) {
        CHECK(a.id(i) == b.id(i));
        CHECK(a[i].life == b[i].life);
        CHECK(a[i].speed == b[i].speed);  // bit for bit
        CHECK(a[i].tint == b[i].tint);
        CHECK(a[i].element == b[i].element);
        CHECK(a[i].loot.id == b[i].loot.id);
        CHECK(a[i].loot.resolved() == b[i].loot.resolved());
        CHECK(a.resolved(i) == b.resolved(i));
    }
    CHECK(a.source_hash() == b.source_hash());

    // A damaged file is refused with a message.
    std::ofstream(std::filesystem::path(std::u8string(compiled.begin(), compiled.end())) / "butin.mdat",
                  std::ios::binary | std::ios::trunc) << std::string("MDAT\x01\x00\x00\x00garbage", 15);
    moteur::DataTables damaged;
    register_tables(damaged);
    damaged.set_source(moteur::DataSource::Compiled, compiled);
    CHECK(has_issue(damaged.load_all(assets.str()), "butin.mdat", "illisible"));
}
