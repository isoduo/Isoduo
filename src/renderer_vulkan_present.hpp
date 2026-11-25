#pragma once
struct Swapchain {
    Resources objects;
    VkSwapchainKHR handle{};
    VkExtent2D extent{};
    bool forceOpaque = false;
    std::vector<Texture> images;
    // The next acquisition of the same image proves its previous present consumed this semaphore.
    std::vector<VkSemaphore> presented;
};
class SurfaceRenderer final : public Renderer {
    Drawing d;
    VkQueue presentQueue{};
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getCaps =
        d.session.function<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    PFN_vkCreateSwapchainKHR create = d.api.load<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");
    PFN_vkDestroySwapchainKHR destroy = d.api.load<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR");
    PFN_vkGetSwapchainImagesKHR images = d.api.load<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
    PFN_vkAcquireNextImageKHR acquire = d.api.load<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR");
    PFN_vkQueuePresentKHR present = d.api.load<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
    std::unique_ptr<Swapchain> chain;
    bool dirty = true;
    VkPipeline pipeline{};
    void presentationResult(const char *call, VkResult result, bool rebuilding = false) {
        if (result == VK_SUCCESS)
            return;
        if (!d.running)
            d.checked(call, result, Kind::contextCreation);
        auto kind = RendererFailureKind::presentFailed;
        if (result == VK_ERROR_DEVICE_LOST)
            kind = RendererFailureKind::deviceReset;
        else if (rebuilding && (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY))
            kind = RendererFailureKind::resourceFailed;
        throw FrameError{{kind, uint32_t(result), utf16FromUtf8(std::string(call) + ": " + resultName(result))}, {}};
    }
    void rebuild(VkExtent2D desired) {
        if (chain) {
            presentationResult("vkQueueWaitIdle", d.api.QueueWaitIdle(presentQueue), true);
            chain.reset();
        }
        VkSurfaceCapabilitiesKHR caps{};
        presentationResult("vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                           getCaps(d.selected.physical, d.session.surface, &caps), true);
        auto next = std::make_unique<Swapchain>();
        next->extent = caps.currentExtent.width == UINT32_MAX ? desired : caps.currentExtent;
        if (!next->extent.width || !next->extent.height)
            throw DeferredFrame{};
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = d.session.surface;
        info.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount)
            info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
        info.imageFormat = d.selected.format.format;
        info.imageColorSpace = d.selected.format.colorSpace;
        info.imageExtent = next->extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        uint32_t queues[] = {*d.selected.graphicsQueue, *d.selected.presentQueue};
        if (queues[0] != queues[1]) {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = queues;
        }
        if (d.hooks)
            d.hooks->compositeAlphaMask = caps.supportedCompositeAlpha;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha &
                                                                       (~caps.supportedCompositeAlpha + 1));
        next->forceOpaque = info.compositeAlpha != VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        presentationResult(
            "vkCreateSwapchainKHR",
            invoke(d.hooks, "vkCreateSwapchainKHR", [&] { return create(d.device, &info, nullptr, &next->handle); }),
            true);
        next->objects.keep(next->handle, destroy, d.device);
        auto handles = enumerate<VkImage>("vkGetSwapchainImagesKHR", [&](uint32_t *n, VkImage *out) {
            return images(d.device, next->handle, n, out);
        });
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto handle : handles) {
            next->images.push_back({handle, d.view(next->objects, handle, info.imageFormat), next->extent});
            next->presented.push_back(d.make<VkSemaphore>(next->objects, "vkCreateSemaphore", d.api.CreateSemaphore,
                                                          semaphore, d.api.DestroySemaphore));
        }
        chain = std::move(next);
        dirty = false;
    }

  public:
    SurfaceRenderer(VulkanWindow window, RendererTestHooks *hooks, VkExtent2D initial) : d(window, true, hooks) {
        d.api.GetDeviceQueue(d.device, *d.selected.presentQueue, 0, &presentQueue);
        pipeline = d.selected.format.format == VK_FORMAT_R8G8B8A8_UNORM ? d.rgbaPipeline
                                                                        : d.createPipeline(d.selected.format.format);
        if (initial.width && initial.height)
            rebuild(initial);
        d.running = true;
    }
    ~SurfaceRenderer() override { d.api.DeviceWaitIdle(d.device); }
    void resized() override { dirty = true; }
    RendererCapabilities capabilities() const override { return d.caps; }
    std::string name() const override { return utf8FromUtf16(d.caps.renderer) + " / " + utf8FromUtf16(d.caps.version); }
    void restoreAtlas(
        const std::vector<const AtlasPage *> &) override { /* Frame recording uploads current CPU pages before use. */ }
    Image readBack(const DrawList &list, int width, int height) override { return readPixels(d, list, width, height); }
    double draw(const DrawList &list, int width, int height, bool measure, bool offscreen, double *submission,
                std::chrono::steady_clock::time_point *displayed) override {
        auto start = std::chrono::steady_clock::now();
        if (offscreen) {
            CommandFrame frame(d, measure);
            auto target = d.texture(frame.objects, {uint32_t(width), uint32_t(height)}, VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
            frame.record(list, target, d.rgbaPipeline);
            frame.submit();
            if (submission)
                *submission =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            frame.wait();
            if (displayed)
                *displayed = std::chrono::steady_clock::now();
            return measure ? frame.gpuMilliseconds() : 0;
        } else {
            bool rebuilt = false;
            for (;;) {
                if (dirty || !chain || chain->extent.width != uint32_t(width) ||
                    chain->extent.height != uint32_t(height)) {
                    if (rebuilt)
                        throw DeferredFrame{};
                    rebuild({uint32_t(width), uint32_t(height)});
                    rebuilt = true;
                }
                CommandFrame frame(d, measure);
                VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                auto ready = d.make<VkSemaphore>(frame.objects, "vkCreateSemaphore", d.api.CreateSemaphore, info,
                                                 d.api.DestroySemaphore);
                uint32_t index{};
                auto result = invoke(d.hooks, "vkAcquireNextImageKHR", [&] {
                    return acquire(d.device, chain->handle, 100000000, ready, {}, &index);
                });
                if (d.hooks)
                    d.hooks->lastAcquire = result;
                if (result == VK_TIMEOUT || result == VK_NOT_READY)
                    throw DeferredFrame{};
                if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                    dirty = true;
                    continue;
                }
                dirty = result == VK_SUBOPTIMAL_KHR;
                if (!dirty)
                    presentationResult("vkAcquireNextImageKHR", result);
                auto image = chain->images[index];
                frame.record(list, image, pipeline, chain->forceOpaque);
                d.barrier(frame.cmd, image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
                auto done = chain->presented[index];
                frame.submit(ready, done);
                if (submission)
                    *submission =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                VkPresentInfoKHR show{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
                show.waitSemaphoreCount = show.swapchainCount = 1;
                show.pWaitSemaphores = &done;
                show.pSwapchains = &chain->handle;
                show.pImageIndices = &index;
                auto beforePresent = std::chrono::steady_clock::now();
                result = present(presentQueue, &show);
                if (d.hooks) {
                    d.hooks->lastPresent = result;
                    d.hooks->presentMs =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beforePresent)
                            .count();
                }
                if (displayed)
                    *displayed = std::chrono::steady_clock::now();
                frame.wait();
                if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                    dirty = true;
                    continue;
                }
                if (result == VK_SUBOPTIMAL_KHR)
                    dirty = true;
                else
                    presentationResult("vkQueuePresentKHR", result);
                return measure ? frame.gpuMilliseconds() : 0;
            }
        }
        return 0;
    }
};
