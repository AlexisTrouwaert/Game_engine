#include "moteur/atlas_builder.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {

// stb_rect_pack sorts its rectangles with qsort, which orders equal elements differently on
// Windows, Linux and macOS: the same input would give different atlases. This stable merge sort
// has the same signature and keeps equal elements in their input order, so the result is the same
// everywhere (the rectangles are already put in a fully determined order before packing).
void stable_sort_bytes(void* base, std::size_t count, std::size_t size,
                       int (*compare)(const void*, const void*)) {
    if (count < 2) {
        return;
    }
    auto* data = static_cast<unsigned char*>(base);
    std::vector<unsigned char> buffer(count * size);
    for (std::size_t width = 1; width < count; width *= 2) {
        for (std::size_t left = 0; left < count; left += 2 * width) {
            const std::size_t middle = std::min(left + width, count);
            const std::size_t right = std::min(left + 2 * width, count);
            std::size_t i = left, j = middle, out = left;
            while (i < middle && j < right) {
                // Taking the left element unless the right one is strictly smaller keeps it stable.
                if (compare(data + j * size, data + i * size) < 0) {
                    std::memcpy(buffer.data() + out++ * size, data + j++ * size, size);
                } else {
                    std::memcpy(buffer.data() + out++ * size, data + i++ * size, size);
                }
            }
            while (i < middle) std::memcpy(buffer.data() + out++ * size, data + i++ * size, size);
            while (j < right) std::memcpy(buffer.data() + out++ * size, data + j++ * size, size);
        }
        std::memcpy(data, buffer.data(), count * size);
    }
}

}  // namespace

#define STBRP_SORT stable_sort_bytes
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

namespace moteur {

namespace {

struct Prepared {
    int trim_x = 0, trim_y = 0;  // trimmed area, in the original image
    int w = 0, h = 0;
    int packed_w = 0, packed_h = 0;  // with the padding
};

bool is_visible(const Image& image, int x, int y) {
    return image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4 + 3] != 0;
}

Prepared prepare(const Image& image, const AtlasOptions& options) {
    Prepared p;
    p.w = image.width;
    p.h = image.height;
    if (options.trim) {
        int min_x = image.width, min_y = image.height, max_x = -1, max_y = -1;
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                if (is_visible(image, x, y)) {
                    min_x = std::min(min_x, x);
                    max_x = std::max(max_x, x);
                    min_y = std::min(min_y, y);
                    max_y = std::max(max_y, y);
                }
            }
        }
        if (max_x < 0) {
            // Nothing visible: keep a single transparent pixel, so the frame still exists.
            p.w = 1;
            p.h = 1;
        } else {
            p.trim_x = min_x;
            p.trim_y = min_y;
            p.w = max_x - min_x + 1;
            p.h = max_y - min_y + 1;
        }
    }
    p.packed_w = p.w + 2 * options.padding;
    p.packed_h = p.h + 2 * options.padding;
    return p;
}

int next_power_of_two(int value) {
    int result = 1;
    while (result < value) {
        result *= 2;
    }
    return result;
}

}  // namespace

