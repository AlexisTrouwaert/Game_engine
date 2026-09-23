#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "moteur/image.hpp"

namespace moteur {

// One image to put in an atlas, in straight (non-premultiplied) alpha.
struct AtlasInput {
    std::string name;    // unique, for example "monsters/skeleton/walk_e_00"
    Image image;
    bool has_pivot = false;  // false: the pivot is the bottom center of the original image
    int pivot_x = 0;         // in pixels of the original image, origin at its top left
    int pivot_y = 0;
};

struct AtlasOptions {
    int max_page_size = 2048;  // a page is never wider or taller than this
    int padding = 1;           // border around each image, filled with copies of its edge pixels
    bool trim = true;          // cut the fully transparent margins of each image
};

// Where one image ended up, and what is needed to draw it as if it had never been cut.
struct AtlasFrame {
    std::string name;
    int page = 0;
    int x = 0, y = 0, w = 0, h = 0;    // the (trimmed) image inside its page, padding excluded
    int source_w = 0, source_h = 0;    // the original image
    int offset_x = 0, offset_y = 0;    // where the trimmed image was inside the original
    int pivot_x = 0, pivot_y = 0;      // in pixels of the original image
};

struct AtlasPage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // RGBA8, straight alpha, rows from top to bottom
};

struct AtlasResult {
    std::vector<AtlasPage> pages;
    std::vector<AtlasFrame> frames;    // sorted by name
};

// Packs the images into as few pages as possible.
//
// The result depends only on the images, their names and the options, never on the order of
// `inputs` or on the platform: the same art gives the same atlas on Windows and macOS.
// Each page is the smallest size (powers of two, at most max_page_size; the squarest among
// equal areas) that holds all the images left to place, so small sets give small pages and only
// sets that do not fit max_page_size are split into several full pages.
//
// Throws std::invalid_argument for an empty or duplicated name, an image whose pixel count does not
// match its size, or bad options; std::runtime_error if one image is larger than a page.
AtlasResult build_atlas(std::vector<AtlasInput> inputs, const AtlasOptions& options = {});

}  // namespace moteur
