#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/bidi.hpp"
#include "moteur/sprite_region.hpp"

namespace moteur {

class Renderer;
class SpriteRenderer;
class FontLibrary;

/// Horizontal alignment of each line inside the text's box (as wide as TextStyle::max_width, or
/// as the widest line if that is wider or if wrapping is off).
enum class TextAlign : std::uint8_t {
    Start,   ///< the start of the paragraph's direction: left in French, right in Arabic
    End,     ///< the other side
    Left,
    Center,
    Right,
};

/// A shadow under a text: a copy of its shape (outline included), moved and blurred.
struct TextShadow {
    glm::vec2 offset{2.0f, 2.0f};  ///< pixels (y down)
    float softness = 2.0f;         ///< blur width, pixels (at most ~5 at 16 px: the field's range)
    glm::vec4 color{0.0f, 0.0f, 0.0f, 0.6f};  ///< straight alpha
};

/// How a text looks and is laid out. Colors are straight-alpha, components in [0, 1].
struct TextStyle {
    float size = 16.0f;  ///< the em size in pixels (CSS's font-size); the line height follows the font
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};  ///< the fill (a span's markup color replaces it)
    /// 100 (thin) to 900 (black), 400 regular, 700 bold: a variable font's weight axis, else a
    /// dilation of the glyphs (synthetic bold) from 550.
    float weight = 400.0f;
    bool italic = false;              ///< slanted glyphs (synthetic: the fonts have no italic face yet)
    float outline_width = 0.0f;       ///< pixels around the glyphs (0: none); at most ~6 at 32 px
    glm::vec4 outline_color{0.0f, 0.0f, 0.0f, 1.0f};  ///< used when outline_width > 0
    std::optional<TextShadow> shadow;  ///< none: no shadow
    TextAlign align = TextAlign::Start;  ///< of each line in the box
    /// Wrapping width, in pixels; <= 0: lines end only at newlines. When > 0 the box is at least
    /// this wide. A word longer than the width is not cut (it overflows).
    float max_width = 0.0f;
    float line_spacing = 1.0f;  ///< multiplies the font's line height
    /// The direction of each paragraph: Auto follows its first strong character (an Arabic
    /// sentence reads right to left); the interface of a right-to-left language forces it.
    TextDirection direction = TextDirection::Auto;
    /// BCP 47 language ("fr", "ar", "sr"): the language forms of the fonts (OpenType locl); empty:
    /// none in particular.
    std::string language;
    /// Inline tags, read off the text (parse_text_markup()): `<b>`, `<i>`, `<color=#rrggbb[aa]>`,
    /// `<size=1.5>`, `<weight=600>`, `<icon=name>`; `&lt;` `&gt;` `&amp;` for the characters.
    bool markup = false;
    float depth = 0.0f;  ///< SpriteOptions::depth of every glyph: larger is drawn later, so on top
    /// Drawn only inside this rectangle (x0, y0, x1, y1, pixels), when set (not a layout field).
    std::optional<glm::vec4> clip;
};

/// A span of text with its own look, from the markup (TextStyle::markup).
struct TextSpan {
    std::optional<glm::vec4> color;  ///< none: the style's color
    float scale = 1.0f;              ///< size relative to the style's
    std::optional<float> weight;     ///< none: the style's weight
    bool italic = false;             ///< in addition to the style's
    std::string icon;                ///< non-empty: this span is one inline icon (U+FFFC in the text)
    bool operator==(const TextSpan&) const = default;  ///< same look
};

/// The text without its tags, and for each character the span it belongs to.
struct TextMarkup {
    std::u32string text;                 ///< the characters, tags removed, entities decoded
    std::vector<std::uint16_t> span_of;  ///< per character of `text`
    std::vector<TextSpan> spans;         ///< spans[0] is the plain style
};

/// Reads the inline tags of TextStyle::markup. Never fails: an unknown or malformed tag is kept as
/// text (so that a typo shows), a closing tag without its opening one is dropped.
TextMarkup parse_text_markup(std::string_view utf8);

/// One glyph of a laid-out text.
struct TextGlyph {
    std::uint32_t face = 0;   ///< which font of the stack (index in its library)
    std::uint32_t glyph = 0;  ///< glyph id in that font
    std::int32_t weight = 0;  ///< the font instance (0 for a font without weight axis)
    std::uint16_t span = 0;   ///< TextLayout::spans index
    float size = 0;           ///< em size, pixels (style size times the span's scale)
    float dilate = 0;         ///< synthetic bold, pixels
    bool italic = false;      ///< drawn slanted
    glm::vec2 position{0.0f};  ///< the pen on the baseline, pixels from the box's top left
    std::uint32_t cluster = 0;  ///< the first character it comes from (index in TextLayout::text)
};

/// One line of a laid-out text.
struct TextLine {
    std::uint32_t first_glyph = 0;  ///< its glyphs in TextLayout::glyphs, left to right: the first one
    std::uint32_t glyph_count = 0;  ///< and how many
    std::uint32_t first_char = 0;   ///< its characters [first_char, end_char) of TextLayout::text
    std::uint32_t end_char = 0;     ///< (the trailing spaces and the newline included)
    float top = 0;       ///< pixels from the box's top
    float baseline = 0;  ///< pixels from the box's top
    float height = 0;    ///< pixels (line spacing included)
    float width = 0;     ///< ink advance, trailing spaces left out
    float x = 0;         ///< alignment offset of its left end
    bool rtl = false;    ///< the paragraph reads right to left
};

/// An inline icon placed in a text.
struct TextIcon {
    std::string name;          ///< the `<icon=name>` of the markup
    glm::vec2 position{0.0f};  ///< top left, pixels from the box's top left
    glm::vec2 size{0.0f};      ///< pixels (an em square)
    std::uint16_t span = 0;    ///< TextLayout::spans index (its color tints the icon)
    std::uint32_t line = 0;  ///< TextLayout::lines index
};

