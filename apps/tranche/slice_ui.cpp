#include "slice_ui.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kBodyPoints = 20.0f;
constexpr float kTitlePoints = 30.0f;
constexpr float kPadding = 22.0f;     // around the panel's content, points
constexpr float kButtonHeight = 34.0f;

// Depths of the interface: above the game's own overlay (bars, names, help: up to 2).
constexpr float kDimDepth = 20.0f;
constexpr float kPanelDepth = 21.0f;
constexpr float kHighlightDepth = 22.0f;
constexpr float kContentDepth = 23.0f;

}  // namespace

SliceUi::SliceUi(moteur::Application& app) : app_(app) {
    update_scale();
    moteur::Image white;
    white.width = 1;
    white.height = 1;
    white.pixels = {255, 255, 255, 255};
    white_ = app.renderer().create_texture(white, "interface.white");
}

glm::vec4 SliceUi::color(Tone tone) const {
    switch (tone) {
        case Tone::Dim:
            return {0.62f, 0.65f, 0.72f, 1.0f};
        case Tone::Error:
            return {1.0f, 0.45f, 0.4f, 1.0f};
        case Tone::Accent:
            return {1.0f, 0.85f, 0.35f, 1.0f};
        case Tone::Normal:
        default:
            return {0.93f, 0.93f, 0.96f, 1.0f};
    }
}

void SliceUi::rectangle(glm::vec2 min, glm::vec2 size, glm::vec4 tint, float depth) {
    moteur::SpriteOptions options;
    options.tint = tint;
    options.depth = depth + depth_;
    renderer_->screen_sprites().draw(white_, glm::round(min), glm::round(size), options);
}

void SliceUi::dim(moteur::Renderer& renderer, float alpha, int layer) {
    moteur::SpriteOptions options;
    options.tint = {0.0f, 0.0f, 0.0f, alpha};
    options.depth = kDimDepth + 5.0f * static_cast<float>(layer);  // over the panels of the layers below
    renderer.screen_sprites().draw(white_, {0.0f, 0.0f},
                                   {static_cast<float>(renderer.width()), static_cast<float>(renderer.height())}, options);
}

void SliceUi::update_scale() {
    // The screen's density: the window may have moved to a screen of another one (Retina to a
    // standard screen): the interface keeps its size, its text stays sharp (fonts at the new size).
    float scale = app_.to_pixels({1.0f, 0.0f}).x - app_.to_pixels({0.0f, 0.0f}).x;
    if (!(scale > 0.0f)) {
        scale = 1.0f;
    }
    if (scale != scale_ || !font_) {
        scale_ = scale;
        font_ = app_.assets().font("fonts/Inter-Regular.ttf", std::round(kBodyPoints * scale_));
        title_font_ = app_.assets().font("fonts/Inter-Regular.ttf", std::round(kTitlePoints * scale_));
    }
}

void SliceUi::begin_panel(moteur::Renderer& renderer, float width_points, int layer) {
    update_scale();
    renderer_ = &renderer;
    depth_ = 5.0f * static_cast<float>(layer);
    width_ = width_points * scale_;
    // Centred horizontally; vertically, a third from the top (end_panel() knows the height only).
    origin_ = {std::round((static_cast<float>(renderer.width()) - width_) * 0.5f),
               std::round(static_cast<float>(renderer.height()) * 0.28f)};
    cursor_ = 0.0f;
}

void SliceUi::title(std::string_view text) {
    moteur::TextOptions options;
    options.color = color(Tone::Normal);
    options.depth = kContentDepth + depth_;
    title_font_->draw(renderer_->screen_sprites(), text, origin_ + glm::vec2(0.0f, cursor_), options);
    cursor_ += title_font_->line_height() + 6.0f * scale_;
}

void SliceUi::text(std::string_view text, Tone tone) {
    moteur::TextOptions options;
    options.color = color(tone);
    options.depth = kContentDepth + depth_;
    options.max_width = width_;
    font_->draw(renderer_->screen_sprites(), text, origin_ + glm::vec2(0.0f, cursor_), options);
    cursor_ += font_->measure(text, options).y + 2.0f * scale_;
}

void SliceUi::space(float points) {
    cursor_ += points * scale_;
}

SliceUi::Rect SliceUi::button(std::string_view text, bool focused, float width_points) {
    const float width = width_points > 0.0f ? width_points * scale_ : width_;
    const float height = kButtonHeight * scale_;
    const glm::vec2 min = origin_ + glm::vec2(0.0f, cursor_);
    rectangle(min, {width, height}, focused ? glm::vec4(0.95f, 0.75f, 0.3f, 0.95f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.08f),
              kHighlightDepth);
    moteur::TextOptions options;
    options.color = focused ? glm::vec4(0.08f, 0.06f, 0.04f, 1.0f) : color(Tone::Normal);
    options.depth = kContentDepth + depth_;
    options.align = moteur::TextAlign::Center;
    options.max_width = width;
    const float text_top = std::round((height - font_->line_height()) * 0.5f);
    font_->draw(renderer_->screen_sprites(), text, min + glm::vec2(0.0f, text_top), options);
    cursor_ += height + 6.0f * scale_;
    return {min, min + glm::vec2(width, height)};
}

