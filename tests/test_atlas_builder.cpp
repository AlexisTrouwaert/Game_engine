#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/atlas_builder.hpp"

using moteur::AtlasFrame;
using moteur::AtlasInput;
using moteur::AtlasOptions;
using moteur::AtlasResult;

namespace {

using Rgba = std::array<std::uint8_t, 4>;

// An image of the given size where every pixel is computed from its position.
template <typename Fn>
moteur::Image make_image(int w, int h, Fn pixel) {
    moteur::Image image;
    image.width = w;
    image.height = h;
    image.pixels.resize(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Rgba c = pixel(x, y);
            std::copy(c.begin(), c.end(), &image.pixels[(static_cast<std::size_t>(y) * w + x) * 4]);
        }
    }
    return image;
}

moteur::Image solid(int w, int h, Rgba color) {
    return make_image(w, h, [&](int, int) { return color; });
}

// Every pixel visible and different, so that any misplaced pixel is noticed.
moteur::Image gradient(int w, int h, int seed = 0) {
    return make_image(w, h, [&](int x, int y) {
        return Rgba{static_cast<std::uint8_t>(x * 7 + seed), static_cast<std::uint8_t>(y * 13 + seed), static_cast<std::uint8_t>(x + y + seed), 255};
    });
}

AtlasInput input(const std::string& name, moteur::Image image) {
    AtlasInput in;
    in.name = name;
    in.image = std::move(image);
    return in;
}

const AtlasFrame& frame_named(const AtlasResult& atlas, const std::string& name) {
    for (const AtlasFrame& frame : atlas.frames) {
        if (frame.name == name) return frame;
    }
    FAIL("no frame named " << name);
    return atlas.frames.front();
}

const std::uint8_t* page_pixel(const AtlasResult& atlas, int page, int x, int y) {
    const auto& p = atlas.pages[static_cast<std::size_t>(page)];
    return &p.pixels[(static_cast<std::size_t>(y) * p.width + x) * 4];
}

bool same_pixel(const std::uint8_t* a, const std::uint8_t* b) {
    return std::equal(a, a + 4, b);
}

bool is_power_of_two(int v) {
    return v > 0 && (v & (v - 1)) == 0;
}

}  // namespace

TEST_CASE("a single image gets one page and is copied exactly") {
    const moteur::Image source = gradient(10, 7);
    const AtlasResult atlas = moteur::build_atlas({input("a", source)});

    REQUIRE(atlas.pages.size() == 1);
    REQUIRE(atlas.frames.size() == 1);
    const AtlasFrame& f = atlas.frames[0];
    CHECK((f.w == 10 && f.h == 7));
    CHECK((f.source_w == 10 && f.source_h == 7));
    CHECK((f.offset_x == 0 && f.offset_y == 0));
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 10; ++x) {
            CHECK(same_pixel(page_pixel(atlas, 0, f.x + x, f.y + y), &source.pixels[(static_cast<std::size_t>(y) * 10 + x) * 4]));
        }
    }
}

TEST_CASE("pages are powers of two no bigger than the limit") {
    const AtlasResult atlas = moteur::build_atlas({input("a", gradient(30, 20)), input("b", gradient(10, 10))});
    for (const auto& page : atlas.pages) {
        CHECK(is_power_of_two(page.width));
        CHECK(is_power_of_two(page.height));
        CHECK(page.width <= 2048);
        CHECK(page.height <= 2048);
        CHECK(page.pixels.size() == static_cast<std::size_t>(page.width) * page.height * 4);
    }
}

TEST_CASE("a set of images that tiles a square gets exactly that square") {
    // Sixteen 30x30 images with a 1 pixel padding are sixteen 32x32 rectangles: a 128x128 page.
    std::vector<AtlasInput> inputs;
    for (int i = 0; i < 16; ++i) {
        inputs.push_back(input("img" + std::to_string(100 + i), gradient(30, 30, i)));
    }
    const AtlasResult atlas = moteur::build_atlas(inputs);
    REQUIRE(atlas.pages.size() == 1);
    CHECK((atlas.pages[0].width == 128 && atlas.pages[0].height == 128));
}

TEST_CASE("a page is compact: it does not stretch to the maximum width") {
    // Many small images, far below the 2048 limit: the old behaviour lined them up in one long row.
    std::mt19937 rng(5);
    std::vector<AtlasInput> inputs;
    long long area = 0;
    for (int i = 0; i < 100; ++i) {
        const int w = 4 + static_cast<int>(rng() % 40);
        const int h = 4 + static_cast<int>(rng() % 40);
        inputs.push_back(input("r" + std::to_string(100 + i), gradient(w, h, i)));
        area += static_cast<long long>(w + 2) * (h + 2);
    }
    const AtlasResult atlas = moteur::build_atlas(inputs);
    REQUIRE(atlas.pages.size() == 1);
    const long long page_area = static_cast<long long>(atlas.pages[0].width) * atlas.pages[0].height;
    CHECK(page_area <= 3 * area);  // a power-of-two page never wastes much more than half, plus packing losses
    CHECK(atlas.pages[0].width <= 4 * atlas.pages[0].height);
    CHECK(atlas.pages[0].height <= 4 * atlas.pages[0].width);
}

