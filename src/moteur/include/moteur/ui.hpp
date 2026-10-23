#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "moteur/asset_cache.hpp"
#include "moteur/font.hpp"

union SDL_Event;

/// The interface system of the games (milestone 9 bis, part 6): screens written as markup (`.ui`,
/// an XML subset) and styled by style sheets (`.uss`, a CSS subset), laid out by a flexbox subset,
/// drawn with the engine's sprites and text, driven by the game's actions in the ticks (keyboard,
/// gamepad and mouse together; a replay replays the menus), localized, reloaded while the game runs.
/// FORMATS.md, section 23, describes the files; GUIDE.md, the use.
///
/// ```
/// moteur::Ui ui(app);                                   // the fonts, textures and strings of the app
/// moteur::UiDocument& pause = ui.open("ui/pause.ui", 10);
/// ui.model().set("hero.health", 0.75);                 // what `{$hero.health}` shows
/// // In the tick:
/// ui.tick(moteur::UiInput::from_actions(app.input()));
/// for (const moteur::UiEvent& e : ui.take_events()) { if (e.action == "resume") ... }
/// // In the render:
/// ui.render(renderer, dt);
/// ```
///
/// Main thread.
namespace moteur {

class Application;
class Assets;
class Input;
class Localization;
class Renderer;
class UiElement;
class UiDocument;
struct UiSystem;

/// A value of the interface's model: a number, a text or a yes/no.
using UiValue = std::variant<double, std::string, bool>;

/// The data the screens show and edit (`{$path}` in a text, `bind="path"` on a control). Setting a
/// value redoes only the elements that use it; the screens never poll the game.
class UiModel {
public:
    /// Sets the value at `path` ("hero.health"); an unchanged value costs nothing.
    void set(std::string_view path, UiValue value);
    /// Convenience: numbers, yes/no and texts.
    ///@{
    void set(std::string_view path, int value) { set(path, UiValue(static_cast<double>(value))); }
    void set(std::string_view path, double value) { set(path, UiValue(value)); }
    void set(std::string_view path, float value) { set(path, UiValue(static_cast<double>(value))); }
    void set(std::string_view path, bool value) { set(path, UiValue(value)); }
    void set(std::string_view path, const char* value) { set(path, UiValue(std::string(value))); }
    void set(std::string_view path, std::string value) { set(path, UiValue(std::move(value))); }
    ///@}
    /// The value, or `fallback`.
    UiValue get(std::string_view path, const UiValue& fallback = UiValue(std::string())) const;
    /// The value if it is a number (a yes/no counts 0 or 1), else `fallback`.
    double number(std::string_view path, double fallback = 0) const;
    /// The value if it is a text; "" otherwise (absent, a number, a yes/no).
    std::string text(std::string_view path) const;
    /// The value as a yes/no (a number: not 0; a text: not empty, "false" nor "0"), or `fallback`.
    bool flag(std::string_view path, bool fallback = false) const;
    /// Whether a value was set at `path`.
    bool has(std::string_view path) const;
    /// The number of items of a list (`<repeat source="slots">`): items are `slots.0.name`...
    void set_count(std::string_view list, int count) { set(std::string(list) + ".count", count); }
    /// Bumped by every change (the interface compares it).
    std::uint64_t version() const { return version_; }
    /// The version a path last changed at (0: never set).
    std::uint64_t version_of(std::string_view path) const;

private:
    struct Entry {
        UiValue value;
        std::uint64_t version = 0;
    };
    std::map<std::string, Entry, std::less<>> values_;
    std::uint64_t version_ = 1;
};

/// What the game reads from the interface after a tick.
struct UiEvent {
    /// What happened.
    enum class Kind : std::uint8_t {
        Activate,  ///< a button (or any element with `action`) was confirmed or clicked
        Change,    ///< a control changed its value (checkbox, slider, select, input): written in the model
        Back,      ///< "back" was pressed: the document's `back-action`
        Focus,     ///< the player moved the focus to this element (sounds); not sent for the focus a screen opens with
    };
    Kind kind = Kind::Activate;  ///< what happened
    std::string action;    ///< the element's `action` (or the document's `back-action`)
    std::string id;        ///< the element's id ("" without one)
    std::string document;  ///< the document's path
    int index = -1;        ///< in a `<repeat>`: the item's index
    UiValue value;         ///< Change: the new value
};

/// The navigation of a tick: the game's actions (UiInput::from_actions() reads the usual
/// `menu_*` ones), and the pointer.
struct UiInput {
    bool up = false;       ///< move the focus up (menu_up pressed in this tick)
    bool down = false;     ///< move the focus down
    bool left = false;     ///< left: a slider or a select changes, else the focus moves
    bool right = false;    ///< right: the same
    bool confirm = false;  ///< activate the focused element
    bool back = false;     ///< the document's back-action (Escape, Start)
    bool click = false;         ///< the pointer's button pressed in this tick
    bool held = false;          ///< the pointer's button down (sliders follow it)
    float wheel = 0;            ///< notches, positive up
    glm::vec2 pointer{-1.0f};   ///< in window pixels, (-1, -1): none
    /// The actions menu_up, menu_down, menu_left, menu_right, menu_confirm, menu_click, pause (back)
    /// and the pointer of `input`, those that exist.
    static UiInput from_actions(const Input& input);
};

/// An element of a document. Handles stay valid until the document is closed or reloaded (a
/// reload keeps the elements with an id: find them again with UiDocument::find()).
class UiElement {
public:
    /// Its element name ("button").
    const std::string& tag() const;
    /// Its `id` ("" without one).
    const std::string& id() const;
    /// Its text template ("{pause-title}", "Vie : {$hero.health}"), replaced.
    void set_text(std::string text);
    /// Adds (`on`) or removes a class: the styles of the class apply, with their transitions.
    void set_class(std::string_view name, bool on);
    /// Whether it has the class.
    bool has_class(std::string_view name) const;
    /// Hidden: not drawn, not laid out, not navigated (its `visible` attribute, set by the game).
    void set_visible(bool visible);
    /// Whether it is shown (its own flag, not its parents').
    bool visible() const;
    /// Disabled: not focusable nor activable, styled by `:disabled`.
    void set_enabled(bool enabled);
    /// An attribute's value as written ("" when absent).
    std::string attribute(std::string_view name) const;
    /// Its rectangle on the screen, in pixels, once laid out: x, y, width, height.
    glm::vec4 rect() const;
    /// Places an absolute element at a point of the screen, in pixels (world labels).
    void set_screen_position(glm::vec2 pixels);
    /// Declarations added after its `style` attribute ("opacity: 0.4"), as a style sheet would
    /// write them; the same text again costs nothing. Empty: only the attribute's.
    void set_style(std::string_view declarations);

