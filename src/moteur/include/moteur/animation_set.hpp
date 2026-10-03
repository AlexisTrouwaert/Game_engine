#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace moteur {

// The description of a character's skeletal animations (milestone 5, part 6): what the glTF file
// does not say. A JSON file next to the game's data, in the spirit of the sprite animations'
// animations.json:
//
//   {
//     "version": 1,
//     "fade_ticks": 12,                     // default crossfade, in ticks
//     "clips": {
//       "Walking_A": { "ground_speed": 1.9,    // metres of the file per second, in place
//                      "events": [ { "time": 0.53, "name": "step_right" } ] },  // seconds of the file
//       "Running_A": { "phase": 0.5 },        // shifts the clip in a blend space (feet in step)
//       "1H_Melee_Attack_Chop": { "fade_ticks": 6,
//                                 "events": [ { "time": 0.4, "name": "impact", "always": true } ] }
//     },
//     "blend_spaces": {
//       "locomotion": ["Idle", "Walking_A", "Running_A"]  // placed on their ground speeds
//     },
//     "masks": {
//       "upper_body": { "joint": "spine", "ramp": 2 }  // spine and below it, over 3 joints
//     },
//     "upper_mask": "upper_body",           // the mask of layer 1
//     "attach_points": {                    // where objects go on the character (part 8)
//       "right_hand": { "joint": "handslot.r", "position": [0, 0.033, 0], "rotation": [0, 180, 0] }
//     }
//   }
//
// Plain data, checked against no skeleton: the Animator resolves clips and joints when it plays.
// A blend space's clips are placed on the move-speed axis at their ground speed: 0 for a clip
// without one (an idle).
//
// Events (part 7) fire when playback crosses their time, exactly once per pass, during the tick
// (see Animator::advance). While clips are blended, only a clip that weighs half or more fires its
// events (in a blend space, the clip that weighs most); an event marked "always" fires whatever the
// weight, as long as the clip plays at all (an impact whose attack is still fading in).
struct ClipEvent {
    float time = 0.0f;  // seconds from the start of the clip
    std::string name;
    bool always = false;
};

struct ClipSettings {
    float ground_speed = 0.0f;  // metres (of the file) per second at x1, 0: none (does not move)
    int fade_ticks = -1;        // crossfade into this clip; -1: the file's default
    float phase = 0.0f;         // [0, 1): where this clip starts in a blend space's cycle
    std::vector<ClipEvent> events;  // sorted by time
};

struct BlendSpaceData {
    std::vector<std::string> clips;  // sorted by ground speed at load
    std::vector<float> positions;    // their ground speeds, ascending
};

// A named place on the character (see BoneAttachment): a joint, and an offset from it (metres of
// the file; rotation given in degrees, X then Y then Z). The game says "right_hand", not the joint
// names of one model.
struct AttachPoint {
    std::string joint;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    glm::mat4 offset() const;
};

struct MaskData {
    std::string joint;  // this joint and every joint below it
    int ramp = 0;       // the first `ramp` joints down from it get 1/(ramp+1), 2/(ramp+1)...
};

class AnimationSet {
public:
    static constexpr int kVersion = 1;

    // Throws std::runtime_error naming `source`: invalid JSON, unknown version, unknown mask,
    // blend space with fewer than two clips or two at the same speed, negative values.
    static AnimationSet parse(std::string_view json_text, const std::string& source);

    const std::string& source() const { return source_; }
    int default_fade_ticks() const { return default_fade_ticks_; }

    // The settings of `clip`; defaults if the file does not name it.
    const ClipSettings& clip(const std::string& name) const;
    // The crossfade into `clip`, in ticks.
    int fade_ticks(const std::string& clip) const;

    const std::map<std::string, BlendSpaceData>& blend_spaces() const { return blend_spaces_; }
    const BlendSpaceData* blend_space(const std::string& name) const;
    const std::map<std::string, MaskData>& masks() const { return masks_; }
    const MaskData* mask(const std::string& name) const;
    const std::string& upper_mask() const { return upper_mask_; }
    const std::map<std::string, AttachPoint>& attach_points() const { return attach_points_; }
    const AttachPoint* attach_point(const std::string& name) const;

private:
    std::string source_;
    int default_fade_ticks_ = 0;
    std::map<std::string, ClipSettings> clips_;
    std::map<std::string, BlendSpaceData> blend_spaces_;
    std::map<std::string, MaskData> masks_;
    std::string upper_mask_;
    std::map<std::string, AttachPoint> attach_points_;
};

}  // namespace moteur
