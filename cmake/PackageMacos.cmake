# The macOS package of a game (cmake -P, run by the target <game>_package; see cmake/Package.cmake).
# Inputs (-D): TARGET NAME VERSION PUBLISHER ICON EXE LICENSES COOK WORK OUTPUT IDENTIFIER
# SIGN_IDENTITY NOTARY_PROFILE MINIMUM_MACOS PLIST.
#
# <NAME>.app/Contents/
#   MacOS/<exe>               the program (its debug information moved into a dSYM, kept in symbols/)
#   Resources/archives/*.pak  the cooked assets and shaders (SDL_GetBasePath() is Resources/ in a bundle)
#   Resources/licenses/       the libraries' license texts
#   Resources/<icon>.icns
#   Info.plist
# Nothing is ever written into the bundle: it is signed, and a changed file breaks the signature.

cmake_minimum_required(VERSION 3.25)

set(_stem "${TARGET}-${VERSION}-macos-arm64")
set(_root "${WORK}/stage")
set(_app "${_root}/${NAME}.app")
get_filename_component(_exe_name "${EXE}" NAME)

function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        string(REPLACE ";" " " _line "${ARGV}")
        message(FATAL_ERROR "package: '${_line}' failed (${_result})")
    endif()
endfunction()

# 1. The bundle.
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_app}/Contents/MacOS" "${_app}/Contents/Resources")
file(GLOB _packs "${COOK}/archives/*.pak")
if(NOT _packs)
    message(FATAL_ERROR "package: no archive in ${COOK}/archives (did the cooking fail?)")
endif()
file(COPY "${EXE}" DESTINATION "${_app}/Contents/MacOS")
file(COPY ${_packs} DESTINATION "${_app}/Contents/Resources/archives")
file(COPY "${LICENSES}/" DESTINATION "${_app}/Contents/Resources/licenses")
set(PLIST_ICON "")
if(ICON)
    get_filename_component(PLIST_ICON "${ICON}.icns" NAME)
    file(COPY "${ICON}.icns" DESTINATION "${_app}/Contents/Resources")
endif()
set(PLIST_NAME "${NAME}")
set(PLIST_EXE "${_exe_name}")
set(PLIST_IDENTIFIER "${IDENTIFIER}")
set(PLIST_VERSION "${VERSION}")
set(PLIST_MINIMUM "${MINIMUM_MACOS}")
if(NOT PLIST_MINIMUM)
    set(PLIST_MINIMUM "13.0")
endif()
string(TIMESTAMP _year "%Y")
set(PLIST_COPYRIGHT "")
if(PUBLISHER)
    set(PLIST_COPYRIGHT "© ${_year} ${PUBLISHER}")
endif()
configure_file("${PLIST}" "${_app}/Contents/Info.plist" @ONLY)
run(plutil -lint "${_app}/Contents/Info.plist")

# 2. The debug symbols apart (dSYM, kept per version with the program), the shipped program without them.
set(_symbols "${OUTPUT}/symbols/${_stem}")
file(REMOVE_RECURSE "${_symbols}")
file(MAKE_DIRECTORY "${_symbols}")
run(dsymutil "${_app}/Contents/MacOS/${_exe_name}" -o "${_symbols}/${_exe_name}.dSYM")
run(strip -S "${_app}/Contents/MacOS/${_exe_name}")
file(COPY "${_app}/Contents/MacOS/${_exe_name}" DESTINATION "${_symbols}")

# 3. Signed from the inside out (no --deep): the program, then the bundle. Hardened runtime, no
#    entitlements (no JIT, no microphone...). '-' signs ad hoc: required on Apple Silicon anyway; a
#    downloaded ad hoc app opens through System Settings > Privacy & Security > Open Anyway.
set(_sign codesign --force --options runtime --sign "${SIGN_IDENTITY}")
if(NOT SIGN_IDENTITY STREQUAL "-")
    list(APPEND _sign --timestamp)
endif()
run(${_sign} "${_app}/Contents/MacOS/${_exe_name}")
run(${_sign} "${_app}")
run(codesign --verify --strict --verbose=2 "${_app}")

# 4. Notarized and stapled when an Apple Developer account is set up (Developer ID + notarytool profile).
if(NOTARY_PROFILE AND NOT SIGN_IDENTITY STREQUAL "-")
    run(ditto -c -k --keepParent "${_app}" "${WORK}/notarize.zip")
    run(xcrun notarytool submit "${WORK}/notarize.zip" --keychain-profile "${NOTARY_PROFILE}" --wait)
    run(xcrun stapler staple "${_app}")
else()
    message(STATUS "package: not notarized (MOTEUR_MACOS_SIGN_IDENTITY / MOTEUR_MACOS_NOTARY_PROFILE): Gatekeeper will ask the players to confirm")
endif()

# 5. The disk image: the app and a link to Applications.
file(CREATE_LINK /Applications "${_root}/Applications" SYMBOLIC)
file(REMOVE "${OUTPUT}/${_stem}.dmg")
run(hdiutil create -volname "${NAME}" -srcfolder "${_root}" -ov -format UDZO "${OUTPUT}/${_stem}.dmg")
if(NOT SIGN_IDENTITY STREQUAL "-")
    run(codesign --force --timestamp --sign "${SIGN_IDENTITY}" "${OUTPUT}/${_stem}.dmg")
    if(NOTARY_PROFILE)
        run(xcrun notarytool submit "${OUTPUT}/${_stem}.dmg" --keychain-profile "${NOTARY_PROFILE}" --wait)
        run(xcrun stapler staple "${OUTPUT}/${_stem}.dmg")
    endif()
endif()
message(STATUS "package: ${OUTPUT}/${_stem}.dmg")
