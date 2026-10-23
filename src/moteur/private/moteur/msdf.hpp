#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

/// Multi-channel signed distance fields of glyph outlines (milestone 9 bis, part 4), written in the
/// engine after Viktor Chlumský's published method ("Shape Decomposition for Multi-channel Distance
/// Fields", 2015, and the msdfgen notes): edges colored so that each corner is kept by two channels,
/// pseudo-distances per channel, contours that overlap (variable fonts keep them), a scanline pass
/// that settles the inside/outside sign and a pass that removes the texels whose channels would
/// clash when filtered.
///
/// The fourth channel holds the true signed distance (MTSDF): rounded far from the edge, it draws
/// wide outlines and soft shadows without the multi-channel corners.
namespace moteur::msdf {

/// A glyph's outline, in any unit (font units here): closed contours of lines and quadratic
/// curves (cubic curves are approximated by quadratic ones when added). The fill rule is non-zero.
class Outline {
public:
    void move_to(glm::dvec2 p);
    void line_to(glm::dvec2 p);
    void quadratic_to(glm::dvec2 control, glm::dvec2 p);
    /// Split into quadratic curves (4 per cubic: far below a texel at glyph sizes).
    void cubic_to(glm::dvec2 control1, glm::dvec2 control2, glm::dvec2 p);
    /// Closes the current contour (a line back to its start if needed). move_to() closes too.
    void close();

    /// One edge: a line (control == the middle, `quadratic` false) or a quadratic curve.
    struct Segment {
        glm::dvec2 p0, p1, p2;  ///< start, control, end
        bool quadratic = false;
        std::uint8_t color = 7;  ///< which channels it belongs to (bits: red 1, green 2, blue 4)
    };
    using Contour = std::vector<Segment>;

    const std::vector<Contour>& contours() const { return contours_; }
    std::vector<Contour>& contours() { return contours_; }
    bool empty() const;
    /// The bounding box of the control points (contains the curves), {min, max}; zero if empty.
    void bounds(glm::dvec2& min, glm::dvec2& max) const;

private:
    std::vector<Contour> contours_;
    glm::dvec2 start_{0.0};
    glm::dvec2 pen_{0.0};
    bool open_ = false;
};

/// How generate() maps the outline onto the texels.
struct Placement {
    double scale = 1.0;           ///< texels per outline unit
    glm::dvec2 translate{0.0};    ///< added to the outline's coordinates before scaling
    double range = 4.0;           ///< total width of the distance range, in texels (from -range/2 to +range/2)
};

/// Fills `rgba` (width * height * 4 bytes, rows top to bottom: the outline's y axis points up and
/// the first row is its top) with the MTSDF of `outline`: channels 0.5 on the edge, above inside.
/// Texel (x, y) samples the outline point ((x + 0.5) / scale - translate.x, ((height - y - 0.5)) /
/// scale - translate.y). Colors the edges itself (the outline is modified). Pure computation: any
/// thread, one outline per call.
void generate(Outline& outline, const Placement& transform, int width, int height, std::uint8_t* rgba);

/// The edge coloring alone (generate() calls it): corners sharper than `angle` radians (between
/// the tangents) get a color change.
void color_edges(Outline& outline, double angle = 3.0);

}  // namespace moteur::msdf