SliceUi::Rect SliceUi::card(const moteur::Texture* picture, glm::vec2 picture_points, const std::vector<Line>& lines,
                            bool focused) {
    const glm::vec2 picture_size = picture_points * scale_;
    const float gap = 12.0f * scale_;
    float text_height = 0.0f;
    for (const Line& line : lines) {
        (void)line;
        text_height += font_->line_height();
    }
    const float height = std::max(picture_size.y, text_height) + 2.0f * gap * 0.5f;
    const glm::vec2 min = origin_ + glm::vec2(0.0f, cursor_);
    rectangle(min, {width_, height}, focused ? glm::vec4(0.95f, 0.75f, 0.3f, 0.35f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.06f),
              kHighlightDepth);
    const glm::vec2 picture_at = min + glm::vec2(gap * 0.5f, (height - picture_size.y) * 0.5f);
    if (picture != nullptr && picture->gpu) {
        moteur::SpriteOptions options;
        options.depth = kContentDepth + depth_;
        renderer_->screen_sprites().draw(*picture, glm::round(picture_at), glm::round(picture_size), options);
    } else {
        rectangle(picture_at, picture_size, {0.0f, 0.0f, 0.0f, 0.5f}, kContentDepth);
    }
    glm::vec2 at = min + glm::vec2(gap * 0.5f + picture_size.x + gap, (height - text_height) * 0.5f);
    for (const Line& line : lines) {
        moteur::TextOptions options;
        options.color = color(line.tone);
        options.depth = kContentDepth + depth_;
        font_->draw(renderer_->screen_sprites(), line.text, glm::round(at), options);
        at.y += font_->line_height();
    }
    cursor_ += height + 6.0f * scale_;
    return {min, min + glm::vec2(width_, height)};
}

void SliceUi::progress(float value) {
    const float height = 10.0f * scale_;
    const glm::vec2 min = origin_ + glm::vec2(0.0f, cursor_);
    rectangle(min, {width_, height}, {1.0f, 1.0f, 1.0f, 0.12f}, kHighlightDepth);
    rectangle(min, {std::round(width_ * std::clamp(value, 0.0f, 1.0f)), height}, {0.95f, 0.75f, 0.3f, 1.0f}, kContentDepth);
    cursor_ += height + 8.0f * scale_;
}

void SliceUi::end_panel() {
    const float padding = kPadding * scale_;
    rectangle(origin_ - glm::vec2(padding), {width_ + 2.0f * padding, cursor_ + 2.0f * padding},
              {0.07f, 0.07f, 0.1f, 1.0f}, kPanelDepth);
    renderer_ = nullptr;
}

// --- SliceMenu

void SliceMenu::set_items(std::vector<std::string> items) {
    items_ = std::move(items);
    rects_.clear();
    focus_ = std::clamp(focus_, 0, std::max(0, static_cast<int>(items_.size()) - 1));
}

void SliceMenu::set_rect(int index, const SliceUi::Rect& rect) {
    if (rects_.size() < items_.size()) {
        rects_.resize(items_.size());
    }
    rects_[static_cast<std::size_t>(index)] = rect;
}

int SliceMenu::update(moteur::Application& app, bool& moved, bool& chosen) {
    moved = false;
    chosen = false;
    const moteur::Input& input = app.input();
    const auto pressed = [&input](const char* name) {
        const moteur::ActionId action = input.find(name);
        return action != moteur::kNoAction && input.pressed(action);
    };
    const int count = static_cast<int>(items_.size());
    if (count == 0) {
        return -1;
    }
    if (pressed("menu_up")) {
        focus_ = (focus_ + count - 1) % count;
        moved = true;
    }
    if (pressed("menu_down")) {
        focus_ = (focus_ + 1) % count;
        moved = true;
    }
    if (pressed("menu_confirm")) {
        chosen = true;
        return focus_;
    }
    if (pressed("menu_click")) {
        for (int i = 0; i < static_cast<int>(rects_.size()) && i < count; ++i) {
            if (rects_[static_cast<std::size_t>(i)].contains(input.pointer())) {
                focus_ = i;
                chosen = true;
                return i;
            }
        }
    }
    return -1;
}

void SliceMenu::draw(SliceUi& ui, float width_points) {
    const int count = static_cast<int>(items_.size());
    rects_.assign(items_.size(), SliceUi::Rect{});  // the items not drawn cannot be clicked
    int last = count;
    if (visible_rows_ > 0 && count > visible_rows_) {
        first_ = std::clamp(first_, std::max(0, focus_ - visible_rows_ + 1), focus_);
        first_ = std::clamp(first_, 0, count - visible_rows_);
        last = first_ + visible_rows_;
        ui.text(first_ > 0 ? "(suite plus haut)" : " ", SliceUi::Tone::Dim);
    } else {
        first_ = 0;
    }
    for (int i = first_; i < last; ++i) {
        rects_[static_cast<std::size_t>(i)] = ui.button(items_[static_cast<std::size_t>(i)], i == focus_, width_points);
    }
    if (visible_rows_ > 0 && count > visible_rows_) {
        ui.text(last < count ? "(suite plus bas)" : " ", SliceUi::Tone::Dim);
    }
}
