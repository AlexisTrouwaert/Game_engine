#include "moteur/spatial_hash.hpp"

#include <algorithm>
#include <cmath>

namespace moteur {

SpatialHash::SpatialHash(float bucket_size) : bucket_size_(bucket_size > 0.0f ? bucket_size : 1.0f) {}

void SpatialHash::clear() {
    entries_.clear();
    for (auto& [key, bucket] : buckets_) {
        bucket.clear();  // keeps the memory from tick to tick
    }
    largest_radius_ = 0.0f;
}

glm::ivec2 SpatialHash::bucket(glm::vec2 p) const {
    return {static_cast<int>(std::floor(p.x / bucket_size_)), static_cast<int>(std::floor(p.y / bucket_size_))};
}

void SpatialHash::insert(entt::entity entity, glm::vec2 position, float radius, std::uint32_t layer) {
    const auto index = static_cast<std::uint32_t>(entries_.size());
    entries_.push_back({entity, position, radius, layer});
    largest_radius_ = std::max(largest_radius_, radius);
    const glm::ivec2 b = bucket(position);
    buckets_[key(b.x, b.y)].push_back(index);
}

void SpatialHash::candidates(glm::vec2 low, glm::vec2 high, std::vector<std::uint32_t>& out) const {
    out.clear();
    // Entries are bucketed by their centre: widen by the largest radius to find those that reach in.
    const glm::ivec2 from = bucket(low - glm::vec2(largest_radius_));
    const glm::ivec2 to = bucket(high + glm::vec2(largest_radius_));
    for (int y = from.y; y <= to.y; ++y) {
        for (int x = from.x; x <= to.x; ++x) {
            const auto found = buckets_.find(key(x, y));
            if (found != buckets_.end()) {
                out.insert(out.end(), found->second.begin(), found->second.end());
            }
        }
    }
}

std::vector<entt::entity> SpatialHash::sorted(std::vector<std::pair<float, entt::entity>>& found) const {
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : entt::to_integral(a.second) < entt::to_integral(b.second);
    });
    std::vector<entt::entity> result;
    result.reserve(found.size());
    for (const auto& [distance, entity] : found) {
        result.push_back(entity);
    }
    return result;
}

std::vector<entt::entity> SpatialHash::query_circle(glm::vec2 centre, float radius, std::uint32_t layers) const {
    candidates(centre - glm::vec2(radius), centre + glm::vec2(radius), scratch_);
    std::vector<std::pair<float, entt::entity>> found;
    for (const std::uint32_t i : scratch_) {
        const Entry& e = entries_[i];
        const glm::vec2 d = e.position - centre;
        const float reach = radius + e.radius;
        const float distance2 = glm::dot(d, d);
        if ((e.layer & layers) != 0 && distance2 <= reach * reach) {
            found.emplace_back(distance2, e.entity);
        }
    }
    return sorted(found);
}

std::vector<entt::entity> SpatialHash::query_cone(glm::vec2 origin, glm::vec2 direction, float range,
                                                  float cos_half_angle, std::uint32_t layers) const {
    const float length = std::sqrt(glm::dot(direction, direction));
    const glm::vec2 axis = length > 0.0f ? direction / length : glm::vec2(1.0f, 0.0f);
    candidates(origin - glm::vec2(range), origin + glm::vec2(range), scratch_);
    std::vector<std::pair<float, entt::entity>> found;
    for (const std::uint32_t i : scratch_) {
        const Entry& e = entries_[i];
        if ((e.layer & layers) == 0) {
            continue;
        }
        const glm::vec2 d = e.position - origin;
        const float distance2 = glm::dot(d, d);
        const float reach = range + e.radius;
        if (distance2 > reach * reach) {
            continue;
        }
        const float distance = std::sqrt(distance2);
        // Inside the cone if its centre is, or if it overlaps the origin; the radius widens the
        // test by about radius / distance (no trigonometry: compared on the cosine side).
        bool inside = distance <= e.radius;
        if (!inside) {
            const float cosine = glm::dot(d, axis) / distance;
            const float slack = e.radius / distance;
            inside = cosine + slack >= cos_half_angle;
        }
        if (inside) {
            found.emplace_back(distance2, e.entity);
        }
    }
    return sorted(found);
}

std::vector<entt::entity> SpatialHash::query_rect(glm::vec2 origin, glm::vec2 direction, float length, float half_width,
                                                  std::uint32_t layers) const {
    const float norm = std::sqrt(glm::dot(direction, direction));
    const glm::vec2 axis = norm > 0.0f ? direction / norm : glm::vec2(1.0f, 0.0f);
    const glm::vec2 side(-axis.y, axis.x);
    const float extent = length + half_width;
    candidates(origin - glm::vec2(extent), origin + glm::vec2(extent), scratch_);
    std::vector<std::pair<float, entt::entity>> found;
    for (const std::uint32_t i : scratch_) {
        const Entry& e = entries_[i];
        if ((e.layer & layers) == 0) {
            continue;
        }
        const glm::vec2 d = e.position - origin;
        const float along = glm::dot(d, axis);
        const float across = glm::dot(d, side);
        if (along >= -e.radius && along <= length + e.radius && std::abs(across) <= half_width + e.radius) {
            found.emplace_back(glm::dot(d, d), e.entity);
        }
    }
    return sorted(found);
}

}  // namespace moteur
