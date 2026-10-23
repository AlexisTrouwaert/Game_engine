// Localization (milestone 9 bis, part 5): CLDR plural rules and number formats, the string files
// (syntax inspired by Fluent), fallbacks, checks and pseudo-localization.

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "moteur/input.hpp"
#include "moteur/locale.hpp"
#include "moteur/localization.hpp"

using moteur::Localization;
using moteur::LocalizationIssue;
using moteur::PluralCategory;
using moteur::PluralOperands;

TEST_CASE("locale: every CLDR plural sample falls in its category") {
    std::ifstream file(std::string(MOTEUR_TEST_DATA) + "locale/plural_samples.txt");
    REQUIRE(file);
    int count = 0;
    int failures = 0;
    for (std::string line; std::getline(file, line);) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string language, kind, category, sample;
        fields >> language >> kind >> category >> sample;
        const PluralCategory got =
            moteur::plural_category(language, PluralOperands::parse(sample), kind == "ordinal");
        ++count;
        if (category != moteur::plural_category_name(got)) {
            if (++failures <= 10) {
                MESSAGE(language << " " << kind << " " << sample << ": " << moteur::plural_category_name(got)
                                 << ", CLDR says " << category);
            }
        }
    }
    CHECK(count > 10000);
    CHECK(failures == 0);
}

TEST_CASE("locale: plural categories of French, English, Russian and Arabic") {
    const auto cardinal = [](const char* language, double n, int digits = 0) {
        return moteur::plural_category(language, PluralOperands::of(n, digits));
    };
    CHECK(cardinal("fr", 0) == PluralCategory::One);  // « 0 objet »
    CHECK(cardinal("fr", 1) == PluralCategory::One);
    CHECK(cardinal("fr", 1.5, 1) == PluralCategory::One);
    CHECK(cardinal("fr", 2) == PluralCategory::Other);
    CHECK(cardinal("en", 1) == PluralCategory::One);
    CHECK(cardinal("en", 1, 1) == PluralCategory::Other);  // "1.0 objects"
    CHECK(cardinal("en", 0) == PluralCategory::Other);
    CHECK(cardinal("ru", 21) == PluralCategory::One);
    CHECK(cardinal("ru", 23) == PluralCategory::Few);
    CHECK(cardinal("ru", 25) == PluralCategory::Many);
    CHECK(cardinal("ar", 0) == PluralCategory::Zero);
    CHECK(cardinal("ar", 2) == PluralCategory::Two);
    CHECK(cardinal("ar", 105) == PluralCategory::Few);
    CHECK(cardinal("pt-BR", 1) == PluralCategory::One);   // a region falls back to its language
    CHECK(cardinal("xx-unknown", 1) == PluralCategory::One);  // then to English
    CHECK(moteur::plural_category("en", PluralOperands::of(2), true) == PluralCategory::Two);  // 2nd
    CHECK(moteur::plural_category("en", PluralOperands::of(23), true) == PluralCategory::Few);  // 23rd
    CHECK(moteur::plural_category("fr", PluralOperands::of(1), true) == PluralCategory::One);   // 1er
}

TEST_CASE("locale: numbers written in each language's conventions") {
    using moteur::format_number;
    using moteur::NumberFormatOptions;
    CHECK(format_number("fr", 1234567.891) == "1 234 567,891");
    CHECK(format_number("en", 1234567.891) == "1,234,567.891");
    CHECK(format_number("de", 1234.5) == "1.234,5");
    CHECK(format_number("hi", 1234567) == "12,34,567");
    CHECK(format_number("es", 1234) == "1234");     // grouping from five digits in Spanish
    CHECK(format_number("es", 12345) == "12.345");
    CHECK(format_number("en", -3.14159) == "-3.142");
    CHECK(format_number("en", 2.5, NumberFormatOptions{2, 2}) == "2.50");
    CHECK(format_number("en", 1234, NumberFormatOptions{0, 3, false}) == "1234");
    CHECK(format_number("fr", 0.456, NumberFormatOptions{0, 0, true, true}) == "46 %");
    CHECK(format_number("en", 0.456, NumberFormatOptions{0, 1, true, true}) == "45.6%");
    CHECK(format_number("en", 0.5, NumberFormatOptions{0, 0}) == "1");  // halves away from zero
}