    /// Made by the interface for its elements (not by the game).
    explicit UiElement(void* node) : node_(node) {}

private:
    friend struct UiSystem;
    friend class UiDocument;
    void* node_;
};

/// A screen read from a `.ui` file.
class UiDocument {
public:
    /// The path it was opened from ("ui/pause.ui").
    const std::string& path() const;
    /// The element with this id, or null.
    UiElement* find(std::string_view id);
    /// Its `<ui>` element.
    UiElement* root();
    /// The focused element, or null.
    UiElement* focused();
    /// Gives the focus to `element` (null: none; the next tick focuses the first element).
    void focus(UiElement* element);
    /// Hidden: not drawn, not navigated.
    void set_visible(bool visible);
    /// Whether it is shown.
    bool visible() const;
    /// The order among the documents: a higher layer is drawn above and takes the input.
    int layer() const;
    /// A copy of the `<template id="...">` element, one per `key`, kept while the game asks for it
    /// every frame (world labels: one bar per monster), drawn under the document's own elements in
    /// the order they were asked for in the frame (what overlaps follows the game's state).
    /// Its position: UiElement::set_screen_position().
    UiElement* instance(std::string_view template_id, std::uint64_t key);
    /// The problems of its files (syntax, unknown properties): reported in the log too.
    const std::vector<std::string>& problems() const;

    /// Made by the interface for its documents (not by the game).
    explicit UiDocument(void* impl) : impl_(impl) {}

private:
    friend struct UiSystem;
    friend class Ui;
    void* impl_;
};

/// The interface: its documents, their model, their navigation and drawing.
class Ui {
public:
    /// Uses the application's assets, renderer and strings; the default font is
    /// `fonts/interface.font.json` (the `font-family` of a style may name another one).
    explicit Ui(Application& app, std::string default_font = "fonts/interface.font.json");
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    /// Opens a document (its file and its style sheets, reloaded when they change). The same path
    /// opened twice gives the same document. Throws std::runtime_error if the file cannot be read
    /// or its markup is broken (the problems of the styles are only reported).
    UiDocument& open(const std::string& path, int layer = 0);
    /// Closes a document: its elements' handles become invalid.
    void close(UiDocument& document);
    /// Closes the document opened from `path`, if any.
    void close(const std::string& path);
    /// The open document, or null.
    UiDocument* document(const std::string& path);

    /// The data the documents show (one model for all of them).
    UiModel& model();
    /// A texture the game made (a save's thumbnail), shown by `<image src="name">` or a style's
    /// `background: url(name)` before any asset of that name. Null forgets it. The texture must
    /// live while it is set.
    void set_texture(const std::string& name, const Texture* texture);
    /// The size of the interface: the player's choice (75 % to 200 %) times the screen's density.
    void set_scale(float scale);
    /// Pixels per point of the documents.
    float scale() const;

    /// The tick: navigation of the top visible document that takes the input (`modal` documents,
    /// the default; a HUD is `modal="false"`), events queued.
    void tick(const UiInput& input);
    /// The events since the last call.
    std::vector<UiEvent> take_events();
    /// Text typed into a focused `<input>` (SDL text and key events), between ticks.
    void on_event(const SDL_Event& event);

    /// Restyles and lays out what changed, advances the transitions by `dt` seconds (real time),
    /// shows the tooltips, and records the documents into renderer.screen_sprites() at depths from
    /// `base_depth` up.
    void render(Renderer& renderer, double dt, float base_depth = 100.0f);

    /// The problems of the open documents (UiDocument::problems()), and the `{id}` of their texts
    /// that name no message of the current language ("unknown message 'id'"): what a check of the
    /// screens reports.
    std::vector<std::string> problems() const;

    /// Draw statistics of the last render: elements drawn, quads recorded.
    struct Stats {
        int elements = 0;  ///< elements drawn
        int quads = 0;     ///< quads recorded (boxes, images, glyphs)
        int layouts = 0;  ///< documents laid out again in the last render
    };
    /// The statistics of the last render().
    Stats stats() const;

private:
    std::unique_ptr<UiSystem> system_;
};

/// Checks the screens of a game without showing them (the cook, `--check-data`): every `.ui` and
/// `.uss` under the `ui/` folder of each assets root read, their style sheets found, their
/// properties and values known, and the `{id}` of their texts messages of `strings` (when given).
/// The problems, "ui/file.ui:line: what"; none when all is well.
std::vector<std::string> check_ui_files(const std::vector<std::string>& roots, const Localization* strings = nullptr);

}  // namespace moteur