TEST_CASE("transparent margins are trimmed and remembered") {
    // A 10x10 image whose only visible pixels are a 2x3 block at (3, 4).
    const moteur::Image source = make_image(10, 10, [](int x, int y) {
        const bool inside = x >= 3 && x <= 4 && y >= 4 && y <= 6;
        return Rgba{200, 100, 50, static_cast<std::uint8_t>(inside ? 255 : 0)};
    });
    const AtlasResult atlas = moteur::build_atlas({input("a", source)});
    const AtlasFrame& f = atlas.frames[0];
    CHECK((f.w == 2 && f.h == 3));
    CHECK((f.offset_x == 3 && f.offset_y == 4));
    CHECK((f.source_w == 10 && f.source_h == 10));
}

TEST_CASE("without trimming the whole image is kept") {
    AtlasOptions options;
    options.trim = false;
    const moteur::Image source = make_image(10, 10, [](int x, int y) {
        return Rgba{1, 2, 3, static_cast<std::uint8_t>(x == 5 && y == 5 ? 255 : 0)};
    });
    const AtlasResult atlas = moteur::build_atlas({input("a", source)}, options);
    CHECK((atlas.frames[0].w == 10 && atlas.frames[0].h == 10));
    CHECK((atlas.frames[0].offset_x == 0 && atlas.frames[0].offset_y == 0));
}

TEST_CASE("a fully transparent image becomes one transparent pixel") {
    const AtlasResult atlas = moteur::build_atlas({input("a", solid(8, 8, {9, 9, 9, 0}))});
    const AtlasFrame& f = atlas.frames[0];
    CHECK((f.w == 1 && f.h == 1));
    CHECK((f.source_w == 8 && f.source_h == 8));
    const std::uint8_t* p = page_pixel(atlas, 0, f.x, f.y);
    CHECK((p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0));
}

TEST_CASE("padding repeats the edge pixels around an image") {
    AtlasOptions options;
    options.padding = 2;
    const AtlasResult atlas = moteur::build_atlas({input("a", gradient(4, 3))}, options);
    const AtlasFrame& f = atlas.frames[0];

    // Left, right, top and bottom borders copy the nearest edge pixel.
    for (int d = 1; d <= 2; ++d) {
        for (int y = 0; y < 3; ++y) {
            CHECK(same_pixel(page_pixel(atlas, 0, f.x - d, f.y + y), page_pixel(atlas, 0, f.x, f.y + y)));
            CHECK(same_pixel(page_pixel(atlas, 0, f.x + 3 + d, f.y + y), page_pixel(atlas, 0, f.x + 3, f.y + y)));
        }
        for (int x = 0; x < 4; ++x) {
            CHECK(same_pixel(page_pixel(atlas, 0, f.x + x, f.y - d), page_pixel(atlas, 0, f.x + x, f.y)));
            CHECK(same_pixel(page_pixel(atlas, 0, f.x + x, f.y + 2 + d), page_pixel(atlas, 0, f.x + x, f.y + 2)));
        }
    }
    // Corners copy the corner pixel.
    CHECK(same_pixel(page_pixel(atlas, 0, f.x - 2, f.y - 2), page_pixel(atlas, 0, f.x, f.y)));
    CHECK(same_pixel(page_pixel(atlas, 0, f.x + 5, f.y + 4), page_pixel(atlas, 0, f.x + 3, f.y + 2)));
}

TEST_CASE("padding may be zero") {
    AtlasOptions options;
    options.padding = 0;
    const AtlasResult atlas = moteur::build_atlas({input("a", gradient(4, 4)), input("b", gradient(4, 4, 50))}, options);
    CHECK(atlas.frames.size() == 2);
}

TEST_CASE("the pivot defaults to the bottom center and can be set") {
    AtlasInput plain = input("plain", solid(20, 30, {1, 1, 1, 255}));
    AtlasInput custom = input("custom", solid(20, 30, {1, 1, 1, 255}));
    custom.has_pivot = true;
    custom.pivot_x = 5;
    custom.pivot_y = 7;
    const AtlasResult atlas = moteur::build_atlas({plain, custom});
    CHECK((frame_named(atlas, "plain").pivot_x == 10 && frame_named(atlas, "plain").pivot_y == 30));
    CHECK((frame_named(atlas, "custom").pivot_x == 5 && frame_named(atlas, "custom").pivot_y == 7));
}

