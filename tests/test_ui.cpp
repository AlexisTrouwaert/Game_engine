// The interface's files (milestone 9 bis, part 6): the markup and style readers, the selectors and
// the cascade, the check of a game's screens. The layout, the drawing and the navigation are
// checked by `bac_a_sable --ui-tour` (they need fonts and a renderer: tools/tests/check_references.py).

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "moteur/ui.hpp"
#include "moteur/ui_markup.hpp"
#include "moteur/ui_style.hpp"

namespace ui = moteur::ui;

TEST_CASE("ui markup: elements, attributes, text, entities, comments") {
    const ui::MarkupNode root = ui::parse_markup(R"(<?xml version="1.0"?>
<!-- a screen -->
<ui back-action="close">
    <style src="ui/theme.uss"/>
    <panel id="p" class="a b">
        <label>  Hello &amp;   <!-- inside --> world &#233; &#x263A;  </label>
        <button action='go' text="{play}"/>
    </panel>
</ui>)");
    CHECK(root.tag == "ui");
    CHECK(root.attribute("back-action") == "close");
    REQUIRE(root.children.size() == 2);
    CHECK(root.children[0].tag == "style");
    const ui::MarkupNode& panel = root.children[1];
    CHECK(panel.attribute("class") == "a b");
    CHECK(panel.line == 5);
    REQUIRE(panel.children.size() == 2);
    REQUIRE(panel.children[0].children.size() >= 1);
    std::string text;
    for (const ui::MarkupNode& t : panel.children[0].children) text += t.text + "|";
    CHECK(text == "Hello & world \xC3\xA9 \xE2\x98\xBA|");  // one text: the comment is taken out
    CHECK(panel.children[1].attribute("action") == "go");
    CHECK(panel.children[1].attribute("missing", "x") == "x");
}

TEST_CASE("ui markup: errors say their line") {
    const auto line_of = [](const char* text) {
        try {
            ui::parse_markup(text);
        } catch (const ui::MarkupError& e) {
            return e.line;
        }
        return 0;
    };
    CHECK(line_of("<ui>\n<panel>\n</ui>") == 3);           // </ui> closes <panel>
    CHECK(line_of("<ui>\n<b x=1/>\n</ui>") == 2);           // unquoted value
    CHECK(line_of("<ui>\n\n<b>a & b</b></ui>") == 3);       // bare &
    CHECK(line_of("<ui></ui>\n<extra/>") == 2);
    CHECK(line_of("<ui a=\"1\" a=\"2\"/>") == 1);           // attribute twice
}

TEST_CASE("ui styles: rules, selectors, specificity, comments") {
    const ui::StyleSheet sheet = ui::parse_style_sheet(R"(
/* the panels */
panel, .box { padding: 4 8; background: #102030; }
panel > button:focus { color: rgba(255, 128, 0, 0.5); }
#ok.big { font-size: 20px; }
)",
                                                       "t.uss");
    REQUIRE(sheet.rules.size() == 3);
    CHECK(sheet.rules[0].selectors.size() == 2);
    CHECK(sheet.rules[0].line == 3);
    CHECK(sheet.rules[1].selectors[0].parts.size() == 2);
    CHECK(sheet.rules[1].selectors[0].combinators[0] == '>');
    CHECK(sheet.rules[1].selectors[0].specificity == 102);  // :focus 100 + two types
    CHECK(sheet.rules[2].selectors[0].specificity == 10100);
    CHECK(sheet.rules[0].declarations[0].property == "padding");
    CHECK(sheet.rules[0].declarations[0].value == "4 8");
    CHECK_THROWS_AS(ui::parse_style_sheet("a { b: c; "), ui::StyleError);
    CHECK_THROWS_AS(ui::parse_style_sheet("a:wobble { b: c; }"), ui::StyleError);
}

