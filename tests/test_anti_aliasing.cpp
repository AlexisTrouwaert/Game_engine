#include <doctest/doctest.h>

#include <string>

#include "moteur/renderer.hpp"

TEST_CASE("AntiAliasing: every mode is read back from its command line name") {
    for (const moteur::AntiAliasing mode : {moteur::AntiAliasing::None, moteur::AntiAliasing::Fxaa,
                                            moteur::AntiAliasing::Msaa2, moteur::AntiAliasing::Msaa4}) {
        moteur::AntiAliasing parsed = moteur::AntiAliasing::None;
        REQUIRE(moteur::parse_anti_aliasing(moteur::anti_aliasing_name(mode), parsed));
        CHECK(parsed == mode);
    }
}

TEST_CASE("AntiAliasing: an unknown name is refused and leaves the mode alone") {
    moteur::AntiAliasing mode = moteur::AntiAliasing::Fxaa;
    CHECK_FALSE(moteur::parse_anti_aliasing("msaa8", mode));
    CHECK_FALSE(moteur::parse_anti_aliasing("", mode));
    CHECK(mode == moteur::AntiAliasing::Fxaa);
}
