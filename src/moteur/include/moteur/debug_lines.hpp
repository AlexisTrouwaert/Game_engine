#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/gpu_resource.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

// One end of a debug line, laid out exactly as it is sent to the GPU.
struct DebugLineVertex {
    glm::vec3 position;  // world, metres
    glm::vec4 color;     // linear, may exceed 1
};
static_assert(sizeof(DebugLineVertex) == 28, "DebugLineVertex is uploaded as is: keep it tightly packed");

// Lines to look at what the engine does (milestone 3, part 12): boxes, frustums, rays, axes.
// Plain CPU code, testable without a GPU. Each line is either hidden by the meshes in front of it
// (`on_top = false`) or drawn over everything.
class DebugLineBuffer {
public:
    void clear();
    void line(glm::vec3 from, glm::vec3 to, glm::vec4 color, bool on_top = false);
    // The 12 edges of a box.
    void box(const Aabb& box, glm::vec4 color, bool on_top = false);
    // The 12 edges of what a view-projection matrix sees (depth in [0, 1]): a camera, a shadow map.
    void frustum(const glm::mat4& view_projection, glm::vec4 color, bool on_top = false);
    // X in red, Y in green, Z in blue, `length` metres from `origin`.
    void axes(glm::vec3 origin, float length, bool on_top = true);
    // A circle in the plane perpendicular to `normal`, drawn with `segments` lines.
    void circle(glm::vec3 center, glm::vec3 normal, float radius, glm::vec4 color, int segments = 32, bool on_top = false);
    // Three circles, one per axis plane.
    void sphere(glm::vec3 center, float radius, glm::vec4 color, bool on_top = false);

    // Two vertices per line: those hidden by the meshes, and those drawn on top.
    const std::vector<DebugLineVertex>& depth_tested() const { return depth_tested_; }
    const std::vector<DebugLineVertex>& on_top() const { return on_top_; }
    std::size_t line_count() const { return (depth_tested_.size() + on_top_.size()) / 2; }

private:
    std::vector<DebugLineVertex> depth_tested_;
    std::vector<DebugLineVertex> on_top_;
};

// Draws a DebugLineBuffer in the "scene" pass, after the meshes and billboards, with the camera
// given to set_camera(), or else the one given to MeshRenderer::set_camera(). Lines are one pixel
// wide (SDL_GPU has no line width). Recorded again every frame, like everything else.
//
//   renderer.debug_lines().lines().box(bounds, {0, 1, 0, 1});
class DebugLineRenderer {
public:
    DebugLineRenderer(Renderer& renderer, SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format);

    void set_camera(const glm::mat4& view_projection) {
        view_projection_ = view_projection;
        has_camera_ = true;
    }
    DebugLineBuffer& lines() { return lines_; }
    bool has_work() const { return has_camera_ && lines_.line_count() > 0; }

private:
    friend class Renderer;  // drives prepare(), render() and clear(); gives the meshes' camera

    void prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats);
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    void clear() {
        lines_.clear();
        has_camera_ = false;
    }

    // For `samples` per pixel (MSAA): made again by the Renderer when the anti-aliasing changes.
    void create_pipelines(SDL_GPUSampleCount samples);

    Renderer& renderer_;
    SDL_GPUTextureFormat color_format_;
    SDL_GPUTextureFormat depth_format_;
    GpuShader vertex_shader_;
    GpuShader fragment_shader_;
    GpuGraphicsPipeline depth_tested_pipeline_;
    GpuGraphicsPipeline on_top_pipeline_;
    GpuBuffer vertex_buffer_;
    GpuTransferBuffer transfer_buffer_;
    std::size_t capacity_ = 0;  // vertices
    DebugLineBuffer lines_;
    glm::mat4 view_projection_{1.0f};
    bool has_camera_ = false;
};

}  // namespace moteur
