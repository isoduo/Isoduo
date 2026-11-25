#pragma once
#include "renderer.hpp"

namespace isoduo::detail {
#ifdef _WIN32
using VulkanWindow = HWND;
#else
using VulkanWindow = const NativeWindow&;
#endif
void startVulkan(VulkanWindow window, RendererTestHooks* hooks);
}
