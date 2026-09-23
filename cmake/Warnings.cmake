# Enables a strict warning level on our own targets (not on third-party code).
function(moteur_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
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