TEST_CASE("the pivot is expressed in the original image, not the trimmed one") {
    const moteur::Image source = make_image(20, 20, [](int x, int y) {
        return Rgba{1, 1, 1, static_cast<std::uint8_t>(x >= 8 && x < 12 && y >= 2 && y < 10 ? 255 : 0)};
    });
    const AtlasFrame f = moteur::build_atlas({input("a", source)}).frames[0];
    CHECK((f.pivot_x == 10 && f.pivot_y == 20));  // bottom center of the 20x20 original
    CHECK((f.offset_x == 8 && f.offset_y == 2));
}

TEST_CASE("frames come out sorted by name") {
    const AtlasResult atlas = moteur::build_atlas({input("c", gradient(5, 5)), input("a", gradient(6, 6)), input("b", gradient(7, 7))});
    REQUIRE(atlas.frames.size() == 3);
    CHECK(atlas.frames[0].name == "a");
    CHECK(atlas.frames[1].name == "b");
    CHECK(atlas.frames[2].name == "c");
}

TEST_CASE("many random images never overlap and stay inside their page") {
    std::mt19937 rng(7);
    std::vector<AtlasInput> inputs;
    for (int i = 0; i < 300; ++i) {
        const int w = 1 + static_cast<int>(rng() % 60);
        const int h = 1 + static_cast<int>(rng() % 60);
        inputs.push_back(input("img_" + std::to_string(1000 + i), gradient(w, h, i)));
    }
    AtlasOptions options;
    options.max_page_size = 256;
    options.padding = 2;
    const AtlasResult atlas = moteur::build_atlas(inputs, options);

    REQUIRE(atlas.frames.size() == 300);
    CHECK(atlas.pages.size() > 1);  // 300 images cannot fit in a 256x256 page

    // Each frame plus its padding lies inside its page ...
    for (const AtlasFrame& f : atlas.frames) {
        const auto& page = atlas.pages[static_cast<std::size_t>(f.page)];
        CHECK(f.x - 2 >= 0);
        CHECK(f.y - 2 >= 0);
        CHECK(f.x + f.w + 2 <= page.width);
        CHECK(f.y + f.h + 2 <= page.height);
    }
    // ... and no two frames of a page touch, padding included.
    int overlaps = 0;
    for (std::size_t a = 0; a < atlas.frames.size(); ++a) {
        for (std::size_t b = a + 1; b < atlas.frames.size(); ++b) {
            const AtlasFrame& fa = atlas.frames[a];
            const AtlasFrame& fb = atlas.frames[b];
            if (fa.page != fb.page) continue;
            const bool apart = fa.x + fa.w + 2 <= fb.x - 2 || fb.x + fb.w + 2 <= fa.x - 2 ||
                               fa.y + fa.h + 2 <= fb.y - 2 || fb.y + fb.h + 2 <= fa.y - 2;
            if (!apart) ++overlaps;
        }
    }
    CHECK(overlaps == 0);
}

TEST_CASE("every pixel of every image is found at its place in the page") {
    std::mt19937 rng(11);
    std::vector<AtlasInput> inputs;
    std::vector<moteur::Image> sources;
    for (int i = 0; i < 40; ++i) {
        const int w = 3 + static_cast<int>(rng() % 30);
        const int h = 3 + static_cast<int>(rng() % 30);
        sources.push_back(gradient(w, h, i * 3));
        inputs.push_back(input("n" + std::to_string(100 + i), sources.back()));
    }
    const AtlasResult atlas = moteur::build_atlas(inputs);
    int wrong = 0;
    for (int i = 0; i < 40; ++i) {
        const AtlasFrame& f = frame_named(atlas, "n" + std::to_string(100 + i));
        for (int y = 0; y < f.h; ++y) {
            for (int x = 0; x < f.w; ++x) {
                const std::uint8_t* expected = &sources[static_cast<std::size_t>(i)].pixels[(static_cast<std::size_t>(y + f.offset_y) * f.source_w + (x + f.offset_x)) * 4];
                if (!same_pixel(page_pixel(atlas, f.page, f.x + x, f.y + y), expected)) ++wrong;
            }
        }
    }
    CHECK(wrong == 0);
}

