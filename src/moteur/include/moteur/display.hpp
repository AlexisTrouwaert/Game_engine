#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

#include "moteur/renderer.hpp"

namespace moteur {

/// The window modes the player can choose (see DisplaySettings::mode): `Windowed`, or `Fullscreen`
/// windowed (borderless, at the screen's resolution).
///
/// Exclusive fullscreen is not offered: with the present models of Windows 10 / 11 a borderless
/// window has the same latency, without the black flash when switching windows, and macOS has no
/// such mode (its fullscreen is a space of its own). The cost of the 3D is set by the render scale.
enum class WindowMode { Windowed, Fullscreen };
/// The shadow qualities the player can choose (see DisplaySettings::shadows): each caps the
/// resolution and the number of the shadow maps (see shadow_caps()). `High` keeps what the game asks for.
enum class ShadowQuality { Low, Medium, High };

/// The player's display settings (milestone 8, part 8): applied by Application::apply_display(),
/// kept in display.json in the player's preferences (ApplicationConfig::display_settings). The file
/// and the French keys and values it had until 2026-10-17 (affichage.json: ecran, synchronisation...)
/// are still read.
struct DisplaySettings {
    /// The `version` written into display.json.
    static constexpr int kVersion = 1;

    WindowMode mode = WindowMode::Windowed;  ///< windowed, or fullscreen windowed
    int display = 0;              ///< index in displays(): 0 is the main screen
    bool vsync = true;            ///< waits for the screen's refresh to show each frame (no tearing)
    int frame_limit = 0;          ///< without vsync: at most this many frames per second (0: none)
    float render_scale = 1.0f;    ///< the 3D's resolution, 0.5 to 1 (the interface stays sharp)
    AntiAliasing anti_aliasing = AntiAliasing::Fxaa;  ///< the anti-aliasing of the 3D (see Renderer::set_anti_aliasing())
    ShadowQuality shadows = ShadowQuality::High;      ///< see shadow_caps()

    /// Every field equal (an options menu compares the edited settings with those in force).
    bool operator==(const DisplaySettings&) const = default;
    /// What needs the player's confirmation (the screen may show nothing usable): the mode, the screen.
    bool changes_screen(const DisplaySettings& other) const { return mode != other.mode || display != other.display; }

    /// The settings as display.json holds them: a JSON object with `version`, `mode`, `display`,
    /// `vsync`, `frame_limit`, `render_scale` (rounded to the hundredth), `anti_aliasing` and
    /// `shadows`, the enumerations by their technical names; indented, ending with a line end.
    std::string to_json() const;
    /// The settings read from the text of display.json. Never throws: a missing key keeps its value
    /// in `defaults` (the Application gives ApplicationConfig::vsync as the default vsync); unknown
    /// values and values of the wrong JSON type (`"vsync": 1`) keep it too, and `warnings` says
    /// which; numbers out of range are clamped (`frame_limit` to [0, 1000], `render_scale` to
    /// [0.5, 1], `display` to 0 or more; Application::apply_display() brings it within the screens
    /// connected). A text that is not a JSON object, or a `version` other than kVersion (a file
    /// written by a newer game), gives `defaults` with a warning; a file without `version` is read
    /// as version 1.
    static DisplaySettings from_json(const std::string& text, std::vector<std::string>& warnings,
                                     const DisplaySettings& defaults);
    /// from_json() with the engine's defaults (a DisplaySettings as constructed).
    static DisplaySettings from_json(const std::string& text, std::vector<std::string>& warnings) {
        return from_json(text, warnings, DisplaySettings());
    }
};

/// Converts a position expressed in window points (what SDL reports for the mouse) into pixels
/// (what the renderer draws in). The two differ on high-density screens, where one point is
/// several pixels. Returns the point unchanged if a size is zero.
glm::vec2 window_to_pixels(glm::vec2 point, glm::vec2 window_size_points, glm::vec2 window_size_pixels);

/// The screens connected now: their names as the system gives them, in SDL's order (the main one
/// first); "Display N" (from 1) when the system gives none. Empty before SDL's video is initialized.
/// Main thread.
std::vector<std::string> displays();

/// The caps a shadow quality puts on what the game asks for (MeshRenderer::set_shadow_caps(), which
/// Application::apply_display() calls).
struct ShadowCaps {
    int resolution;        ///< of the sun's shadow map, in texels per side
    int point_budget;      ///< point lights with a shadow
    int point_resolution;  ///< per face of a point light's cube map, in texels per side
};
/// The caps of a quality: Low 1024 / 1 / 256, Medium 2048 / 2 / 512, High 8192 / 8 / 2048 (in the
/// order of ShadowCaps' fields; High caps nothing a game asks for in practice).
ShadowCaps shadow_caps(ShadowQuality quality);

/// Technical names, for the log, the reports and display.json (`windowed`, `fullscreen`; `low`,
/// `medium`, `high`). The engine shows no text to the player: the game words and translates its
/// options menu itself. Static strings.
///@{
const char* window_mode_name(WindowMode mode);
const char* shadow_quality_name(ShadowQuality quality);
///@}

}  // namespace moteur
