/* REPLACES: lib/dawn/BackendBinding.cpp (native window handles: HWND, Metal, Wayland, X11, Android) */
// WebGPU surfaces in the browser: an SDL window is a <canvas>, and its surface is made from the canvas's CSS
// selector (emdawnwebgpu's EmscriptenSurfaceSourceCanvasHTMLSelector). Same interface as Aurora's
// lib/dawn/BackendBinding.cpp; the other surface types have no meaning on a web page.
#include "BackendBinding.hpp"

#include <aurora/webgpu.hpp>
#include "../window.hpp"

#include <memory>
#include <string>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

namespace aurora::webgpu {
namespace utils {

std::shared_ptr<wgpu::ChainedStruct> SetupWindowAndGetSurfaceDescriptor(SDL_Window* window) {
  struct CanvasSelector : wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector {
    std::string text;
  };
  const char* id = SDL_GetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING,
                                         "canvas");
  auto desc = std::make_shared<CanvasSelector>();
  desc->text = std::string("#") + (id[0] == '#' ? id + 1 : id);
  desc->selector = wgpu::StringView(desc->text.c_str(), desc->text.size());
  return desc;
}

} // namespace utils

wgpu::Surface create_window_surface(const wgpu::Instance& instance, SDL_Window* window, const char* label) {
  if (!instance || window == nullptr) {
    return nullptr;
  }
  const auto chainedDescriptor = utils::SetupWindowAndGetSurfaceDescriptor(window);
  const wgpu::SurfaceDescriptor descriptor{
      .nextInChain = chainedDescriptor.get(),
      .label = label,
  };
  return instance.CreateSurface(&descriptor);
}

wgpu::Surface create_surface(const wgpu::Instance& instance, const AuroraSurfaceDesc& desc, const char* label) {
  if (!instance || desc.type != AURORA_SURFACE_SDL_MANAGED) {
    return nullptr;
  }
  return window::create_sdl_surface(instance, label);
}

} // namespace aurora::webgpu
