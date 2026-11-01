/* REPLACES: lib/imgui.cpp (Dear ImGui on the WebGPU device, Aurora's debug overlay) */
// Aurora's debug overlay is off in the browser build. The imgui release Aurora pins (1.91.9b) has a WebGPU backend
// for Emscripten's former built-in WebGPU only, not for emdawnwebgpu, which this build uses. The overlay is a
// development aid, not part of the game's picture: these are the interface of lib/imgui.hpp with no overlay.
#include "imgui.hpp"

namespace aurora::imgui {

struct DrawData::Impl {};

bool is_initialized() noexcept { return false; }
void create_context() noexcept {}
void initialize() noexcept {}
void shutdown() noexcept {}
void process_event(const SDL_Event&) noexcept {}
void set_cursor_managed(bool) noexcept {}
void new_frame(const AuroraWindowSize&) noexcept {}
DrawData freeze() noexcept { return {}; }
void render(const wgpu::RenderPassEncoder&, const DrawData&) noexcept {}

} // namespace aurora::imgui
