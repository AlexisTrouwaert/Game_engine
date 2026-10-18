#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace moteur {

/// A span of heights (y, metres) for the queries of a 3D level: an entry counts only if its own span
/// overlaps it (touching counts). The default spans every height: the plane of milestone 6.
struct HeightSpan {
    float low = -std::numeric_limits<float>::infinity();   ///< lowest y
    float high = std::numeric_limits<float>::infinity();   ///< highest y
    /// True if the two spans share a height.
    bool overlaps(const HeightSpan& other) const { return low <= other.high && other.low <= high; }
};

/// The circles of the plane sorted into square buckets (milestone 6, parts 3 and 6): collisions test
/// only neighbours, and the game asks who is in a circle, a cone or a rectangle. Filled once per tick
/// (separate_colliders() does it), then read.
///
/// Every query returns entities sorted by distance from the query's origin, then by identifier: the
/// same answer on every machine. An entity counts when its circle overlaps the area (for a cone or a
/// rectangle: its centre within the area grown by its radius).
///
/// Positions and sizes are in metres on the ground plane (x, z of a Transform, as plane_position()
/// gives). Entries are bucketed by their centre; a query widens its search by largest_radius(), so a
/// few huge circles make every query look at more buckets. Not thread-safe: the queries are `const`
/// but share an internal scratch buffer, so even concurrent queries need a lock or one hash per
/// thread.
class SpatialHash {
public:
    /// One circle of the hash, as passed to insert().
    struct Entry {
        entt::entity entity = entt::null;  ///< the entity it stands for (returned by the queries)
        glm::vec2 position{0.0f};          ///< the centre on the ground plane, in metres
        float radius = 0.0f;               ///< in metres
        std::uint32_t layer = 1;           ///< bits, matched against the `layers` mask of the queries
        HeightSpan heights;                ///< in a 3D level: from its feet to its top
    };

    /// An empty hash with square buckets of `bucket_size` metres (a value <= 0 is taken as 1). About
    /// the diameter of a typical collider is a good size.
    explicit SpatialHash(float bucket_size = 2.0f);

    /// Removes every entry. Keeps the buckets' memory, so refilling each tick does not reallocate.
    void clear();
    /// Adds a circle of `radius` metres centred on `position`, on the `layer` bits. Nothing checks
    /// that the entity is not already in: inserting it twice makes the queries return it twice.
    /// Amortised O(1).
    void insert(entt::entity entity, glm::vec2 position, float radius, std::uint32_t layer = 1, HeightSpan heights = {});
    /// The number of entries inserted since the last clear().
    std::size_t size() const { return entries_.size(); }
    /// The entries in insertion order; valid until the next insert() or clear().
    const std::vector<Entry>& entries() const { return entries_; }
    /// The largest radius inserted since the last clear() (0 when empty), in metres.
    float largest_radius() const { return largest_radius_; }

    /// Indices in entries() of the entries whose bucket touches the box [low, high] grown by
    /// largest_radius() (unsorted, each once): what the queries and the collisions start from. They
    /// may lie outside the box; the caller tests them. Clears `out` first.
    void candidates(glm::vec2 low, glm::vec2 high, std::vector<std::uint32_t>& out) const;

    /// The entities whose circle overlaps the circle of `radius` metres around `centre` (touching
    /// counts). `layers`: only entries whose layer has a bit in it. Allocates the returned vector.
    /// `heights`: only entries whose span overlaps it (a 3D level: who is on this floor); every query
    /// takes it.
    std::vector<entt::entity> query_circle(glm::vec2 centre, float radius, std::uint32_t layers = ~0u,
                                           HeightSpan heights = {}) const;
    /// A cone from `origin` towards `direction` (need not be unit; zero means +x), out to `range`
    /// metres, of half-angle given by its cosine (0.5: 60 degrees each side; -1: a full circle). An
    /// entity overlapping `origin` always counts. `layers` as for query_circle().
    std::vector<entt::entity> query_cone(glm::vec2 origin, glm::vec2 direction, float range, float cos_half_angle,
                                         std::uint32_t layers = ~0u, HeightSpan heights = {}) const;
    /// A rectangle from `origin` along `direction` (a sweep in front of a character): `length` metres
    /// ahead, `half_width` metres each side. `direction` need not be unit; zero means +x. The
    /// rectangle starts at `origin` (nothing behind it, except by an entity's own radius). `layers` as
    /// for query_circle().
    std::vector<entt::entity> query_rect(glm::vec2 origin, glm::vec2 direction, float length, float half_width,
                                         std::uint32_t layers = ~0u, HeightSpan heights = {}) const;

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
    mutable std::vector<std::uint32_t> scratch_;  ///< an entry is in one bucket only: no duplicates
};

}  // namespace moteur
