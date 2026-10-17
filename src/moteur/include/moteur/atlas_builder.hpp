#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "moteur/image.hpp"

namespace moteur {

/// One image to put in an atlas, in straight (non-premultiplied) alpha.
struct AtlasInput {
    std::string name;    ///< unique and non-empty, for example `monsters/skeleton/walk_e_00`
    Image image;         ///< RGBA8; its pixel count must match its width and height
    bool has_pivot = false;  ///< false: the pivot is the bottom center of the original image
    /// The pivot when `has_pivot` is true, in pixels of the original image, origin at its top
    /// left (ignored otherwise).
    ///@{
    int pivot_x = 0;
    int pivot_y = 0;
    ///@}
};

/// How build_atlas() lays out the pages.
struct AtlasOptions {
    int max_page_size = 2048;  ///< a page is never wider or taller than this, in pixels; 1 to 32768
    int padding = 1;           ///< border around each image, in pixels (>= 0), filled with copies of its edge pixels
    /// Cut the fully transparent margins of each image (alpha exactly 0). An image with nothing
    /// visible keeps a single transparent pixel, so its frame still exists.
    bool trim = true;
};

/// Where one image ended up, and what is needed to draw it as if it had never been cut.
/// All values are in pixels; TextureAtlas reads them back from the JSON file into a SpriteRegion.
struct AtlasFrame {
    std::string name;  ///< the AtlasInput::name
    int page = 0;      ///< index into AtlasResult::pages
    /// The (trimmed) image inside its page, padding excluded: top-left corner, width and height.
    ///@{
    int x = 0, y = 0, w = 0, h = 0;
    ///@}
    /// The size of the original image.
    ///@{
    int source_w = 0, source_h = 0;
    ///@}
    /// Top-left corner of the trimmed image inside the original (0, 0 when not trimmed).
    ///@{
    int offset_x = 0, offset_y = 0;
    ///@}
    /// The pivot, in pixels of the original image: the given one, or else its bottom center
    /// (`source_w / 2`, `source_h`).
    ///@{
    int pivot_x = 0, pivot_y = 0;
    ///@}
};

/// One page of an atlas, ready to be written as a PNG or turned into a texture.
struct AtlasPage {
    int width = 0;   ///< in pixels: a power of two, or AtlasOptions::max_page_size
    int height = 0;  ///< in pixels: a power of two, or AtlasOptions::max_page_size
    std::vector<std::uint8_t> pixels;  ///< RGBA8, straight alpha, rows from top to bottom; transparent black where unused
};

/// The output of build_atlas().
struct AtlasResult {
    std::vector<AtlasPage> pages;      ///< at least one when there was at least one input
    std::vector<AtlasFrame> frames;    ///< one per input, sorted by name
};

/// Packs the images into as few pages as possible.
///
/// The result depends only on the images, their names and the options, never on the order of
/// `inputs` or on the platform: the same art gives the same atlas on Windows and macOS.
/// Each page is the smallest size (powers of two, at most AtlasOptions::max_page_size; the
/// squarest among equal areas) that holds all the images left to place, so small sets give small
/// pages and only sets that do not fit max_page_size are split into several pages (each filled at
/// max_page_size, then shrunk to the power of two that holds what it got).
///
/// Throws std::invalid_argument for an empty or duplicated name, an image of zero size or whose
/// pixel count does not match its size, or bad options; std::runtime_error if one image (with its
/// padding) is larger than a page. Plain CPU work (no GPU), callable from any thread; allocates the
/// pages, and tries several page sizes, so keep it for tools and load time, not for every frame.
AtlasResult build_atlas(std::vector<AtlasInput> inputs, const AtlasOptions& options = {});

}  // namespace moteur
