#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "moteur/display.hpp"

TEST_CASE("window points convert to pixels on a high-density screen") {
    // A 640x360 point window drawn at 1280x720 pixels: one point is two pixels.
    CHECK(moteur::window_to_pixels({100.0f, 50.0f}, {640.0f, 360.0f}, {1280.0f, 720.0f}) == glm::vec2(200.0f, 100.0f));
    // Same size: nothing changes.
    CHECK(moteur::window_to_pixels({100.0f, 50.0f}, {1280.0f, 720.0f}, {1280.0f, 720.0f}) == glm::vec2(100.0f, 50.0f));
    // A degenerate window does not divide by zero.
    CHECK(moteur::window_to_pixels({100.0f, 50.0f}, {0.0f, 0.0f}, {1280.0f, 720.0f}) == glm::vec2(100.0f, 50.0f));
}

TEST_CASE("display settings are written and read back as they are") {
    moteur::DisplaySettings settings;
    settings.mode = moteur::WindowMode::Fullscreen;
    settings.display = 1;
    settings.vsync = false;
    settings.frame_limit = 144;
    settings.render_scale = 0.75f;
    settings.anti_aliasing = moteur::AntiAliasing::Msaa4;
    settings.shadows = moteur::ShadowQuality::Low;
    std::vector<std::string> warnings;
    CHECK(moteur::DisplaySettings::from_json(settings.to_json(), warnings) == settings);
    CHECK(warnings.empty());
}

TEST_CASE("bad display settings keep their defaults and say why") {
    std::vector<std::string> warnings;
    CHECK(moteur::DisplaySettings::from_json("{", warnings) == moteur::DisplaySettings{});
    CHECK(warnings.size() == 1);

    warnings.clear();
    const moteur::DisplaySettings read = moteur::DisplaySettings::from_json(
        R"({"version": 1, "anti_aliasing": "taa", "render_scale": 4.0, "frame_limit": -5, "display": -2})", warnings);
    CHECK(read.anti_aliasing == moteur::DisplaySettings{}.anti_aliasing);  // unknown: the default
    CHECK(read.render_scale == 1.0f);                                      // clamped to 50 to 100 %
    CHECK(read.frame_limit == 0);
    CHECK(read.display == 0);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("taa") != std::string::npos);
}

TEST_CASE("the shadow qualities cap the shadows more and more") {
    const moteur::ShadowCaps low = moteur::shadow_caps(moteur::ShadowQuality::Low);
    const moteur::ShadowCaps medium = moteur::shadow_caps(moteur::ShadowQuality::Medium);
    const moteur::ShadowCaps high = moteur::shadow_caps(moteur::ShadowQuality::High);
    CHECK(low.resolution < medium.resolution);
    CHECK(medium.resolution < high.resolution);
    CHECK(low.point_budget < medium.point_budget);
    CHECK(low.point_resolution < medium.point_resolution);
}

TEST_CASE("display settings are written in English; the French file of before is still read") {
    moteur::DisplaySettings settings;
    settings.render_scale = 0.7f;
    const std::string text = settings.to_json();
    for (const char* key : {"\"display\"", "\"vsync\"", "\"frame_limit\"", "\"render_scale\": 0.7", "\"anti_aliasing\"",
                            "\"shadows\": \"high\"", "\"mode\": \"windowed\""}) {
        CAPTURE(key);
        CHECK(text.find(key) != std::string::npos);
    }

    std::vector<std::string> warnings;
    const moteur::DisplaySettings old = moteur::DisplaySettings::from_json(
        R"({"version": 1, "mode": "plein_ecran", "ecran": 1, "synchronisation": false, "limite_images": 30,
            "resolution_rendu": 0.6, "anticrenelage": "msaa4", "ombres": "basses"})", warnings);
    CHECK(warnings.empty());
    CHECK(old.mode == moteur::WindowMode::Fullscreen);
    CHECK(old.display == 1);
    CHECK_FALSE(old.vsync);
    CHECK(old.frame_limit == 30);
    CHECK(old.render_scale == doctest::Approx(0.6f));
    CHECK(old.anti_aliasing == moteur::AntiAliasing::Msaa4);
    CHECK(old.shadows == moteur::ShadowQuality::Low);
}
