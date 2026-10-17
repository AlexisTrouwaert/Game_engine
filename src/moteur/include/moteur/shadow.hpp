#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "moteur/camera3d.hpp"

namespace moteur {

/// Where the sun's shadow map looks (milestone 3, part 8): pure math, unit-tested.
///
/// The map covers what the camera can see of the playing field: the four corner rays of the view
/// are cut by the ground (y = 0) and by a ceiling (y = max_height), and the resulting points are
/// wrapped in a sphere. A sphere keeps the map the same size whatever the camera does except zoom,
/// and its centre is then snapped to the map's texel grid: a camera that moves a little moves the
/// map by whole texels, so shadow edges do not shimmer. The sphere's radius is rounded up to a
/// whole metre, so that small changes of the view do not resize the map.
struct ShadowFrame {
    glm::mat4 view_projection{1.0f};  ///< world -> the sun's clip space (depth in [0, 1])
    glm::vec3 center{0.0f};           ///< of the covered sphere, after snapping
    float radius = 0.0f;              ///< of the covered sphere, in metres
    float texel_size = 0.0f;          ///< metres of world per shadow map texel
};

/// What fit_sun_shadow() fits the sun's shadow map to.
struct ShadowSettings {
    /// Texels per side of the map (for the snapping and ShadowFrame::texel_size); below 1 counts as 1.
    int resolution = 2048;
    float max_height = 6.0f;     ///< metres above the ground that can cast shadows into view
    float max_distance = 80.0f;  ///< metres: how far a corner ray is followed when it misses the ground or the ceiling
    float depth_margin = 40.0f;  ///< metres behind the sphere, towards the sun, where casters still count
};

/// Places the sun's shadow map for this frame (see ShadowFrame). `camera_view_projection`: the
/// camera of the frame (clip depth in [0, 1], perspective or orthographic). `to_sun`: direction from
/// the ground towards the sun (need not be normalized; a zero vector counts as straight up).
ShadowFrame fit_sun_shadow(const glm::mat4& camera_view_projection, glm::vec3 to_sun,
                           const ShadowSettings& settings = {});

/// Point light shadows (milestone 3, part 8 bis): pure math and bookkeeping, unit-tested.
///
/// A point light sees in every direction: its shadow is six square views, one per face of a cube
/// around it (+X, -X, +Y, -Y, +Z, -Z), each a tile of a shared depth texture (the atlas). What is
/// stored is not the depth of the projection but the distance to the light divided by its range:
/// a depth bias is then a plain length, the same near the light and far from it.
struct PointShadowFaces {
    glm::mat4 view_projection[6];  ///< world -> the clip space of each face (depth in [0, 1])
    /// Metres of world covered by one texel, per metre of distance from the light.
    float texel_size_per_metre = 0.0f;
};

/// The faces are slightly wider than 90 degrees, so that a point near the edge of a face still has
/// a few texels of that face around it (the filtered lookup reads 3 x 3 texels).
constexpr int kPointShadowMarginTexels = 2;

/// The six face views of a light at `position` (world, metres) reaching `range` metres, for tiles
/// of `tile_size` texels per side (below 8 counts as 8). Each face is a perspective view from
/// the light, its near plane at `near_plane` metres (at least 0.02; 0 or less: automatic,
/// max(0.02, range / 200)) and its far plane at `range`: what is nearer than the near plane casts
/// no shadow (see PointLight::shadow_near).
PointShadowFaces point_shadow_faces(glm::vec3 position, float range, int tile_size, float near_plane = 0.0f);

/// The face a direction from the light falls into: its largest component (the shader does the same).
/// Returns 0 to 5 in the order of PointShadowFaces (+X, -X, +Y, -Y, +Z, -Z); ties go to the
/// earlier axis.
int point_shadow_face(glm::vec3 direction);

/// A light that may cast shadows this frame.
struct PointShadowCandidate {
    glm::vec3 position{0.0f};  ///< world, metres
    float range = 0.0f;        ///< metres; 0 or less never gets a shadow
};

/// The lights that get a shadow: those whose sphere is in view, nearest to `focus` first (ties:
/// the first given), at most `budget` (a negative budget counts as 0). Returns indices into
/// `candidates`.
std::vector<int> select_point_shadows(const std::vector<PointShadowCandidate>& candidates, const Frustum& view,
                                      glm::vec3 focus, int budget);

/// Which tile row of the atlas holds which light, and whether its shadow must be drawn again. A
/// light is known by its signature: a hash of everything its shadow depends on (its position and
/// range, and the shadow casters inside its sphere). While a signature does not change, its tiles
/// are kept, even across frames where the light was not selected.
class PointShadowSlots {
public:
    /// Where one light's shadow goes this frame.
    struct Assignment {
        int slot = 0;         ///< the tile row of the atlas, in [0, count())
        bool render = false;  ///< the tiles do not hold this shadow yet
    };

    /// `count` rows (a negative count counts as 0). Forgets everything (the atlas was recreated).
    void reset(int count);
    /// The number of rows given to reset(); 0 before.
    int count() const { return static_cast<int>(slots_.size()); }

    /// One assignment per signature, in the same order; at most count() signatures: those beyond
    /// are dropped (the result is shorter). Signatures already in a slot keep it; the others take
    /// a slot that never held a shadow first, then any slot not kept this frame.
    std::vector<Assignment> assign(const std::vector<std::uint64_t>& signatures);

private:
    struct Slot {
        bool valid = false;
        std::uint64_t signature = 0;
    };
    std::vector<Slot> slots_;
};

}  // namespace moteur
