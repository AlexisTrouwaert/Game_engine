#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace moteur {

// Dear ImGui, for debug and tool windows: menus, buttons, sliders. Drawn on top of the sprites,
// in window pixels, whatever camera the sprites use.
//
// The Application drives it: events go to process_event(), new_frame() runs just before
// Game::render(), and the Renderer draws the result at the end of the frame. So a game only has to
// call ImGui:: functions from its render():
//
//   if (ImGui::Button("Restart")) restart();
//
// It is created only when ApplicationConfig::debug_ui is set; see Renderer::debug_ui().
class DebugUi {
public:
    // `font_path`: a TrueType font with the accented characters of French (ImGui's built-in font
    // has only ASCII); empty keeps the built-in font. Throws std::runtime_error on failure.
    DebugUi(SDL_Window* window, SDL_GPUDevice* device, SDL_GPUTextureFormat format, const std::string& font_path,
            float font_size);
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    void process_event(const SDL_Event& event);

    // True when ImGui uses this event itself (the mouse is over one of its windows, or a text
    // field has the keyboard): the game should then ignore it.
    bool captures(const SDL_Event& event) const;

    void new_frame();

    // Where ImGui keeps the windows' places (and what DebugTools adds): imgui.ini in the player's
    // preferences, never next to the executable. Empty: nothing kept (the default). Before the
    // first frame, which reads it.
    void set_settings_file(std::string path);

    // Called by the Renderer: prepare() before the render pass (uploads), render() inside it.
    void prepare(SDL_GPUCommandBuffer* commands);
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass);

private:
    std::string settings_file_;   // ImGui keeps a pointer to it
    bool frame_started_ = false;  // new_frame() called, Render() not yet
    bool prepared_ = false;       // prepare() done this frame, render() not yet
};

}  // namespace moteur
