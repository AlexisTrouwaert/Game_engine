# The engine's API reference (milestone 8, part 9): `cmake --build <dir> --target moteur_docs` writes
# it into <dir>/docs/html (index.html). Doxygen reads the public headers' /// comments; any warning
# (a public declaration without documentation, a wrong command) fails the target.
# Doxygen is a build tool, not a library of the engine: when it is missing, the target is not made.
find_package(Doxygen 1.9 QUIET)
if(NOT DOXYGEN_FOUND)
    message(STATUS "Doxygen not found: no moteur_docs target (the API reference)")
    return()
endif()

set(MOTEUR_DOCS_OUTPUT "${CMAKE_BINARY_DIR}/docs")
set(MOTEUR_DOCS_WARN_AS_ERROR "FAIL_ON_WARNINGS")
configure_file("${MOTEUR_ROOT}/tools/docs/Doxyfile.in" "${MOTEUR_DOCS_OUTPUT}/Doxyfile" @ONLY)
file(GLOB _moteur_public_headers CONFIGURE_DEPENDS "${MOTEUR_ROOT}/src/moteur/include/moteur/*.hpp")
add_custom_command(
    OUTPUT "${MOTEUR_DOCS_OUTPUT}/html/index.html"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${MOTEUR_DOCS_OUTPUT}"
    COMMAND Doxygen::doxygen "${MOTEUR_DOCS_OUTPUT}/Doxyfile"
    DEPENDS "${MOTEUR_DOCS_OUTPUT}/Doxyfile" "${MOTEUR_ROOT}/tools/docs/mainpage.md" ${_moteur_public_headers}
    COMMENT "API reference (Doxygen)"
    VERBATIM)
add_custom_target(moteur_docs DEPENDS "${MOTEUR_DOCS_OUTPUT}/html/index.html")
