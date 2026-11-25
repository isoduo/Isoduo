#pragma once
#include "renderer.hpp"

namespace isoduo::detail {
#ifdef _WIN32
using VulkanWindow = HWND;
#else
using VulkanWindow = const NativeWindow&;
#endif
Image renderVulkanOffscreen(const DrawList&, int width, int height);
#ifdef _WIN32
std::unique_ptr<Renderer> createVulkanRenderer(HWND, RendererTestHooks*);
#else
void startVulkan(VulkanWindow window, RendererTestHooks* hooks);
#endif
}
