#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace moteur {

class TextureAtlas;

enum class PlayMode {
    Once,      // plays to the last frame and stays on it
    Loop,      // 0 1 2 3 0 1 2 3 ...
    PingPong,  // 0 1 2 3 2 1 0 1 ... (the end frames are not repeated)
};

struct AnimationFrame {
    std::string region;  // sprite name in an atlas
    int ticks = 1;       // how long the frame shows, in fixed simulation steps
};

// Something that happens when playback reaches a frame: a hit lands, a foot touches the ground.
struct AnimationEvent {
    int frame = 0;  // index in the clip's frames
    std::string name;
};

// A sequence of frames. Durations are whole simulation ticks, so the same clip plays identically
// on every machine, whatever the frame rate.
class AnimationClip {
public:
    // Throws std::invalid_argument, naming the clip, if there are no frames, a frame lasts less
    // than one tick, or an event refers to a frame that does not exist.
    AnimationClip(std::string name, std::vector<AnimationFrame> frames, PlayMode mode = PlayMode::Loop,
                  std::vector<AnimationEvent> events = {});

    const std::string& name() const { return name_; }
    const std::vector<AnimationFrame>& frames() const { return frames_; }
    PlayMode mode() const { return mode_; }
    const std::vector<AnimationEvent>& events() const { return events_; }

    // One cycle, in ticks: the whole clip (Once, Loop), or there and back (PingPong).
    int cycle_ticks() const { return cycle_ticks_; }

    // A cycle is a list of steps, each showing one frame. Once and Loop have one step per frame;
    // PingPong adds the frames on the way back.
    int step_count() const { return static_cast<int>(step_starts_.size()); }
    int step_frame(int step) const;
    int step_start(int step) const { return step_starts_[static_cast<std::size_t>(step)]; }

private:
    std::string name_;
    std::vector<AnimationFrame> frames_;
    PlayMode mode_;
    std::vector<AnimationEvent> events_;
    std::vector<int> step_starts_;  // in ticks, from the start of the cycle
    int cycle_ticks_ = 0;
};

// Plays one clip: the state of one animated thing. Plain data with no link to rendering, so it
// can become an ECS component. The clip must outlive the player.
//
//   player.play(library.clip("attack"));
//   player.set_speed(1.5);                      // attack speed +50 %
//   events.clear();
//   player.advance(1, &events);                 // once per simulation tick
//   for (const AnimationEvent* e : events) ...  // "hit" -> apply damage
//   sprites.draw(atlas.region(player.region()), anchor);
class AnimationPlayer {
public:
    // Speed is stored in thousandths: 1000 means x1. Integers keep playback exact on every OS.
    static constexpr std::int64_t kSpeedOne = 1000;

    AnimationPlayer() = default;
    explicit AnimationPlayer(const AnimationClip& clip) { play(clip); }

    // Starts the clip from its first frame. The speed is kept.
    void play(const AnimationClip& clip);
    // Starts the same clip again, as if play() had just been called.
    void restart();

    // Multiplier of the playback speed, rounded to a thousandth. 0 pauses. Throws
    // std::invalid_argument if negative.
    void set_speed(double multiplier);
    double speed() const { return static_cast<double>(speed_) / static_cast<double>(kSpeedOne); }

    // Moves the playback forward by `ticks` simulation steps (scaled by the speed). The events of
    // every frame reached are appended to `fired`, in order, each exactly once, even when a big
    // step skips over frames or whole loops. The first frame counts as reached on the first
    // advance after play(). `fired` is not cleared.
    void advance(int ticks, std::vector<const AnimationEvent*>* fired = nullptr);

    const AnimationClip* clip() const { return clip_; }
    // The frame showing now. Must not be called without a clip.
    int frame_index() const;
    const std::string& region() const;
    // Only a Once clip finishes: it stays on its last frame.
    bool finished() const;

    // Time since play(), in thousandths of a tick. Can be copied to another player to put it
    // at the same point, for example to vary the phase of many identical animations.
    std::int64_t time() const { return time_; }
    void set_time(std::int64_t time);

private:
    std::int64_t cycle_length() const { return static_cast<std::int64_t>(clip_->cycle_ticks()) * kSpeedOne; }
    int step_at(std::int64_t time) const;

    const AnimationClip* clip_ = nullptr;
    std::int64_t time_ = 0;
    std::int64_t speed_ = kSpeedOne;
    bool started_ = false;  // false until the first frame's events have been fired
};

// The clips of a JSON file:
//
//   { "version": 1,
//     "clips": {
//       "walk": { "mode": "loop", "frame_ticks": 6,
//                 "frames": ["walk_00", "walk_01", { "region": "walk_02", "ticks": 12 }],
//                 "events": [{ "frame": 1, "name": "step" }] } } }
//
// "mode" is "once", "loop" (default) or "ping_pong". "frame_ticks" (default 1) is the duration
// of the frames given by name only.
class AnimationLibrary {
public:
    // Throws std::runtime_error naming `source` if the text is not a valid description.
    static AnimationLibrary parse(std::string_view json_text, const std::string& source);
    static AnimationLibrary load(const std::string& json_path);

    // The reference stays valid as long as the library lives, even after a move. Throws
    // std::runtime_error naming the file and the clip if there is no such clip.
    const AnimationClip& clip(const std::string& name) const;
    bool contains(const std::string& name) const { return clips_.count(name) != 0; }
    // All clip names, sorted.
    std::vector<std::string> names() const;

    // Throws std::runtime_error naming the clip and the sprite if a frame uses a sprite the atlas
    // does not have. Call it once after loading, rather than failing in the middle of a game.
    void check_regions(const TextureAtlas& atlas) const;

private:
    std::string source_;
    std::map<std::string, AnimationClip> clips_;
};

}  // namespace moteur