/// A text shaped and laid out: the result of Font::layout(), drawn by Font::draw().
struct TextLayout {
    glm::vec2 size{0.0f};  ///< the box
    std::u32string text;   ///< the text as laid out (tags removed)
    std::vector<TextLine> lines;  ///< at least one
    std::vector<TextGlyph> glyphs;  ///< line by line, each line left to right
    std::vector<TextSpan> spans;    ///< spans[0]: the plain style
    std::vector<TextIcon> icons;    ///< the inline icons
};

/// Where a text cursor before character `index` of `layout.text` stands: x, the top and the bottom
/// of its line, pixels from the box's top left (the end of the text for an index past it). Right to
/// left text puts it on the right of its character.
glm::vec3 text_caret(const TextLayout& layout, std::size_t index);
/// The character index (0 to text.size()) nearest to `point` (pixels from the box's top left): a
/// click in a text field.
std::size_t text_hit(const TextLayout& layout, glm::vec2 point);

/// Draws the inline icons of a text (TextStyle::markup's <icon=name>): the region for a name, or
/// null for an unknown one (a box is left empty).
using TextIconLookup = std::function<const SpriteRegion*(std::string_view name)>;

/// A font stack (milestone 9 bis, part 4): a main font and fallback fonts, tried in turn for each
/// character (a Chinese character missing from Inter is found in Noto Sans SC). Text is shaped by
/// HarfBuzz (kerning, ligatures, Arabic joining, Indic reordering), laid out by the engine
/// (Unicode line breaking, bidirectional text) and drawn with multi-channel distance fields: sharp
/// at any size, with outline and shadow.
///
/// Size is not part of the font: each draw says it (TextStyle::size), and every size shares the
/// same glyphs. Fonts loaded through Assets share one FontLibrary (the fonts' files and the glyph
/// atlas).
///
/// Drawing records sprites (SpriteRenderer, usually Renderer::screen_sprites() for the interface)
/// and may make missing glyphs: from the thread that runs the frame. layout() and measure() may run
/// on any thread.
class Font {
public:
    /// Loads `path`: a font file (.ttf, .otf) alone, or a font stack (.font.json):
    ///
    /// ```json
    /// { "faces": ["Inter-Regular.ttf", "noto/NotoSansArabic.ttf", "noto/NotoSansSC.ttf"],
    ///   "prepare": "latin" }
    /// ```
    ///
    /// Faces are relative to the stack's folder; the first is required, a missing fallback is
    /// skipped with a warning (a package that leaves the Chinese font out). "prepare" ("latin",
    /// the default, or "none"): the glyphs made at once, over the job system's threads (ASCII,
    /// Latin-1, the French ligatures and punctuation), so that the first frames do not make them.
    /// `library` (Assets passes its own) shares fonts and atlas; null: a library of its own.
    /// `renderer` null: no GPU (tests, tools; the atlas stays in memory). Throws std::runtime_error
    /// naming the file if a required file cannot be read or parsed.
    static Font load(Renderer* renderer, const std::string& path, std::shared_ptr<FontLibrary> library = nullptr);

    /// A library for several fonts (Assets keeps one). `renderer` null: no GPU.
    static std::shared_ptr<FontLibrary> make_library(Renderer* renderer);

    ~Font();
    /// Movable: the library and its atlas are shared, nothing moves on the GPU.
    ///@{
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    ///@}
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    /// The text shaped and laid out with `style`. Cached: the same text and style return the same
    /// layout without work (the interface draws the same texts every frame). `utf8_text` is
    /// decoded with decode_utf8() (invalid bytes become U+FFFD). Thread-safe.
    std::shared_ptr<const TextLayout> layout(std::string_view utf8_text, const TextStyle& style) const;

    /// The size of the box layout() gives.
    glm::vec2 measure(std::string_view utf8_text, const TextStyle& style) const;

    /// Records the text's glyphs, `anchor` being the top-left corner of the box. Glyphs not made
    /// yet are made within the frame's budget (see GlyphAtlas); the others show from a later frame.
    void draw(SpriteRenderer& sprites, std::string_view utf8_text, glm::vec2 anchor, const TextStyle& style) const;
    /// Records an already laid-out text with `style`'s colors, outline, shadow and depth (its
    /// layout fields are those of `layout`).
    void draw(SpriteRenderer& sprites, const TextLayout& layout, glm::vec2 anchor, const TextStyle& style) const;

    /// Distance between the tops of two lines at `size` (the main font's ascent, descent and gap).
    float line_height(float size) const;
    /// Distance from the top of a line to its baseline at `size`.
    float ascent(float size) const;

    /// Makes now the glyphs of these characters at this weight (a loading screen that knows its
    /// texts), over the job system's threads. Main thread.
    void prepare(std::u32string_view characters, float weight = 400.0f) const;

    /// The icons of TextStyle::markup (none by default: icons are left as empty boxes).
    void set_icons(TextIconLookup icons) { icons_ = std::move(icons); }

    /// GPU memory of the atlas the font draws with (shared by every font of the library).
    std::size_t gpu_bytes() const;
    /// The files of the stack (resolved paths), the main font first.
    const std::vector<std::string>& files() const { return files_; }
    /// The library the font draws with (its faces and atlas; tests and tools).
    FontLibrary& library() const { return *library_; }

private:
    Font() = default;

    std::shared_ptr<FontLibrary> library_;
    std::vector<std::uint32_t> faces_;  ///< the stack, as library indices
    std::vector<std::string> files_;
    TextIconLookup icons_;
};

}  // namespace moteur
