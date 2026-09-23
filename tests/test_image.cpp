#include <doctest/doctest.h>

#include "moteur/image.hpp"

namespace {

moteur::Image pixels(std::initializer_list<std::uint8_t> values, int width) {
    moteur::Image image;
    image.width = width;
    image.height = static_cast<int>(values.size()) / 4 / width;
    image.pixels.assign(values.begin(), values.end());
    return image;
}

}  // namespace

TEST_CASE("premultiplying leaves opaque pixels alone and clears transparent ones") {
    moteur::Image image = pixels({10, 20, 30, 255,   200, 100, 50, 0,   255, 255, 255, 128}, 3);
    moteur::premultiply_alpha(image);

    CHECK((image.pixels[0] == 10 && image.pixels[1] == 20 && image.pixels[2] == 30 && image.pixels[3] == 255));
    CHECK((image.pixels[4] == 0 && image.pixels[5] == 0 && image.pixels[6] == 0 && image.pixels[7] == 0));
    // 255 * 128 / 255 = 128, and the alpha itself is never changed.
    CHECK((image.pixels[8] == 128 && image.pixels[9] == 128 && image.pixels[10] == 128 && image.pixels[11] == 128));
}

TEST_CASE("premultiplying rounds to the nearest value") {
    // 128 * 128 = 16384 -> 64.25 -> 64 ; 1 * 128 -> 0.5 -> 1 ; 255 * 128 -> 128
    moteur::Image image = pixels({128, 1, 255, 128}, 1);
    moteur::premultiply_alpha(image);
    CHECK((image.pixels[0] == 64 && image.pixels[1] == 1 && image.pixels[2] == 128));
}

TEST_CASE("premultiplying an empty image does nothing") {
    moteur::Image image;
    moteur::premultiply_alpha(image);
    CHECK(image.pixels.empty());
}
