#include <doctest/doctest.h>

#include "moteur/geometry_pool.hpp"

using Ranges = moteur::GeometryPool::Ranges;

TEST_CASE("geometry pool: ranges are taken first fit, aligned, and merged when given back") {
    Ranges ranges;
    ranges.free = {{0, 4096}};
    std::uint32_t a = 0, b = 0, c = 0;
    REQUIRE(ranges.take(100, 256, a));   // rounded up to 256
    REQUIRE(ranges.take(256, 256, b));
    REQUIRE(ranges.take(1000, 256, c));  // 1024
    CHECK(a == 0);
    CHECK(b == 256);
    CHECK(c == 512);
    CHECK(ranges.free == std::vector<std::pair<std::uint32_t, std::uint32_t>>{{1536, 2560}});

    ranges.give_back(b, 256);  // a hole in the middle
    CHECK(ranges.free.size() == 2);
    std::uint32_t d = 0;
    REQUIRE(ranges.take(200, 256, d));
    CHECK(d == 256);  // first fit: the hole
    ranges.give_back(d, 256);
    ranges.give_back(a, 256);  // merged with the hole after it
    ranges.give_back(c, 1024); // and everything with the tail
    CHECK(ranges.free == std::vector<std::pair<std::uint32_t, std::uint32_t>>{{0, 4096}});

    std::uint32_t e = 0;
    CHECK_FALSE(ranges.take(8192, 256, e));  // larger than the page
}

TEST_CASE("geometry pool: a freed range waits a few frames before it is reused") {
    int pages = 0;
    moteur::GeometryPool pool([&pages](std::uint32_t) {
        ++pages;
        return moteur::GpuBuffer();  // no GPU here: the pool only does the bookkeeping
    });
    std::uint32_t first_offset = 0;
    {
        moteur::GpuSlice slice = pool.allocate(1000);
        first_offset = slice.offset();
        CHECK(pages == 1);
        CHECK(pool.stats().used_bytes == 1024);
    }  // released: the GPU may still read it during the frames in flight
    CHECK(pool.stats().used_bytes == 0);
    CHECK(pool.stats().waiting_bytes == 1024);
    moteur::GpuSlice other = pool.allocate(1000);
    CHECK(other.offset() != first_offset);  // not reused yet
    for (std::uint64_t f = 0; f < moteur::GeometryPool::kReuseDelayFrames; ++f) {
        pool.begin_frame();
    }
    CHECK(pool.stats().waiting_bytes == 0);
    moteur::GpuSlice again = pool.allocate(1000);
    CHECK(again.offset() == first_offset);  // free again
    CHECK(pages == 1);

    moteur::GpuSlice large = pool.allocate(moteur::GeometryPool::kPageSize / 2);  // a buffer of its own
    CHECK(pages == 2);
    CHECK(pool.stats().pages == 1);
}
