# Enables a strict warning level on our own targets (not on third-party code).
# MOTEUR_WARNINGS_AS_ERRORS (off by default, on in tools/ci/verify.py) turns them into errors.
option(MOTEUR_WARNINGS_AS_ERRORS "Treat the warnings of the project's own targets as errors" OFF)

# AddressSanitizer on the project's own targets (milestone 8, part 8; preset windows-asan / macos-asan):
# out-of-bounds accesses, use after free, double free. The libraries of vcpkg are not instrumented:
# the containers' annotations are turned off everywhere, or their mismatch reports false overflows.
option(MOTEUR_ASAN "Build the project's own targets with AddressSanitizer" OFF)
# Fuzzing (tests/fuzz): the project's own code instrumented for libFuzzer's coverage, with ASan.
option(MOTEUR_FUZZ "Build the fuzzing programs (tests/fuzz), with coverage and AddressSanitizer" OFF)
if(MOTEUR_FUZZ)
    set(MOTEUR_ASAN ON)
endif()
if(MOTEUR_ASAN)
    add_compile_definitions(_DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
endif()

function(moteur_enable_warnings target)
    if(MOTEUR_ASAN)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi /bigobj)
            target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG)
            if(MOTEUR_FUZZ)
                target_compile_options(${target} PRIVATE /fsanitize-coverage=inline-8bit-counters
                                       /fsanitize-coverage=edge /fsanitize-coverage=trace-cmp)
            endif()
        else()
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
            if(MOTEUR_FUZZ)
                target_compile_options(${target} PRIVATE -fsanitize=fuzzer-no-link)
            endif()
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(MOTEUR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(MOTEUR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Without this, MSVC reads every source file using the system codepage (not UTF-8), even one that
# is itself saved as UTF-8: a literal accented character breaks, and even a u8"..." literal (whose
# encoding the standard otherwise guarantees) is silently corrupted, because MSVC must first decode
# the file's raw bytes before it can re-encode them as UTF-8. /utf-8 fixes both source and execution
# charset at once. Clang and GCC assume UTF-8 source by default and are unaffected.
if(MSVC)
    add_compile_options(/utf-8)
endif()