namespace {

constexpr const char* kFrench = R"(# A comment
title = Options
greeting = Bonjour { $name } !
loot = { $count ->
        [0] Vous n'avez rien trouvé.
        [one] Vous avez trouvé un objet.
       *[other] Vous avez trouvé { $count } objets.
    }
-sword = épée
    .gender = feminine
-axe = hache
    .gender = feminine
-shield = bouclier
    .gender = masculine
rusty = { -sword.gender ->
        [feminine] Une { -sword } rouillée
       *[masculine] Un { -sword } rouillé
    }
rusty-shield = { -shield.gender ->
        [feminine] Une { -shield } rouillée
       *[masculine] Un { -shield } rouillé
    }
health = Vie : { NUMBER($ratio, style: "percent") }
precise = { NUMBER($value, minimumFractionDigits: 2) }
reference = { title } et { greeting }
time = { DURATION($seconds) } de jeu
button = Jouer
    .tooltip = Commencer une partie
multiline =
    Première ligne
    deuxième ligne

    après un blanc
literal = Accolade : {"{"} ; guillemet : {"\""}
)";

constexpr const char* kEnglish = R"(title = Options
greeting = Hello { $name }!
loot = { $count ->
        [0] You found nothing.
        [one] You found an item.
       *[other] You found { $count } items.
    }
-sword = sword
    .gender = neuter
rusty = A rusty { -sword }
health = Health: { NUMBER($ratio, style: "percent") }
precise = { NUMBER($value, minimumFractionDigits: 2) }
reference = { title } and { greeting }
time = { DURATION($seconds) } played
button = Play
    .tooltip = Start a game
extra-only-in-english = Oops
)";

constexpr const char* kEngine = R"(moteur-duration-hours = { $hours } h { $minutes } min
moteur-duration-minutes = { $minutes } min { $seconds } s
moteur-duration-seconds = { $seconds } s
input-mouse-left = Clic gauche
input-key-space = Espace
)";

void fill(Localization& l) {
    l.set_base_language("fr");
    REQUIRE(l.add_resource("fr", kEngine, "fr/moteur.ftl"));
    REQUIRE(l.add_resource("fr", kFrench, "fr/test.ftl"));
    REQUIRE(l.add_resource("en", kEnglish, "en/test.ftl"));
    REQUIRE(l.set_language("fr"));
}

}  // namespace

TEST_CASE("localization: messages, variables, plurals, terms and attributes") {
    Localization l;
    fill(l);
    CHECK(l.format("title") == "Options");
    CHECK(l.format("greeting", {{"name", "Gauvain"}}) == "Bonjour Gauvain !");
    CHECK(l.format("loot", {{"count", 0}}) == "Vous n'avez rien trouvé.");  // exact value first
    CHECK(l.format("loot", {{"count", 1}}) == "Vous avez trouvé un objet.");
    CHECK(l.format("loot", {{"count", 1234}}) == "Vous avez trouvé 1 234 objets.");
    CHECK(l.format("rusty") == "Une épée rouillée");
    CHECK(l.format("rusty-shield") == "Un bouclier rouillé");
    CHECK(l.format("health", {{"ratio", 0.75}}) == "Vie : 75 %");
    CHECK(l.format("precise", {{"value", 3}}) == "3,00");
    CHECK(l.format("reference", {{"name", "toi"}}) == "Options et Bonjour toi !");
    CHECK(l.format("time", {{"seconds", 3725.0}}) == "1 h 2 min de jeu");
    CHECK(l.format("time", {{"seconds", 75}}) == "1 min 15 s de jeu");
    CHECK(l.format("button.tooltip") == "Commencer une partie");
    CHECK(l.format("multiline") == "Première ligne\ndeuxième ligne\n\naprès un blanc");
    CHECK(l.format("literal") == "Accolade : { ; guillemet : \"");
    CHECK(l.format("greeting") == "Bonjour {$name} !");  // a missing variable shows

    REQUIRE(l.set_language("en"));
    CHECK(l.format("loot", {{"count", 1}}) == "You found an item.");
    CHECK(l.format("loot", {{"count", 2}}) == "You found 2 items.");
    CHECK(l.format("rusty") == "A rusty sword");
    CHECK(l.format("health", {{"ratio", 0.75}}) == "Health: 75%");
    CHECK(l.format("precise", {{"value", 3}}) == "3.00");
    CHECK(l.format("rusty-shield") == "Un bouclier rouillé");  // missing in English: the base language's
    CHECK(l.format("nothing-here") == "{nothing-here}");
    CHECK(l.format_number(1234.5) == "1,234.5");
}

