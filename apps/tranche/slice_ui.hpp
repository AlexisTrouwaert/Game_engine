#pragma once

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/font.hpp"
#include "moteur/renderer.hpp"
#include "moteur/sprite_renderer.hpp"

// The slice's own interface (milestone 8, part 12): its menus and panels drawn with the engine's
// text and sprites, as a shipped game draws them. Dear ImGui stays for the debug tools, and is not
// in the shipped program at all.
//
// A panel is laid out from top to bottom, centred on the screen: begin_panel(), then lines,
// buttons, cards and bars, then end_panel(), which draws the panel's background behind them (the
// sprites are sorted by depth: what is recorded first may be drawn last). Sizes are in points,
// multiplied by the screen's density (2 on a Retina screen), so that the interface keeps its size.
class SliceUi {
public:
    // A rectangle of the screen, in pixels: where a button was drawn, for the mouse.
    struct Rect {
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};
        bool contains(glm::vec2 point) const {
            return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
        }
    };
    enum class Tone { Normal, Dim, Error, Accent };

    explicit SliceUi(moteur::Application& app);

    // The whole screen darkened (the pause, over the frozen game), over the panels of lower layers.
    void dim(moteur::Renderer& renderer, float alpha, int layer = 0);

    // `layer`: a panel over another one (the slots over the pause) has a higher layer.
    void begin_panel(moteur::Renderer& renderer, float width_points, int layer = 0);
    void title(std::string_view text);
    void text(std::string_view text, Tone tone = Tone::Normal);
    void space(float points = 10.0f);
    // A button of the panel's width (or `width_points`), highlighted when focused.
    Rect button(std::string_view text, bool focused, float width_points = 0.0f);
    // A picture (a save's thumbnail, or an empty frame without one) with lines beside it, the whole
    // card highlighted when focused.
    struct Line {
        std::string text;
        Tone tone = Tone::Normal;
    };
    Rect card(const moteur::Texture* picture, glm::vec2 picture_points, const std::vector<Line>& lines, bool focused);
    void progress(float value);  // 0 to 1
    void end_panel();

    float scale() const { return scale_; }

private:
    void update_scale();
    glm::vec4 color(Tone tone) const;
    void rectangle(glm::vec2 min, glm::vec2 size, glm::vec4 color, float depth);

    moteur::Application& app_;
    float scale_ = 0.0f;
    moteur::Asset<moteur::Font> font_;
    moteur::Asset<moteur::Font> title_font_;
    moteur::Texture white_;
    // The panel being laid out.
    moteur::Renderer* renderer_ = nullptr;
    glm::vec2 origin_{0.0f};  // top-left corner of the content, pixels
    float width_ = 0.0f;      // of the content, pixels
    float cursor_ = 0.0f;     // pixels below the top of the content
    float depth_ = 0.0f;      // added to every depth: the panel's layer
};

// A column of choices driven by the menu actions (arrows, pad, Enter) or the mouse: the focused one
// is highlighted. Everything is read in the tick, the mouse included (the pointer is part of the
// input frames, recorded and replayed): a click on a button chooses it; the pointer moving over
// the buttons does not move the focus (a replay would then depend on where the mouse rests).
class SliceMenu {
public:
    explicit SliceMenu(std::vector<std::string> items) : items_(std::move(items)) {}
    void set_items(std::vector<std::string> items);
    const std::string& item(int index) const { return items_[static_cast<std::size_t>(index)]; }
    std::size_t size() const { return items_.size(); }
    int focus() const { return focus_; }

    // The tick: the item chosen (confirmed or clicked), or -1. `moved` and `chosen` play the sounds.
    int update(moteur::Application& app, bool& moved, bool& chosen);
    // The buttons, where the panel is at; their rectangles are kept for the clicks of the next ticks.
    void draw(SliceUi& ui, float width_points = 0.0f);
    // Where item `index` was drawn (a card drawn by the caller, for instance).
    void set_rect(int index, const SliceUi::Rect& rect);
    // A long list: only `rows` items are drawn, the window following the focus (0: all of them).
    void set_visible_rows(int rows) { visible_rows_ = rows; }

private:
    std::vector<std::string> items_;
    std::vector<SliceUi::Rect> rects_;
    int focus_ = 0;
    int visible_rows_ = 0;
    int first_ = 0;  // the first item drawn
};

// The slice's menu actions, with their default bindings (see assets/input/demo3d.json).
inline constexpr const char* kMenuActions[] = {"menu_up", "menu_down", "menu_left", "menu_right", "menu_confirm", "menu_click"};
