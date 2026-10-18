#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "moteur/aabb.hpp"
#include "moteur/gpu_resource.hpp"
#include "moteur/image.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

class MeshRenderer;
class World;
struct Environment;

// The thumbnails of the editor's asset library (milestone 9, part 10), drawn by the engine's own
// mesh renderer: the asset as the game shows it (its materials, its lights), not an icon. Off
// screen, into textures of their own; kept on disk, so that a library opens without drawing them
// again.

/// Draws thumbnails off screen: a second MeshRenderer into targets of its own, tone mapped as the
/// game's image (PBR Neutral, sRGB), with a transparent background. Main thread, between two frames
/// (not inside begin_frame() and end_frame()): it submits its own GPU work and waits for it.
class ThumbnailRenderer {
public:
    /// Thumbnails of `size` x `size` pixels (drawn at twice that, then averaged: smooth edges).
    explicit ThumbnailRenderer(Renderer& renderer, int size = 128);
    ~ThumbnailRenderer();
    ThumbnailRenderer(const ThumbnailRenderer&) = delete;
    ThumbnailRenderer& operator=(const ThumbnailRenderer&) = delete;

    /// The size of the thumbnails, in pixels.
    int size() const { return size_; }
    /// Draws what `world` holds (an asset instantiated in a world of its own) around `bounds`, seen
    /// from the front, to the side and above, lit by a sun and `sky` (may be null). Throws
    /// std::runtime_error when the GPU fails.
    Image render(World& world, const Aabb& bounds, const Environment* sky);

private:
    Renderer& renderer_;
    int size_ = 128;
    std::unique_ptr<MeshRenderer> meshes_;
    GpuTexture color_;
    GpuTexture distance_;
    GpuTexture depth_;
    GpuTransferBuffer download_;
};

/// Thumbnails kept: a PNG per asset in a folder (named after the asset's key and its fingerprint:
/// an asset changed gets a new one, the old one is removed), and their textures once read.
class ThumbnailCache {
public:
    /// Kept in `directory` (made when needed), textures made with `renderer`.
    ThumbnailCache(Renderer& renderer, std::string directory);

    /// The thumbnail of `key` as it is at `fingerprint`, from memory or from the disk; null when there
    /// is none yet (make it, then store()).
    const Texture* find(const std::string& key, std::uint64_t fingerprint);
    /// Keeps `image` as the thumbnail of `key` at `fingerprint`: written to the disk (a failure is
    /// logged, the texture is still made) and made a texture. Returns it.
    const Texture* store(const std::string& key, std::uint64_t fingerprint, const Image& image);
    /// The folder.
    const std::string& directory() const { return directory_; }
    /// Forgets the textures (the files stay).
    void clear() { textures_.clear(); }

private:
    struct Entry {
        std::uint64_t fingerprint = 0;
        Texture texture;
    };
    std::string file_name(const std::string& key, std::uint64_t fingerprint) const;

    Renderer& renderer_;
    std::string directory_;
    std::unordered_map<std::string, Entry> textures_;
};

}  // namespace moteur
