#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace moteur {

/// One corner of a billboard, laid out exactly as it is sent to the GPU.
struct BillboardVertex {
    /// World position, metres.
    ///@{
    float x, y, z;
    ///@}
    /// Texture coordinates, 0 to 1 across the texture.
    ///@{
    float u, v;
    ///@}
    /// Color, premultiplied, linear; may exceed 1 (glow); a = 0 adds light (see BillboardDesc::additive).
    ///@{
    float r, g, b, a;
    ///@}
    float soft;          ///< metres of fade before the surface behind (soft particles); 0: hard
};
static_assert(sizeof(BillboardVertex) == 40, "BillboardVertex is uploaded as is: keep it tightly packed");

/// Which way a billboard turns.
enum class BillboardFacing {
    Camera,   ///< faces the camera entirely (sparks, glows, smoke)
    Upright,  ///< stays vertical, turns only around the vertical axis (a character or a tree on a card)
    Flat,     ///< lies flat, in a horizontal plane (a puddle, a magic circle): the texture's top towards -z
};

/// What the game asks for: a textured rectangle in the world, turned towards the camera.
struct BillboardDesc {
    const void* texture = nullptr;  ///< identity of the texture: billboards sharing it can be drawn together
    glm::vec3 center{0.0f};         ///< world position of the middle of the rectangle
    glm::vec2 size{1.0f};           ///< metres
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};  ///< u0, v0, u1, v1
    glm::vec4 color{1.0f};          ///< linear, rgb may exceed 1; a: opacity
    /// Adds its light to what is behind instead of covering it (fire, sparks); its alpha then only
    /// scales its color.
    bool additive = false;
    BillboardFacing facing = BillboardFacing::Camera;  ///< which way it turns
    float rotation = 0.0f;          ///< radians, in its own plane (smoke that turns)
    float soft = 0.0f;              ///< metres over which it fades as it nears the surface behind it; 0: hard
};

/// Where the camera is and which way it looks, for turning billboards (see Camera3D). The four
/// vectors are in world space; the three directions are unit length.
struct BillboardView {
    glm::vec3 eye{0.0f};                    ///< camera position, metres
    glm::vec3 right{1.0f, 0.0f, 0.0f};      ///< the screen's right
    glm::vec3 up{0.0f, 1.0f, 0.0f};         ///< the screen's up
    glm::vec3 forward{0.0f, 0.0f, -1.0f};   ///< the direction the camera looks in
};

/// Consecutive billboards (after sorting) that share a texture: one draw call.
struct BillboardRun {
    const void* texture;  ///< the BillboardDesc::texture of every billboard of the run
    std::uint32_t first;  ///< first billboard, in draw order; its vertices start at `4 * first`
    std::uint32_t count;  ///< number of billboards, at most kMaxQuadsPerRun
};

/// Turns the billboards of a frame into vertices and draw calls. Plain CPU code, testable without a
/// GPU (like SpriteBatcher). Covering billboards come first, sorted from the farthest to the nearest
/// along the view (they are transparent, so the order matters more than the number of draw calls;
/// ties keep the recording order). Additive ones come after them, grouped by texture (in the order
/// the textures first appear, then the recording order): adding light does not depend on the order,
/// so thousands of sparks take a few draw calls. A run ends whenever the texture changes.
class BillboardBatcher {
public:
    /// A draw call uses 16-bit indices: at most 16 384 quads.
    static constexpr std::uint32_t kMaxQuadsPerRun = 16384;

    /// Starts a new frame: forgets the billboards, vertices and runs of the previous one (keeping
    /// the memory).
    void begin();
    /// Records a billboard (copied).
    void add(const BillboardDesc& billboard);
    /// Sorts the billboards recorded since begin() for `view`, builds their vertices and splits
    /// them into runs. O(n log n), plus O(n x distinct textures) to rank the textures.
    void finish(const BillboardView& view);

    /// Number of billboards recorded since begin().
    std::size_t count() const { return billboards_.size(); }
    /// The vertices built by finish(): 4 per billboard, in the order of corners(), in draw order.
    /// Valid until the next begin() or finish().
    const std::vector<BillboardVertex>& vertices() const { return vertices_; }
    /// The draw calls built by finish(), in draw order. Valid until the next begin() or finish().
    const std::vector<BillboardRun>& runs() const { return runs_; }

    /// The four corners of a billboard, in world space: top left, top right, bottom right, bottom
    /// left, as seen with its texture upright. Writes `out[0]` to `out[3]`.
    static void corners(const BillboardDesc& billboard, const BillboardView& view, glm::vec3 out[4]);

private:
    std::vector<BillboardDesc> billboards_;
    std::vector<std::uint32_t> order_;
    std::vector<std::uint32_t> texture_rank_;
    std::vector<float> depth_;
    std::vector<BillboardVertex> vertices_;
    std::vector<BillboardRun> runs_;
};

}  // namespace moteur
