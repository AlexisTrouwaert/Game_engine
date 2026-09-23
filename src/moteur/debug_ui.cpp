#include "moteur/debug_ui.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <stdexcept>

namespace moteur {

DebugUi::DebugUi(SDL_Window* window, SDL_GPUDevice* device, SDL_GPUTextureFormat format, const std::string& font_path,
                 float font_size) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no imgui.ini next to the executable: the layout is set by the code
    ImGui::StyleColorsDark();

    if (!font_path.empty()) {
        // Glyphs are rasterized on demand (ImGui 1.92+), so every accented character works.
        if (io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_size) == nullptr) {
            ImGui::DestroyContext();
            throw std::runtime_error("DebugUi: cannot load the font '" + font_path + "'");
        }
    }

    if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
        ImGui::DestroyContext();
        throw std::runtime_error("DebugUi: ImGui_ImplSDL3_InitForSDLGPU failed");
    }
    ImGui_ImplSDLGPU3_InitInfo info = {};
    info.Device = device;
    info.ColorTargetFormat = format;
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("DebugUi: ImGui_ImplSDLGPU3_Init failed");
    }
}

DebugUi::~DebugUi() {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void DebugUi::process_event(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool DebugUi::captures(const SDL_Event& event) const {
    const ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            return io.WantCaptureMouse;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
            return io.WantCaptureKeyboard;
        default:
            return false;
    }
}

void DebugUi::new_frame() {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    frame_started_ = true;
}

void DebugUi::prepare(SDL_GPUCommandBuffer* commands) {
    if (!frame_started_) {
        return;
    }
    ImGui::Render();
    frame_started_ = false;
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commands);
    prepared_ = true;
}

void DebugUi::render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) {
    if (!prepared_) {
        return;  // only draw data uploaded by prepare() in this same frame
    }
    prepared_ = false;
    ImDrawData* data = ImGui::GetDrawData();
    if (data != nullptr && data->CmdListsCount > 0) {
        ImGui_ImplSDLGPU3_RenderDrawData(data, commands, pass);
    }
}

}  // namespace moteur
