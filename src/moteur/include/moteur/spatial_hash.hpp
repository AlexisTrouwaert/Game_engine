#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace moteur {

// The circles of the plane sorted into square buckets (milestone 6, parts 3 and 6): collisions test
// only neighbours, and the game asks who is in a circle, a cone or a rectangle. Filled once per tick
// (separate_colliders() does it), then read.
//
// Every query returns entities sorted by distance from the query's origin, then by identifier: the
// same answer on every machine. An entity counts when its circle overlaps the area (for a cone or a
// rectangle: its centre within the area grown by its radius).
class SpatialHash {
public:
    struct Entry {
        entt::entity entity = entt::null;
        glm::vec2 position{0.0f};
        float radius = 0.0f;
        std::uint32_t layer = 1;
    };

    explicit SpatialHash(float bucket_size = 2.0f);

    void clear();
    void insert(entt::entity entity, glm::vec2 position, float radius, std::uint32_t layer = 1);
    std::size_t size() const { return entries_.size(); }
    const std::vector<Entry>& entries() const { return entries_; }
    float largest_radius() const { return largest_radius_; }

    // Indices in entries() of the entries whose bucket touches the box [low, high] (unsorted,
    // each once): what the queries and the collisions start from.
    void candidates(glm::vec2 low, glm::vec2 high, std::vector<std::uint32_t>& out) const;

    // `layers`: only entries whose layer has a bit in it.
    std::vector<entt::entity> query_circle(glm::vec2 centre, float radius, std::uint32_t layers = ~0u) const;
    // A cone from `origin` towards `direction` (need not be unit), out to `range`, of half-angle
    // given by its cosine (0.5: 60 degrees each side; -1: a full circle).
    std::vector<entt::entity> query_cone(glm::vec2 origin, glm::vec2 direction, float range, float cos_half_angle,
                                         std::uint32_t layers = ~0u) const;
    // A rectangle from `origin` along `direction` (a sweep in front of a character): `length` ahead,
    // `half_width` each side.
    std::vector<entt::entity> query_rect(glm::vec2 origin, glm::vec2 direction, float length, float half_width,
                                         std::uint32_t layers = ~0u) const;

private:
    static std::int64_t key(int x, int y) {
        return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::int64_t>(static_cast<std::uint32_t>(y));
    }
    glm::ivec2 bucket(glm::vec2 p) const;
    std::vector<entt::entity> sorted(std::vector<std::pair<float, entt::entity>>& found) const;

    float bucket_size_;
    float largest_radius_ = 0.0f;
    std::vector<Entry> entries_;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> buckets_;
    mutable std::vector<std::uint32_t> scratch_;  // an entry is in one bucket only: no duplicates
};

}  // namespace moteur
