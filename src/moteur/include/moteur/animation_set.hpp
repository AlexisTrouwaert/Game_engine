#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/animation_graph.hpp"

namespace moteur {

/// An event of a clip (key "events" of a clip): a name the game receives as an AnimatorEvent when
/// playback crosses `time` (see AnimationSet for when it fires).
struct ClipEvent {
    /// Seconds from the start of the clip, >= 0, at most the clip's duration (checked when it plays).
    float time = 0.0f;
    std::string name;   ///< not empty; what AnimatorEvent::name carries
    /// Fires whatever the clip's weight (as long as it weighs something), not only from half of it.
    bool always = false;
};

/// What the file says about one clip (an entry of "clips"). A clip the file does not name has
/// these defaults.
struct ClipSettings {
    float ground_speed = 0.0f;  ///< metres (of the file) per second at x1, 0: none (does not move)
    int fade_ticks = -1;        ///< crossfade into this clip; -1: the file's default
    float phase = 0.0f;         ///< [0, 1): where this clip starts in a blend space's cycle
    std::vector<ClipEvent> events;  ///< sorted by time
};

/// A blend space (an entry of "blend_spaces"): clips placed on the move-speed axis.
struct BlendSpaceData {
    std::vector<std::string> clips;  ///< sorted by ground speed at load
    std::vector<float> positions;    ///< their ground speeds, ascending
};

/// A named place on the character (see BoneAttachment): a joint, and an offset from it (metres of
/// the file; rotation given in degrees, X then Y then Z). The game says "right_hand", not the joint
/// names of one model.
struct AttachPoint {
    std::string joint;  ///< the joint's name (SkeletonData::find), resolved against the skeleton when used
    glm::vec3 position{0.0f};  ///< from the joint, in the joint's space, metres of the file
    /// From the joint: the file's degrees about the fixed X, then Y, then Z axes, as a quaternion.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    /// The attach point -> joint matrix: translate(position) x rotate(rotation), no scale.
    glm::mat4 offset() const;
};

/// A joint mask (an entry of "masks"): the joints a layer moves, with a soft edge. Turned into one
/// weight per joint by Animator::mask_weights().
struct MaskData {
    std::string joint;  ///< this joint and every joint below it
    int ramp = 0;       ///< the first `ramp` joints down from it get 1/(ramp+1), 2/(ramp+1)...
};

/// The description of a character's skeletal animations (milestone 5, part 6): what the glTF file
/// does not say. A JSON file next to the game's data, in the spirit of the sprite animations'
/// animations.json:
///
/// ```
/// {
///   "version": 1,
///   "fade_ticks": 12,                     // default crossfade, in ticks
///   "clips": {
///     "Walking_A": { "ground_speed": 1.9,    // metres of the file per second, in place
///                    "events": [ { "time": 0.53, "name": "step_right" } ] },  // seconds of the file
///     "Running_A": { "phase": 0.5 },        // shifts the clip in a blend space (feet in step)
///     "1H_Melee_Attack_Chop": { "fade_ticks": 6,
///                               "events": [ { "time": 0.4, "name": "impact", "always": true } ] }
///   },
///   "blend_spaces": {
///     "locomotion": ["Idle", "Walking_A", "Running_A"]  // placed on their ground speeds
///   },
///   "masks": {
///     "upper_body": { "joint": "spine", "ramp": 2 }  // spine and below it, over 3 joints
///   },
///   "upper_mask": "upper_body",           // the mask of layer 1
///   "attach_points": {                    // where objects go on the character (part 8)
///     "right_hand": { "joint": "handslot.r", "position": [0, 0.033, 0], "rotation": [0, 180, 0] }
///   },
///   "graph": { ... }                      // the animation graph (milestone 7): see AnimationGraph
/// }
/// ```
///
/// Plain data, checked against no skeleton: the Animator resolves clips and joints when it plays.
/// A blend space's clips are placed on the move-speed axis at their ground speed: 0 for a clip
/// without one (an idle).
///
/// Events (part 7) fire when playback crosses their time, exactly once per pass, during the tick
/// (see Animator::advance). While clips are blended, only a clip that weighs half or more fires its
/// events (in a blend space, the clip that weighs most); an event marked "always" fires whatever the
/// weight, as long as the clip plays at all (an impact whose attack is still fading in); in a blend
/// space, only the clip that weighs most fires, "always" or not. On the upper layer, the weight is
/// the clip's times the layer's.
///
/// Plain data, immutable once parsed; usually loaded and hot-reloaded through the asset cache
/// (`Asset<AnimationSet>`), and shared by every Animator of the character.
class AnimationSet {
public:
    static constexpr int kVersion = 1;  ///< the only "version" read

    /// Parses the text of an animation set file. `source` names it in messages (and is kept: see
    /// source()). Throws std::runtime_error naming `source`: invalid JSON, a missing or wrong
    /// "version", a negative "fade_ticks", "ground_speed" or "ramp", a "phase" outside [0, 1), an
    /// event without name or with a negative time, an "upper_mask" that is not in "masks", an
    /// attach point whose position or rotation is not 3 numbers, a blend space with fewer than two
    /// clips or two at the same speed, or an invalid graph (AnimationGraph::parse()).
    static AnimationSet parse(std::string_view json_text, const std::string& source);

    /// The name given to parse(), usually the file's path.
    const std::string& source() const { return source_; }
    /// The file's "fade_ticks": the crossfade of a clip without its own, in ticks (0 if absent).
    int default_fade_ticks() const { return default_fade_ticks_; }

    /// The settings of `clip`; defaults if the file does not name it. The reference stays valid as
    /// long as the set.
    const ClipSettings& clip(const std::string& name) const;
    /// The crossfade into `clip`, in ticks: its own "fade_ticks", else default_fade_ticks().
    int fade_ticks(const std::string& clip) const;

    /// Every blend space, by name.
    const std::map<std::string, BlendSpaceData>& blend_spaces() const { return blend_spaces_; }
    /// The blend space `name`, null if none. Owned by the set.
    const BlendSpaceData* blend_space(const std::string& name) const;
    /// Every mask, by name.
    const std::map<std::string, MaskData>& masks() const { return masks_; }
    /// The mask `name`, null if none. Owned by the set.
    const MaskData* mask(const std::string& name) const;
    /// The name of the mask of layer 1 (key "upper_mask"); empty: layer 1 moves the whole body.
    const std::string& upper_mask() const { return upper_mask_; }
    /// Every attach point, by name.
    const std::map<std::string, AttachPoint>& attach_points() const { return attach_points_; }
    /// The attach point `name`, null if none. Owned by the set.
    const AttachPoint* attach_point(const std::string& name) const;
    /// The animation graph. Empty if the file has none (the game plays its clips itself).
    const AnimationGraph& graph() const { return graph_; }

private:
    std::string source_;
    int default_fade_ticks_ = 0;
    std::map<std::string, ClipSettings> clips_;
    std::map<std::string, BlendSpaceData> blend_spaces_;
    std::map<std::string, MaskData> masks_;
    std::string upper_mask_;
    std::map<std::string, AttachPoint> attach_points_;
    AnimationGraph graph_;
};

}  // namespace moteur
