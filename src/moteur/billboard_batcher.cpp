#include "moteur/billboard_batcher.hpp"

#include <algorithm>
#include <numeric>

namespace moteur {

void BillboardBatcher::begin() {
    billboards_.clear();
    vertices_.clear();
    runs_.clear();
}

void BillboardBatcher::add(const BillboardDesc& billboard) {
    billboards_.push_back(billboard);
}

void BillboardBatcher::corners(const BillboardDesc& billboard, const BillboardView& view, glm::vec3 out[4]) {
    glm::vec3 right = view.right;
    glm::vec3 up = view.up;
    if (billboard.facing == BillboardFacing::Upright) {
        // The camera's right, laid flat: the card turns around the vertical axis only.
        const glm::vec3 flat(view.right.x, 0.0f, view.right.z);
        right = glm::dot(flat, flat) > 1e-8f ? glm::normalize(flat) : glm::vec3(1.0f, 0.0f, 0.0f);
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    const glm::vec3 half_right = right * (billboard.size.x * 0.5f);
    const glm::vec3 half_up = up * (billboard.size.y * 0.5f);
    out[0] = billboard.center - half_right + half_up;
    out[1] = billboard.center + half_right + half_up;
    out[2] = billboard.center + half_right - half_up;
    out[3] = billboard.center - half_right - half_up;
}

void BillboardBatcher::finish(const BillboardView& view) {
    vertices_.clear();
    runs_.clear();
    const std::size_t count = billboards_.size();
    if (count == 0) {
        return;
    }

    // Farthest first; a stable sort keeps the recording order between equal depths, so the result
    // is the same on every run.
    depth_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        depth_[i] = glm::dot(billboards_[i].center - view.eye, view.forward);
    }
    order_.resize(count);
    std::iota(order_.begin(), order_.end(), 0u);
    std::stable_sort(order_.begin(), order_.end(), [this](std::uint32_t a, std::uint32_t b) { return depth_[a] > depth_[b]; });

    vertices_.reserve(count * 4);
    for (std::size_t k = 0; k < count; ++k) {
        const BillboardDesc& billboard = billboards_[order_[k]];
        glm::vec3 corner[4];
        corners(billboard, view, corner);
        // Premultiplied: the color is scaled by the opacity; an additive billboard keeps its color
        // but no alpha, so the blending ("source + background x (1 - alpha)") only adds it.
        const glm::vec4 c = billboard.color;
        const float r = c.r * c.a, g = c.g * c.a, b = c.b * c.a;
        const float a = billboard.additive ? 0.0f : c.a;
        const glm::vec4 uv = billboard.uv_rect;
        const float us[4] = {uv.x, uv.z, uv.z, uv.x};
        const float vs[4] = {uv.y, uv.y, uv.w, uv.w};
        for (int i = 0; i < 4; ++i) {
            vertices_.push_back({corner[i].x, corner[i].y, corner[i].z, us[i], vs[i], r, g, b, a});
        }

        const auto index = static_cast<std::uint32_t>(k);
        if (runs_.empty() || runs_.back().texture != billboard.texture || runs_.back().count >= kMaxQuadsPerRun) {
            runs_.push_back({billboard.texture, index, 1});
        } else {
            ++runs_.back().count;
        }
    }
}

}  // namespace moteur
