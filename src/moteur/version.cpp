#include "moteur/version.hpp"

#include <SDL3/SDL.h>

namespace moteur {

namespace build_info {
extern const char* const kCommit;      // build_info.cpp, generated at build time (cmake/BuildInfo.cmake)
extern const char* const kCommitDate;
}  // namespace build_info

const char* version() { return MOTEUR_VERSION; }

const char* build_commit() { return build_info::kCommit; }

const char* build_commit_date() { return build_info::kCommitDate; }

std::string build_description() {
    std::string text = std::string("moteur ") + version() + " (" + build_commit();
    if (*build_commit_date() != '\0') {
        text += std::string(", ") + build_commit_date();
    }
#if defined(MOTEUR_SHIPPING)
    return text + ", Shipping)";
#else
    return text + ", " + MOTEUR_BUILD_TYPE + ")";
#endif
}

std::string sdl_version() {
    const int v = SDL_GetVersion();
    return std::to_string(SDL_VERSIONNUM_MAJOR(v)) + "." +
           std::to_string(SDL_VERSIONNUM_MINOR(v)) + "." +
           std::to_string(SDL_VERSIONNUM_MICRO(v));
}

}  // namespace moteur
