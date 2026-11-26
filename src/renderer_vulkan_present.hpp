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
    std::function<std::remove_pointer_t<PFN_vkCreateSwapchainKHR>> create = d.api.load<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");
    PFN_vkDestroySwapchainKHR destroy = d.api.load<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR");
    PFN_vkGetSwapchainImagesKHR images = d.api.load<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
    std::function<std::remove_pointer_t<PFN_vkAcquireNextImageKHR>> acquire = d.api.load<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR");
    std::function<std::remove_pointer_t<PFN_vkQueuePresentKHR>> present = d.api.load<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
    std::unique_ptr<Swapchain> chain;
    VkPresentModeKHR presentMode;
    bool dirty = true;
    VkPipeline pipeline{};
    int frameNumber = 0;
    void installHooks() {
        if (!d.hooks) return;
        auto wait = d.api.WaitForFences;
        d.api.WaitForFences = [this, wait](auto... args) {
            auto result = wait(args...);
            if (result == VK_SUCCESS && frameNumber && d.hooks->fenceResultAtFrame == frameNumber) {
                d.hooks->fenceResultAtFrame = 0;
                return VK_ERROR_DEVICE_LOST;
            }
            return result;
        };
        auto acquireImage = acquire;
        acquire = [this, acquireImage](auto... args) {
            auto &fault = d.hooks->acquireResultAtFrame;
            if (fault.first != frameNumber) return acquireImage(args...);
            fault.first = 0;
            return static_cast<VkResult>(fault.second);
        };
        auto show = present;
        present = [this, show](auto... args) {
            auto &fault = d.hooks->presentResultAtFrame;
            if (fault.first != frameNumber) return show(args...);
            fault.first = 0;
            if (fault.second == VK_SUBOPTIMAL_KHR) {
                auto actual = show(args...);
                if (actual != VK_SUCCESS) return actual;
            }
            return static_cast<VkResult>(fault.second);
        };
        auto createChain = create;
        create = [this, createChain](auto... args) {
            int fault = std::exchange(d.hooks->swapchainCreateResultOnce, 0);
            return fault ? static_cast<VkResult>(fault) : createChain(args...);
        };
        auto scissor = d.api.CmdSetScissor;
        d.api.CmdSetScissor = [this, scissor](auto cmd, auto first, auto count, auto rect) {
            if (d.session.validationEnabled && std::exchange(d.hooks->misuseOnce,false)) {
                d.session.testingScissorCount = true;
                scissor(cmd, first, 0, rect);
                d.session.testingScissorCount = false;
            }
            if (d.hooks->injectDebugMessage && d.session.debugUtils) {
                auto send=d.session.function<PFN_vkSubmitDebugUtilsMessageEXT>("vkSubmitDebugUtilsMessageEXT");
                VkDebugUtilsMessengerCallbackDataEXT message{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};
                message.pMessageIdName="Isoduo.TestMessage";
                message.pMessage="Injected diagnostic";
                message.messageIdNumber=177;
                for (auto n=std::exchange(d.hooks->injectDebugMessage,0u); n; --n)
                    send(d.session.instance,VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                         VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,&message);
            }
            scissor(cmd, first, count, rect);
        };
        auto submit = d.api.QueueSubmit;
        d.api.QueueSubmit = [this, submit](auto queue, auto count, auto infos, auto fence) {
            auto &fault = d.hooks->submitResultAtFrame;
            if (frameNumber && fault.first == frameNumber) {
                fault.first = 0;
                return static_cast<VkResult>(fault.second);
            }
            return submit(queue, count, infos, fence);
        };
    }
    void presentationResult(const char *call, VkResult result, bool rebuilding = false) {
        if (result == VK_SUCCESS)
            return;
        if (!d.running)
            d.checked(call, result, Kind::contextCreation);
        if (result == VK_ERROR_SURFACE_LOST_KHR) d.session.surfaceLost = true;
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
        info.presentMode = presentMode;
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
    SurfaceRenderer(VulkanWindow window, RendererTestHooks *hooks, VkExtent2D initial,
                    std::shared_ptr<Session> session, VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR)
        : d(window, true, hooks, std::move(session)), presentMode(mode) {
        d.api.GetDeviceQueue(d.device, *d.selected.presentQueue, 0, &presentQueue);
        pipeline = d.selected.format.format == VK_FORMAT_R8G8B8A8_UNORM ? d.rgbaPipeline
                                                                        : d.createPipeline(d.selected.format.format);
        if (initial.width && initial.height)
            rebuild(initial);
        installHooks();
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
        ++frameNumber;
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
                auto result = acquire(d.device, chain->handle, 100000000, ready, {}, &index);
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