TEST_CASE("ui styles: matching descendants, children and states") {
    const std::vector<std::string> panel_classes{"menu"};
    const std::vector<std::string> button_classes{"big"};
    ui::MatchNode root;
    root.type = "ui";
    ui::MatchNode panel;
    panel.type = "panel";
    panel.classes = &panel_classes;
    panel.parent = &root;
    ui::MatchNode row;
    row.type = "row";
    row.parent = &panel;
    ui::MatchNode button;
    button.type = "button";
    button.id = "ok";
    button.classes = &button_classes;
    button.parent = &row;
    button.states = ui::kFocus;
    button.first_child = true;
    const auto match = [&](const char* selector) { return ui::matches(ui::parse_selectors(selector)[0], button); };
    CHECK(match("button"));
    CHECK(match(".menu button"));
    CHECK_FALSE(match(".menu > button"));
    CHECK(match("panel > row > button.big#ok"));
    CHECK(match("button:focus:first-child"));
    CHECK_FALSE(match("button:hover"));
    CHECK(match("ui *"));
    CHECK_FALSE(match("label"));
}

TEST_CASE("ui styles: cascade, inheritance, shorthands, variables, transitions") {
    const ui::StyleSheet sheet = ui::parse_style_sheet(R"(
panel { --accent: #ff8000; color: #ffffff; font-size: 18; padding: 1 2 3; margin: 0 auto; }
label { color: var(--accent); width: 50%; border: 2 #00ff00; transition: opacity 200ms ease-out, color 0.5s linear 0.1s; }
label { flex: 2; text-outline: 1.5 black; wobble: 3; width: blue; }
)");
    std::map<std::string, std::string, std::less<>> vars;
    std::vector<const ui::Declaration*> panel_decls;
    for (const auto& d : sheet.rules[0].declarations) panel_decls.push_back(&d);
    const ui::ComputedStyle panel = ui::compute_style(nullptr, panel_decls, vars);
    CHECK(panel.padding[0] == 1.0f);
    CHECK(panel.padding[1] == 2.0f);
    CHECK(panel.padding[2] == 3.0f);
    CHECK(panel.padding[3] == 2.0f);
    CHECK(panel.margin[1].is_auto());
    CHECK(panel.margin[0].unit == ui::Length::Unit::Points);
    CHECK(vars.at("--accent") == "#ff8000");

    std::vector<const ui::Declaration*> label_decls;
    for (const auto& d : sheet.rules[1].declarations) label_decls.push_back(&d);
    for (const auto& d : sheet.rules[2].declarations) label_decls.push_back(&d);
    std::vector<std::string> warnings;
    std::map<std::string, std::string, std::less<>> label_vars = vars;
    const ui::ComputedStyle label = ui::compute_style(&panel, label_decls, label_vars, &warnings);
    CHECK(label.color.r == doctest::Approx(1.0f));
    CHECK(label.color.g == doctest::Approx(128.0f / 255.0f));
    CHECK(label.font_size == 18.0f);  // inherited
    CHECK(label.padding[0] == 0.0f);  // not inherited
    CHECK(label.width.unit == ui::Length::Unit::Percent);  // "width: blue" is refused, the 50 % stays
    CHECK(label.border_width == 2.0f);
    CHECK(label.grow == 2.0f);
    CHECK(label.text_outline_width == 1.5f);
    REQUIRE(label.transitions.size() == 2);
    CHECK(label.transitions[0].property == "opacity");
    CHECK(label.transitions[0].duration == doctest::Approx(0.2f));
    CHECK(label.transitions[0].easing == ui::Easing::EaseOut);
    CHECK(label.transitions[1].delay == doctest::Approx(0.1f));
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].find("wobble") == 0);
    CHECK(warnings[1].find("width") == 0);
}

