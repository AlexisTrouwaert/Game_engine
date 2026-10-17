// Fuzzing of the engine's file readers (milestone 8, part 8), with libFuzzer (MSVC or Clang:
// -fsanitize=fuzzer,address). One program per reader, chosen by MOTEUR_FUZZ_TARGET. A reader may
// refuse a file (an exception, an error): that is its job. A crash, a hang or an AddressSanitizer
// report is a bug; libFuzzer keeps the file that caused it (crash-..., timeout-...), which then
// becomes a case of tests/test_robustness.cpp.
//
//   fuzz_map CORPUS -max_total_time=600     (see tools/fuzz/fuzzer.py)

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/animation_set.hpp"
#include "moteur/archive.hpp"
#include "moteur/data_table.hpp"
#include "moteur/file_io.hpp"
#include "moteur/image.hpp"
#include "moteur/input.hpp"
#include "moteur/ktx_texture.hpp"
#include "moteur/map_data.hpp"
#include "moteur/model.hpp"
#include "moteur/particles.hpp"
#include "moteur/save_file.hpp"

namespace {

[[maybe_unused]] std::string_view text_of(const std::uint8_t* data, std::size_t size) {
    return {reinterpret_cast<const char*>(data), size};
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
#if MOTEUR_FUZZ_TARGET == 1
        (void)moteur::MapData::parse(text_of(data, size), "fuzz.json");
#elif MOTEUR_FUZZ_TARGET == 2
        (void)moteur::ParticleEffect::parse(text_of(data, size), "fuzz.json");
#elif MOTEUR_FUZZ_TARGET == 3
        (void)moteur::AnimationSet::parse(text_of(data, size), "fuzz.json");
#elif MOTEUR_FUZZ_TARGET == 4
        std::string error;
        (void)moteur::SaveGame::decode(std::vector<std::uint8_t>(data, data + size), error);
#elif MOTEUR_FUZZ_TARGET == 5
        (void)moteur::decode_ktx2(data, size, "fuzz.ktx2", moteur::TextureSettings{}, moteur::CompressedFormats{true, true});
#elif MOTEUR_FUZZ_TARGET == 6
        (void)moteur::parse_gltf(data, size, "fuzz.glb");
#elif MOTEUR_FUZZ_TARGET == 7
        moteur::DataIssues issues;
        (void)moteur::DataTableBase::decode(std::vector<std::uint8_t>(data, data + size), "fuzz", issues);
#elif MOTEUR_FUZZ_TARGET == 8
        moteur::Input input;
        input.add_button("a");
        input.add_axis("b");
        input.load_bindings(text_of(data, size), "fuzz.json");
#elif MOTEUR_FUZZ_TARGET == 9
        (void)moteur::decode_image(data, size, "fuzz.png");
#elif MOTEUR_FUZZ_TARGET == 10
        // An archive: written to a file (the reader opens files), then every entry read.
        static const std::string path = [] {
            char* folder = SDL_GetPrefPath("moteur", "fuzz");
            std::string p = std::string(folder != nullptr ? folder : "") + "fuzz.pak";
            SDL_free(folder);
            return p;
        }();
        std::string error;
        if (moteur::write_file_atomic(path, data, size, error)) {
            if (const auto archive = moteur::Archive::open(path, error)) {
                for (const moteur::Archive::Entry& entry : archive->entries()) {
                    try {
                        (void)archive->read(entry);
                    } catch (const std::exception&) {
                    }
                }
            }
        }
#else
#error "MOTEUR_FUZZ_TARGET: 1 to 10"
#endif
    } catch (const std::exception&) {
        // Refused: what a reader must do with a bad file.
    }
    return 0;
}
