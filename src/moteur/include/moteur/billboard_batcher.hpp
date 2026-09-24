#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace moteur {

// One corner of a billboard, laid out exactly as it is sent to the GPU.
struct BillboardVertex {
    float x, y, z;       // world position, metres
    float u, v;
    float r, g, b, a;    // premultiplied, linear; may exceed 1 (glow); a = 0 adds light (see BillboardDesc)
};
static_assert(sizeof(BillboardVertex) == 36, "BillboardVertex is uploaded as is: keep it tightly packed");

// Which way a billboard turns.
enum class BillboardFacing {
    Camera,   // faces the camera entirely (sparks, glows, smoke)
    Upright,  // stays vertical, turns only around the vertical axis (a character or a tree on a card)
};

// What the game asks for: a textured rectangle in the world, turned towards the camera.
struct BillboardDesc {
    const void* texture = nullptr;  // identity of the texture: billboards sharing it can be drawn together
    glm::vec3 center{0.0f};         // world position of the middle of the rectangle
    glm::vec2 size{1.0f};           // metres
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};  // u0, v0, u1, v1
    glm::vec4 color{1.0f};          // linear, rgb may exceed 1; a: opacity
    bool additive = false;          // adds its light to what is behind instead of covering it (fire, sparks)
    BillboardFacing facing = BillboardFacing::Camera;
};

// Where the camera is and which way it looks, for turning billboards (see Camera3D).
struct BillboardView {
    glm::vec3 eye{0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
};

// Consecutive billboards (after sorting) that share a texture: one draw call.
struct BillboardRun {
    const void* texture;
    std::uint32_t first;  // first billboard
    std::uint32_t count;
};

// Turns the billboards of a frame into vertices and draw calls. Plain CPU code, testable without a
// GPU (like SpriteBatcher). Billboards are transparent, so they are sorted from the farthest to the
// nearest along the view (ties keep the recording order), and a run ends whenever the texture
// changes: the order matters more than the number of draw calls.
class BillboardBatcher {
public:
    // A draw call uses 16-bit indices: at most 16 384 quads.
    static constexpr std::uint32_t kMaxQuadsPerRun = 16384;

    void begin();
    void add(const BillboardDesc& billboard);
    void finish(const BillboardView& view);

    std::size_t count() const { return billboards_.size(); }
    const std::vector<BillboardVertex>& vertices() const { return vertices_; }
    const std::vector<BillboardRun>& runs() const { return runs_; }

    // The four corners of a billboard: top left, top right, bottom right, bottom left.
    static void corners(const BillboardDesc& billboard, const BillboardView& view, glm::vec3 out[4]);

private:
    std::vector<BillboardDesc> billboards_;
    std::vector<std::uint32_t> order_;
    std::vector<float> depth_;
    std::vector<BillboardVertex> vertices_;
    std::vector<BillboardRun> runs_;
};

}  // namespace moteur