TEST_CASE("ui styles: flex shorthand and basis, as in CSS") {
    const auto style = [](const char* text) {
        const ui::StyleSheet sheet = ui::parse_style_sheet(text);
        std::vector<const ui::Declaration*> decls;
        for (const auto& d : sheet.rules[0].declarations) decls.push_back(&d);
        std::map<std::string, std::string, std::less<>> vars;
        return ui::compute_style(nullptr, decls, vars);
    };
    const ui::ComputedStyle one = style("a { flex: 1; }");  // the whole room shared: basis 0
    CHECK(one.grow == 1.0f);
    CHECK(one.shrink == 1.0f);
    CHECK(one.basis.unit == ui::Length::Unit::Points);
    CHECK(one.basis.value == 0.0f);
    const ui::ComputedStyle content = style("a { flex: 1 0 auto; }");  // the free room only
    CHECK(content.shrink == 0.0f);
    CHECK(content.basis.is_auto());
    const ui::ComputedStyle fixed = style("a { flex-basis: 120; }");
    CHECK(fixed.basis.value == 120.0f);
    CHECK(fixed.grow == 0.0f);
    const ui::ComputedStyle none = style("a { flex: none; }");
    CHECK(none.grow == 0.0f);
    CHECK(none.shrink == 0.0f);
    CHECK(none.basis.is_auto());
}

TEST_CASE("ui styles: colors, lengths, easing") {
    CHECK(ui::parse_color("#fff") == glm::vec4(1.0f));
    CHECK(ui::parse_color("#00000080")->a == doctest::Approx(128.0f / 255.0f));
    CHECK(ui::parse_color("transparent")->a == 0.0f);
    CHECK_FALSE(ui::parse_color("#ggg").has_value());
    CHECK(ui::parse_length("12px")->value == 12.0f);
    CHECK(ui::parse_length("25%")->unit == ui::Length::Unit::Percent);
    CHECK(ui::parse_length("auto")->is_auto());
    CHECK_FALSE(ui::parse_length("12em").has_value());
    CHECK(ui::ease(ui::Easing::Linear, 0.25f) == 0.25f);
    CHECK(ui::ease(ui::Easing::Ease, 0.0f) == 0.0f);
    CHECK(ui::ease(ui::Easing::Ease, 1.0f) == 1.0f);
    CHECK(ui::ease(ui::Easing::EaseIn, 0.5f) < 0.5f);
}

TEST_CASE("ui model: values, versions") {
    moteur::UiModel model;
    const std::uint64_t v0 = model.version();
    model.set("hero.health", 0.5);
    CHECK(model.number("hero.health") == 0.5);
    CHECK(model.version() > v0);
    const std::uint64_t v1 = model.version();
    model.set("hero.health", 0.5);  // unchanged
    CHECK(model.version() == v1);
    model.set("name", "Gauvain");
    CHECK(model.text("name") == "Gauvain");
    model.set("on", true);
    CHECK(model.flag("on"));
    model.set_count("slots", 3);
    CHECK(model.number("slots.count") == 3.0);
    CHECK(model.version_of("name") > model.version_of("hero.health"));
}

TEST_CASE("ui check: the screens of a game read without showing them") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "moteur_test_ui_check";
    fs::remove_all(root);
    fs::create_directories(root / "ui");
    const auto write = [&root](const char* name, const char* text) { std::ofstream(root / "ui" / name, std::ios::binary) << text; };
    write("good.uss", "button { color: #fff; --accent: red; } label { color: var(--accent); }");
    write("good.ui", "<ui><style src=\"ui/good.uss\"/><label text=\"{$hero.name} {{\"/></ui>");
    write("bad.uss", "button { colour: red; width: blue; }");
    write("bad.ui", "<ui><style src=\"ui/missing.uss\"/><label style=\"wobble: 2\"/></ui>");
    write("broken.ui", "<ui><label></ui>");
    const std::vector<std::string> problems = moteur::check_ui_files({root.string()});
    const auto has = [&problems](const char* file, const char* what) {
        for (const std::string& p : problems) {
            if (p.starts_with(file) && p.find(what) != std::string::npos) return true;
        }
        return false;
    };
    CHECK(has("ui/bad.uss:1", "colour"));
    CHECK(has("ui/bad.uss:1", "width"));
    CHECK(has("ui/bad.ui:1", "missing.uss"));
    CHECK(has("ui/bad.ui:1", "wobble"));
    CHECK(has("ui/broken.ui", "line 1"));
    for (const std::string& p : problems) {
        CHECK_MESSAGE(!p.starts_with("ui/good"), p);
    }
    CHECK(problems.size() == 5);
    fs::remove_all(root);
}