TEST_CASE("the result does not depend on the order of the inputs") {
    std::mt19937 rng(3);
    std::vector<AtlasInput> inputs;
    for (int i = 0; i < 80; ++i) {
        // Many identical sizes: exactly where an unstable sort would give different results.
        const int w = 8 + static_cast<int>(rng() % 3) * 8;
        const int h = 8 + static_cast<int>(rng() % 3) * 8;
        inputs.push_back(input("s" + std::to_string(100 + i), gradient(w, h, i)));
    }
    AtlasOptions options;
    options.max_page_size = 128;

    const AtlasResult first = moteur::build_atlas(inputs, options);
    std::shuffle(inputs.begin(), inputs.end(), rng);
    const AtlasResult second = moteur::build_atlas(inputs, options);
    std::reverse(inputs.begin(), inputs.end());
    const AtlasResult third = moteur::build_atlas(inputs, options);

    for (const AtlasResult* other : {&second, &third}) {
        REQUIRE(other->pages.size() == first.pages.size());
        REQUIRE(other->frames.size() == first.frames.size());
        for (std::size_t i = 0; i < first.frames.size(); ++i) {
            const AtlasFrame& a = first.frames[i];
            const AtlasFrame& b = other->frames[i];
            CHECK((a.name == b.name && a.page == b.page && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h));
        }
        for (std::size_t p = 0; p < first.pages.size(); ++p) {
            CHECK((first.pages[p].width == other->pages[p].width && first.pages[p].pixels == other->pages[p].pixels));
        }
    }
}

TEST_CASE("images that do not fit one page use several") {
    AtlasOptions options;
    options.max_page_size = 64;
    options.padding = 1;
    std::vector<AtlasInput> inputs;
    for (int i = 0; i < 20; ++i) {
        inputs.push_back(input("i" + std::to_string(100 + i), gradient(30, 30, i)));
    }
    const AtlasResult atlas = moteur::build_atlas(inputs, options);
    CHECK(atlas.pages.size() >= 5);  // a 64x64 page holds four 32x32 rectangles
    CHECK(atlas.frames.size() == 20);
    for (const AtlasFrame& f : atlas.frames) {
        CHECK(f.page >= 0);
        CHECK(f.page < static_cast<int>(atlas.pages.size()));
    }
}

TEST_CASE("an image larger than a page is refused with its name") {
    AtlasOptions options;
    options.max_page_size = 32;
    try {
        (void)moteur::build_atlas({input("huge_sprite", gradient(40, 10))}, options);
        FAIL("expected an exception");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("huge_sprite") != std::string::npos);
    }
}

TEST_CASE("bad input is rejected") {
    CHECK_THROWS_AS(moteur::build_atlas({input("a", gradient(4, 4)), input("a", gradient(4, 4))}), std::invalid_argument);
    CHECK_THROWS_AS(moteur::build_atlas({input("", gradient(4, 4))}), std::invalid_argument);

    moteur::Image broken = gradient(4, 4);
    broken.pixels.pop_back();
    CHECK_THROWS_AS(moteur::build_atlas({input("a", broken)}), std::invalid_argument);

    AtlasOptions bad;
    bad.padding = -1;
    CHECK_THROWS_AS(moteur::build_atlas({input("a", gradient(4, 4))}, bad), std::invalid_argument);
    bad = {};
    bad.max_page_size = 0;
    CHECK_THROWS_AS(moteur::build_atlas({input("a", gradient(4, 4))}, bad), std::invalid_argument);
}

// Not run by default (it prints timings, it does not check anything). Run it in a Release build with:
//   moteur_tests -tc="atlas packing benchmark" --no-skip
TEST_CASE("atlas packing benchmark" * doctest::skip()) {
    for (const int count : {200, 1000, 3000, 8000}) {
        std::mt19937 rng(42);
        std::vector<AtlasInput> inputs;
        long long pixels = 0;
        for (int i = 0; i < count; ++i) {
            const int w = 8 + static_cast<int>(rng() % 120);
            const int h = 8 + static_cast<int>(rng() % 120);
            inputs.push_back(input("sprite_" + std::to_string(100000 + i), gradient(w, h, i)));
            pixels += static_cast<long long>(w) * h;
        }
        const auto start = std::chrono::steady_clock::now();
        const AtlasResult atlas = moteur::build_atlas(inputs);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        long long page_pixels = 0;
        for (const auto& page : atlas.pages) page_pixels += static_cast<long long>(page.width) * page.height;
        MESSAGE(count << " images (" << pixels / 1000 << " kpixels): " << ms << " ms, " << atlas.pages.size()
                      << " page(s), " << 100.0 * static_cast<double>(pixels) / static_cast<double>(page_pixels)
                      << " % of the pages used by image pixels");
    }
}

TEST_CASE("an empty atlas is empty") {
    const AtlasResult atlas = moteur::build_atlas({});
    CHECK(atlas.pages.empty());
    CHECK(atlas.frames.empty());
}
