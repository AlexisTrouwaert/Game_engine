#pragma once

#include <string>

/// @namespace moteur
/// The whole public interface of the engine: every class, function and constant a game uses is in
/// this namespace (`#include "moteur/..."`). The engine's macros, which cannot live in a namespace,
/// are prefixed `MOTEUR_` instead (see moteur/assert.hpp).
namespace moteur {

/// The engine's version, `major.minor.patch`, as written in `project(moteur VERSION ...)` in
/// CMakeLists.txt: one minor version per milestone (milestone 8 = 0.8.0). A static string, valid for
/// the whole run.
const char* version();

/// The source the program was built from: the commit's short hash (7 characters), followed by `+`
/// when tracked files of the working tree had uncommitted changes (`7cba197+`), or `inconnu` when
/// the build was made outside a git checkout (or without git). Written at build time by
/// cmake/BuildInfo.cmake; a static string, valid for the whole run.
const char* build_commit();

/// Date of that commit (`YYYY-MM-DD`), or an empty string outside a git checkout. A static string.
const char* build_commit_date();

/// `moteur 0.8.0 (7cba197+, 2026-10-05, Release)`: the line written at the top of the log and in the
/// crash reports. The date is left out when it is unknown; the last field is the build type (Debug,
/// Release...), or `Shipping` in the shipping configuration (MOTEUR_SHIPPING).
std::string build_description();

/// Version of the SDL library linked at runtime (which may differ from the headers the engine was
/// compiled with), formatted as `major.minor.micro`. Callable before SDL is initialized.
std::string sdl_version();

}  // namespace moteur
