// atlas_packer: packs a folder of PNG images into atlas pages and a JSON description.
//
//   atlas_packer --input <folder> --output <folder> --name <name>
//                [--page-size N] [--padding N] [--no-trim]
//
// Every PNG below <folder> becomes a sprite named after its path relative to <folder>, without
// the extension and with '/' as separator ("monsters/skeleton/walk_00.png" -> "monsters/skeleton/walk_00").
// An optional <folder>/pivots.json sets the pivot of some sprites: {"name": [x, y], ...}, in pixels
// of the original image. By default the pivot is the bottom center.
//
// Output: <output>/<name>.json and <output>/<name>_0.png, <name>_1.png, ...
// The output only depends on the images, so the same art gives the same files on every machine.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "moteur/atlas_builder.hpp"
#include "moteur/image.hpp"

namespace fs = std::filesystem;

namespace {

struct Arguments {
    fs::path input;
    fs::path output;
    std::string name;
    moteur::AtlasOptions options;
};

// A path as UTF-8 text, which is what load_image expects (std::filesystem gives the system code page on Windows).
std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// The opposite: a path from UTF-8 text.
fs::path path_from_utf8(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

bool parse_arguments(int argc, char** argv, Arguments& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--input" && has_value) {
            args.input = path_from_utf8(argv[++i]);
        } else if (arg == "--output" && has_value) {
            args.output = path_from_utf8(argv[++i]);
        } else if (arg == "--name" && has_value) {
            args.name = argv[++i];
        } else if (arg == "--page-size" && has_value) {
            args.options.max_page_size = std::atoi(argv[++i]);
        } else if (arg == "--padding" && has_value) {
            args.options.padding = std::atoi(argv[++i]);
        } else if (arg == "--no-trim") {
            args.options.trim = false;
        } else {
            std::cerr << "atlas_packer: unknown or incomplete argument '" << arg << "'\n";
            return false;
        }
    }
    if (args.input.empty() || args.output.empty() || args.name.empty()) {
        std::cerr << "usage: atlas_packer --input <folder> --output <folder> --name <name> "
                     "[--page-size N] [--padding N] [--no-trim]\n";
        return false;
    }
    return true;
}

bool has_png_extension(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".png";
}

void write_png(const fs::path& path, const moteur::AtlasPage& page) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot write " + utf8(path));
    }
    const auto write = [](void* context, void* data, int size) {
        static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
    };
    if (stbi_write_png_to_func(write, &file, page.width, page.height, 4, page.pixels.data(), page.width * 4) == 0) {
        throw std::runtime_error("cannot encode " + utf8(path));
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Arguments args;
        if (!parse_arguments(argc, argv, args)) {
            return 2;
        }
        if (!fs::is_directory(args.input)) {
            std::cerr << "atlas_packer: '" << utf8(args.input) << "' is not a folder\n";
            return 2;
        }

        // Optional pivots.
        nlohmann::json pivots = nlohmann::json::object();
        const fs::path pivots_path = args.input / "pivots.json";
        if (fs::exists(pivots_path)) {
            std::ifstream file(pivots_path);
            pivots = nlohmann::json::parse(file);
        }

        std::vector<moteur::AtlasInput> inputs;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(args.input)) {
            if (!entry.is_regular_file() || !has_png_extension(entry.path())) {
                continue;
            }
            fs::path relative = fs::relative(entry.path(), args.input);
            relative.replace_extension();

            moteur::AtlasInput input;
            const std::u8string generic = relative.generic_u8string();  // '/' separators on every OS
            input.name = std::string(generic.begin(), generic.end());
            input.image = moteur::load_image(utf8(entry.path()));
            if (pivots.contains(input.name)) {
                input.has_pivot = true;
                input.pivot_x = pivots.at(input.name).at(0).get<int>();
                input.pivot_y = pivots.at(input.name).at(1).get<int>();
            }
            inputs.push_back(std::move(input));
        }
        for (const auto& item : pivots.items()) {
            const bool known = std::any_of(inputs.begin(), inputs.end(),
                                           [&](const moteur::AtlasInput& in) { return in.name == item.key(); });
            if (!known) {
                std::cerr << "atlas_packer: warning: pivots.json mentions '" << item.key() << "', which is not an image\n";
            }
        }

        const moteur::AtlasResult atlas = moteur::build_atlas(std::move(inputs), args.options);

        fs::create_directories(args.output);

        // Remove the pages of a previous run, which may have been more numerous.
        const std::string prefix = args.name + "_";
        for (const fs::directory_entry& entry : fs::directory_iterator(args.output)) {
            const std::string file = entry.path().filename().string();
            if (file.rfind(prefix, 0) == 0 && has_png_extension(entry.path())) {
                fs::remove(entry.path());
            }
        }

        nlohmann::json doc;
        doc["version"] = 1;
        doc["pages"] = nlohmann::json::array();
        for (std::size_t i = 0; i < atlas.pages.size(); ++i) {
            const std::string file = prefix + std::to_string(i) + ".png";
            write_png(args.output / file, atlas.pages[i]);
            doc["pages"].push_back({{"file", file}, {"width", atlas.pages[i].width}, {"height", atlas.pages[i].height}});
        }
        doc["frames"] = nlohmann::json::object();
        for (const moteur::AtlasFrame& f : atlas.frames) {
            doc["frames"][f.name] = {{"page", f.page},           {"x", f.x},
                                     {"y", f.y},                 {"w", f.w},
                                     {"h", f.h},                 {"source_w", f.source_w},
                                     {"source_h", f.source_h},   {"offset_x", f.offset_x},
                                     {"offset_y", f.offset_y},   {"pivot_x", f.pivot_x},
                                     {"pivot_y", f.pivot_y}};
        }
        std::ofstream json_file(args.output / (args.name + ".json"), std::ios::binary);
        json_file << doc.dump(2) << '\n';

        std::cout << "atlas '" << args.name << "': " << atlas.frames.size() << " images, " << atlas.pages.size()
                  << " page(s):";
        for (const moteur::AtlasPage& page : atlas.pages) {
            std::cout << ' ' << page.width << 'x' << page.height;
        }
        std::cout << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "atlas_packer: " << e.what() << '\n';
        return 1;
    }
}