AtlasResult build_atlas(std::vector<AtlasInput> inputs, const AtlasOptions& options) {
    if (options.max_page_size < 1 || options.max_page_size > 32768 || options.padding < 0) {
        throw std::invalid_argument("build_atlas: invalid options");
    }

    // A fixed order, whatever the caller's: alphabetical by name (byte by byte, no locale).
    std::sort(inputs.begin(), inputs.end(), [](const AtlasInput& a, const AtlasInput& b) { return a.name < b.name; });

    std::set<std::string> seen;
    for (const AtlasInput& input : inputs) {
        if (input.name.empty()) {
            throw std::invalid_argument("build_atlas: an image has an empty name");
        }
        if (!seen.insert(input.name).second) {
            throw std::invalid_argument("build_atlas: duplicate name '" + input.name + "'");
        }
        if (input.image.width <= 0 || input.image.height <= 0 ||
            input.image.pixels.size() != static_cast<std::size_t>(input.image.width) * input.image.height * 4) {
            throw std::invalid_argument("build_atlas: image '" + input.name + "' has an invalid size");
        }
    }

    std::vector<Prepared> prepared;
    prepared.reserve(inputs.size());
    for (const AtlasInput& input : inputs) {
        Prepared p = prepare(input.image, options);
        if (p.packed_w > options.max_page_size || p.packed_h > options.max_page_size) {
            throw std::runtime_error("build_atlas: image '" + input.name + "' (" + std::to_string(p.packed_w) + "x" +
                                     std::to_string(p.packed_h) + " with padding) does not fit in a " +
                                     std::to_string(options.max_page_size) + " pixel page");
        }
        prepared.push_back(p);
    }

    // Packing order: tall first, then wide, then by name. Fully determined, and a good order for packing.
    std::vector<std::size_t> order(inputs.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (prepared[a].packed_h != prepared[b].packed_h) return prepared[a].packed_h > prepared[b].packed_h;
        if (prepared[a].packed_w != prepared[b].packed_w) return prepared[a].packed_w > prepared[b].packed_w;
        return a < b;  // inputs are sorted by name, so this is by name
    });

    AtlasResult result;
    result.frames.resize(inputs.size());
    std::vector<int> frame_x(inputs.size()), frame_y(inputs.size());  // top left of each packed rectangle
    std::vector<int> frame_page(inputs.size(), -1);

    // Packs the remaining rectangles into a page of the given size. stb_rect_pack hands them back
    // in the order they were given, each marked as packed or not.
    const auto pack_into = [&](const std::vector<std::size_t>& remaining, int width, int height) {
        stbrp_context context;
        std::vector<stbrp_node> nodes(static_cast<std::size_t>(width));
        stbrp_init_target(&context, width, height, nodes.data(), static_cast<int>(nodes.size()));
        stbrp_setup_heuristic(&context, STBRP_HEURISTIC_Skyline_BF_sortHeight);

        std::vector<stbrp_rect> rects(remaining.size());
        for (std::size_t k = 0; k < remaining.size(); ++k) {
            rects[k] = {};
            rects[k].id = static_cast<int>(remaining[k]);
            rects[k].w = static_cast<stbrp_coord>(prepared[remaining[k]].packed_w);
            rects[k].h = static_cast<stbrp_coord>(prepared[remaining[k]].packed_h);
        }
        stbrp_pack_rects(&context, rects.data(), static_cast<int>(rects.size()));
        return rects;
    };

    // Page sizes worth trying: powers of two up to the limit, and the limit itself.
    std::vector<int> sizes;
    for (int size = 1; size < options.max_page_size; size *= 2) {
        sizes.push_back(size);
    }
    sizes.push_back(options.max_page_size);

    std::vector<std::size_t> remaining = order;
    while (!remaining.empty()) {
        long long total_area = 0;
        int widest = 0, tallest = 0;
        for (const std::size_t index : remaining) {
            total_area += static_cast<long long>(prepared[index].packed_w) * prepared[index].packed_h;
            widest = std::max(widest, prepared[index].packed_w);
            tallest = std::max(tallest, prepared[index].packed_h);
        }

        // Try the page sizes from the smallest area to the largest, the squarest first among equals,
        // and keep the first that holds everything: a compact page wastes far less memory than one
        // stretched to the maximum width.
        struct Candidate { int w, h; };
        std::vector<Candidate> candidates;
        for (const int w : sizes) {
            for (const int h : sizes) {
                if (w >= widest && h >= tallest && static_cast<long long>(w) * h >= total_area) {
                    candidates.push_back({w, h});
                }
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            const long long area_a = static_cast<long long>(a.w) * a.h, area_b = static_cast<long long>(b.w) * b.h;
            if (area_a != area_b) return area_a < area_b;
            const int skew_a = std::abs(a.w - a.h), skew_b = std::abs(b.w - b.h);
            if (skew_a != skew_b) return skew_a < skew_b;
            return a.w > b.w;
        });

        std::vector<stbrp_rect> rects;
        int page_width = 0, page_height = 0;
        bool everything_fits = false;
        for (const Candidate& candidate : candidates) {
            rects = pack_into(remaining, candidate.w, candidate.h);
            if (std::all_of(rects.begin(), rects.end(), [](const stbrp_rect& r) { return r.was_packed != 0; })) {
                page_width = candidate.w;
                page_height = candidate.h;
                everything_fits = true;
                break;
            }
        }
        if (!everything_fits) {
            // Too much for one page: fill a page of the maximum size, the rest goes to the next one.
            rects = pack_into(remaining, options.max_page_size, options.max_page_size);
        }

        const int page = static_cast<int>(result.pages.size());
        std::vector<std::size_t> next;
        int used_w = 1, used_h = 1;
        for (const stbrp_rect& rect : rects) {
            const auto index = static_cast<std::size_t>(rect.id);
            if (rect.was_packed == 0) {
                next.push_back(index);
                continue;
            }
            frame_page[index] = page;
            frame_x[index] = rect.x;
            frame_y[index] = rect.y;
            used_w = std::max(used_w, rect.x + rect.w);
            used_h = std::max(used_h, rect.y + rect.h);
        }
        if (next.size() == remaining.size()) {
            throw std::runtime_error("build_atlas: nothing could be packed into a new page");
        }
        if (!everything_fits) {
            page_width = std::min(next_power_of_two(used_w), options.max_page_size);
            page_height = std::min(next_power_of_two(used_h), options.max_page_size);
        }

        AtlasPage new_page;
        new_page.width = page_width;
        new_page.height = page_height;
        new_page.pixels.assign(static_cast<std::size_t>(new_page.width) * new_page.height * 4, 0);
        result.pages.push_back(std::move(new_page));

        // `next` is in packing order, ready for the following page.
        remaining = std::move(next);
    }

    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const AtlasInput& input = inputs[i];
        const Prepared& p = prepared[i];
        AtlasPage& page = result.pages[static_cast<std::size_t>(frame_page[i])];

        // A trimmed image with nothing visible became one transparent pixel: write exactly that.
        const bool blank = options.trim && p.w == 1 && p.h == 1 && !is_visible(input.image, p.trim_x, p.trim_y);

        // Draw the image with its padding: each border pixel is a copy of the nearest edge pixel.
        for (int py = 0; py < p.packed_h; ++py) {
            for (int px = 0; px < p.packed_w; ++px) {
                const int sx = std::clamp(px - options.padding, 0, p.w - 1);
                const int sy = std::clamp(py - options.padding, 0, p.h - 1);
                const std::uint8_t* source = &input.image.pixels[(static_cast<std::size_t>(p.trim_y + sy) * input.image.width + (p.trim_x + sx)) * 4];
                std::uint8_t* target = &page.pixels[(static_cast<std::size_t>(frame_y[i] + py) * page.width + (frame_x[i] + px)) * 4];
                if (blank) {
                    std::memset(target, 0, 4);
                } else {
                    std::memcpy(target, source, 4);
                }
            }
        }

        AtlasFrame& frame = result.frames[i];
        frame.name = input.name;
        frame.page = frame_page[i];
        frame.x = frame_x[i] + options.padding;
        frame.y = frame_y[i] + options.padding;
        frame.w = p.w;
        frame.h = p.h;
        frame.source_w = input.image.width;
        frame.source_h = input.image.height;
        frame.offset_x = p.trim_x;
        frame.offset_y = p.trim_y;
        frame.pivot_x = input.has_pivot ? input.pivot_x : input.image.width / 2;
        frame.pivot_y = input.has_pivot ? input.pivot_y : input.image.height;
    }
    return result;
}

}  // namespace moteur