TEST_CASE("localization: changing language bumps the generation, unknown languages are refused") {
    Localization l;
    fill(l);
    const std::uint64_t before = l.generation();
    CHECK(l.set_language("en"));
    CHECK(l.generation() != before);
    const std::uint64_t now = l.generation();
    CHECK(l.set_language("en"));
    CHECK(l.generation() == now);  // the same language: nothing to redo
    CHECK_FALSE(l.set_language("de"));
    CHECK(l.language() == "en");
}

TEST_CASE("localization: syntax errors are reported with their line, the rest is read") {
    Localization l;
    l.set_base_language("fr");
    const bool ok = l.add_resource("fr", "good = Bien\nbad = { $x\nalso-good = Encore\nselect = { $n ->\n    [one] un\n}\n",
                                   "fr/broken.ftl");
    CHECK_FALSE(ok);
    REQUIRE(l.set_language("fr"));
    CHECK(l.format("good") == "Bien");
    CHECK(l.format("also-good") == "Encore");
    REQUIRE(l.issues().size() >= 2);
    CHECK(l.issues()[0].kind == LocalizationIssue::Kind::Syntax);
    CHECK(l.issues()[0].file == "fr/broken.ftl");
    CHECK(l.issues()[0].line == 2);
    CHECK(l.issues()[1].message.find("default") != std::string::npos);
}

TEST_CASE("localization: the check finds missing, extra, untranslated and mismatched messages") {
    Localization l;
    fill(l);
    REQUIRE(l.add_resource("en", "greeting = Hello { $who }!\nbad-ref = { -nowhere }\n", "en/more.ftl"));
    const std::vector<LocalizationIssue> issues = l.check();
    const auto has = [&](LocalizationIssue::Kind kind, const std::string& language, const std::string& id) {
        for (const LocalizationIssue& i : issues) {
            if (i.kind == kind && i.language == language && i.id == id) {
                return true;
            }
        }
        return false;
    };
    CHECK(has(LocalizationIssue::Kind::Missing, "en", "rusty-shield"));
    CHECK(has(LocalizationIssue::Kind::Missing, "en", "literal"));
    CHECK(has(LocalizationIssue::Kind::Extra, "en", "extra-only-in-english"));
    CHECK(has(LocalizationIssue::Kind::Extra, "en", "bad-ref"));
    CHECK(has(LocalizationIssue::Kind::Variables, "en", "greeting"));
    CHECK(has(LocalizationIssue::Kind::Untranslated, "en", "title"));
    CHECK(has(LocalizationIssue::Kind::BadReference, "en", "bad-ref"));
    CHECK_FALSE(has(LocalizationIssue::Kind::Missing, "en", "loot"));
}

TEST_CASE("localization: the pseudo-localization is accented, longer and bracketed") {
    Localization l;
    fill(l);
    REQUIRE(l.set_language(moteur::kPseudoLanguage));
    const std::string title = l.format("title");
    CHECK(title.front() == '[');
    CHECK(title.back() == ']');
    CHECK(title.find("\xC3\x93") != std::string::npos);  // Ó
    CHECK(title.size() > std::string("Options").size() + 4);
    // Variables and plurals still work (French rules: it is made from the base language).
    const std::string loot = l.format("loot", {{"count", 3}});
    CHECK(loot.find('3') != std::string::npos);
    CHECK(loot.front() == '[');
    CHECK(l.format("loot", {{"count", 1234}}).find("1 234") != std::string::npos);  // French numbers
}

TEST_CASE("localization: values inside a right-to-left text are isolated") {
    Localization l;
    l.set_base_language("fr");
    REQUIRE(l.add_resource("ar", "hello = \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 { $name }\n", "ar/t.ftl"));
    REQUIRE(l.set_language("ar"));
    CHECK(l.format("hello", {{"name", "Gauvain"}}) ==
          "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xE2\x81\xA8Gauvain\xE2\x81\xA9");
}

TEST_CASE("localization: the input labels come from the engine's strings") {
    Localization l;
    fill(l);
    moteur::InputSource left;
    left.kind = moteur::InputSource::Kind::MouseButton;
    left.code = 1;  // SDL_BUTTON_LEFT
    CHECK(moteur::input_source_label(left, &l) == "Clic gauche");
    CHECK(moteur::input_source_label(left) == "Left click");  // without strings: English
    const auto space = moteur::parse_input_source("key:Space");
    REQUIRE(space.has_value());
    CHECK(moteur::input_source_label(*space, &l) == "Espace");
}
