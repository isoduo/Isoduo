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
    Drawing d_;
    VkQueue presentQueue_{};
    std::function<std::remove_pointer_t<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>> getCaps_ =
        d_.session.function<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    std::function<std::remove_pointer_t<PFN_vkCreateSwapchainKHR>> create_ = d_.api.loadResult<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");
    PFN_vkDestroySwapchainKHR destroy_ = d_.api.load<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR");
    PFN_vkGetSwapchainImagesKHR images_ = d_.api.load<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
    std::function<std::remove_pointer_t<PFN_vkAcquireNextImageKHR>> acquire_ = d_.api.loadResult<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR");
    std::function<std::remove_pointer_t<PFN_vkQueuePresentKHR>> present_ = d_.api.load<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
    std::unique_ptr<Swapchain> chain_;
    VkPresentModeKHR presentMode_;
    bool dirty_ = true;
    VkPipeline pipeline_{};
    int frameNumber_ = 0;
    VkResult injectedResult(int result) {
        d_.simulatedLoss = result == VK_ERROR_DEVICE_LOST;
        return static_cast<VkResult>(result);
    }
    void installHooks() {
        if (!d_.hooks) return;
        d_.api.createImage = [real=d_.api.createImage, hooks=d_.hooks](auto... args) {
            if (std::exchange(hooks->failImageCreationOnce, false)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            return real(args...);
        };
        if (d_.hooks->hideBufferMemoryTypes)
            d_.api.getBufferMemoryRequirements = [real=d_.api.getBufferMemoryRequirements](auto device, auto buffer, auto* req) {
                real(device, buffer, req);
                req->memoryTypeBits = 0;
            };
        auto wait = d_.api.waitForFences;
        d_.api.waitForFences = [this, wait](auto... args) {
            auto result = wait(args...);
            if (result == VK_SUCCESS && frameNumber_ && d_.hooks->fenceResultAtFrame == frameNumber_) {
                d_.hooks->fenceResultAtFrame = 0;
                d_.simulatedLoss = true;
                return VK_ERROR_DEVICE_LOST;
            }
            return result;
        };
        auto acquireImage = acquire_;
        acquire_ = [this, acquireImage](auto... args) {
            auto &fault = d_.hooks->acquireResultAtFrame;
            if (fault.frame != frameNumber_) return acquireImage(args...);
            fault.frame = 0;
            return injectedResult(fault.result);
        };
        auto show = present_;
        present_ = [this, show](auto... args) {
            auto &fault = d_.hooks->presentResultAtFrame;
            if (fault.frame != frameNumber_) return show(args...);
            fault.frame = 0;
            if (fault.result == VK_SUBOPTIMAL_KHR) {
                auto actual = show(args...);
                if (actual != VK_SUCCESS) return actual;
            }
            return injectedResult(fault.result);
        };
        auto createChain = create_;
        create_ = [this, createChain](auto... args) {
            int fault = std::exchange(d_.hooks->swapchainCreateResultOnce, 0);
            return fault ? injectedResult(fault) : createChain(args...);
        };
        auto scissor = d_.api.cmdSetScissor;
        d_.api.cmdSetScissor = [this, scissor](auto cmd, auto first, auto count, auto rect) {
            if (d_.session.validationEnabled && std::exchange(d_.hooks->misuseOnce,false)) {
                d_.session.testingScissorCount = true;
                scissor(cmd, first, 0, rect);
                d_.session.testingScissorCount = false;
            }
            if (d_.hooks->injectDebugMessage && d_.session.debugUtils) {
                auto send=d_.session.function<PFN_vkSubmitDebugUtilsMessageEXT>("vkSubmitDebugUtilsMessageEXT");
                VkDebugUtilsMessengerCallbackDataEXT message{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};
                message.pMessageIdName="Isoduo.TestMessage";
                message.pMessage="Injected diagnostic";
                message.messageIdNumber=177;
                for (auto n=std::exchange(d_.hooks->injectDebugMessage,0u); n; --n)
                    send(d_.session.instance,VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                         VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,&message);
            }
            scissor(cmd, first, count, rect);
        };
        auto submit = d_.api.queueSubmit;
        d_.api.queueSubmit = [this, submit](auto queue, auto count, auto infos, auto fence) {
            auto &fault = d_.hooks->submitResultAtFrame;
            if (frameNumber_ && fault.frame == frameNumber_) {
                fault.frame = 0;
                return injectedResult(fault.result);
            }
            return submit(queue, count, infos, fence);
        };
    }
    void presentationResult(const char *call, VkResult result, bool rebuilding = false) {
        if (result == VK_SUCCESS)
            return;
        if (!d_.running)
            d_.checked(call, result, Kind::contextCreation);
        if (result == VK_ERROR_SURFACE_LOST_KHR) d_.session.surfaceLost = true;
        auto kind = RendererFailureKind::presentFailed;
        if (result == VK_ERROR_DEVICE_LOST)
            kind = RendererFailureKind::deviceReset;
        else if (rebuilding && (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY))
            kind = RendererFailureKind::resourceFailed;
        d_.diagnostic(call,result,kind);
        throw FrameError{{kind, uint32_t(result), utf16FromUtf8(std::string(call) + ": " + resultName(result))}, {}};
    }
    bool rebuild(VkExtent2D desired) {
        if (chain_) {
            presentationResult("vkQueueWaitIdle", d_.api.queueWaitIdle(presentQueue_), true);
            chain_.reset();
        }
        VkSurfaceCapabilitiesKHR caps{};
        presentationResult("vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                           getCaps_(d_.selected.physical, d_.session.surface, &caps), true);
        auto next = std::make_unique<Swapchain>();
        next->extent = caps.currentExtent.width == UINT32_MAX ? desired : caps.currentExtent;
        if (!next->extent.width || !next->extent.height)
            return false;
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = d_.session.surface;
        info.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount)
            info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
        info.imageFormat = d_.selected.format.format;
        info.imageColorSpace = d_.selected.format.colorSpace;
        info.imageExtent = next->extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        uint32_t queues[] = {*d_.selected.graphicsQueue, *d_.selected.presentQueue};
        if (queues[0] != queues[1]) {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = queues;
        }
        if (d_.hooks)
            d_.hooks->compositeAlphaMask = caps.supportedCompositeAlpha;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha &
                                                                       (~caps.supportedCompositeAlpha + 1));
        next->forceOpaque = info.compositeAlpha != VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = presentMode_;
        info.clipped = VK_TRUE;
        presentationResult(
            "vkCreateSwapchainKHR",
            create_(d_.device, &info, nullptr, &next->handle),
            true);
        next->objects.keep(next->handle, destroy_, d_.device);
        auto handles = enumerate<VkImage>("vkGetSwapchainImagesKHR", [&](uint32_t *n, VkImage *out) {
            auto status=images_(d_.device, next->handle, n, out);
            if (status!=VK_INCOMPLETE) presentationResult("vkGetSwapchainImagesKHR",status,true);
            return status;
        });
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto handle : handles) {
            next->images.push_back({handle, d_.view(next->objects, handle, info.imageFormat), next->extent});
            next->presented.push_back(d_.make<VkSemaphore>(next->objects, "vkCreateSemaphore", d_.api.createSemaphore,
                                                          semaphore, d_.api.destroySemaphore));
        }
        chain_ = std::move(next);
        dirty_ = false;
        return true;
    }

  public:
    SurfaceRenderer(VulkanWindow window, RendererTestHooks *hooks, VkExtent2D initial,
                    std::shared_ptr<Session> session, VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR)
        : d_(window, true, hooks, std::move(session)), presentMode_(mode) {
        d_.api.getDeviceQueue(d_.device, *d_.selected.presentQueue, 0, &presentQueue_);
        pipeline_ = d_.selected.format.format == VK_FORMAT_R8G8B8A8_UNORM ? d_.rgbaPipeline
                                                                        : d_.createPipeline(d_.selected.format.format);
        if (hooks) getCaps_ = [real=getCaps_,hooks](auto physical, auto surface, auto* caps) {
            auto result = real(physical, surface, caps);
            if (result == VK_SUCCESS && std::exchange(hooks->zeroSurfaceExtentOnce, false)) caps->currentExtent = {};
            return result;
        };
        if (initial.width && initial.height)
            rebuild(initial);
        installHooks();
        d_.running = true;
    }
    ~SurfaceRenderer() override {
        if (d_.api.deviceWaitIdle(d_.device)==VK_ERROR_DEVICE_LOST)
            d_.diagnostic("vkDeviceWaitIdle",VK_ERROR_DEVICE_LOST,RendererFailureKind::deviceReset);
    }
    void resized() override { dirty_ = true; }
    RendererCapabilities capabilities() const override { return d_.caps; }
    std::string name() const override { return utf8FromUtf16(d_.caps.renderer) + " / " + utf8FromUtf16(d_.caps.version); }
    void restoreAtlas(const std::vector<const AtlasPage*>& pages) override {
        // Allocation failures here are explicit rebuild failures, before the UI unfreezes.
        if (pages.empty()) return;
        d_.running=false;
        CommandFrame frame(d_);
        for (const auto* page : pages) {
            if (page->pixels.empty()) continue;
            auto buffer=d_.buffer(frame.objects,page->pixels.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            void* mapped{};
            d_.checked("vkMapMemory",d_.api.mapMemory(d_.device,buffer.memory,0,page->pixels.size(),0,&mapped));
            std::memcpy(mapped,page->pixels.data(),page->pixels.size());
            d_.api.unmapMemory(d_.device,buffer.memory);
            VkExtent2D extent{uint32_t(page->width), uint32_t(page->height)};
            auto image = d_.texture(d_.resources, extent, VK_FORMAT_R8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {extent.width, extent.height, 1};
            d_.barrier(frame.cmd, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            d_.api.cmdCopyBufferToImage(frame.cmd, buffer.handle, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            d_.barrier(frame.cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            d_.restored.push_back({page, page->pixels, image.image});
        }
        frame.submit();
        frame.wait();
        d_.running=true;
    }
    Image readBack(const DrawList &list, int width, int height) override { return readPixels(d_, list, width, height); }
    double draw(const DrawList &list, int width, int height, bool measure, bool offscreen, double *submission,
                std::chrono::steady_clock::time_point *displayed) override {
        ++frameNumber_;
        if (d_.hooks) d_.hooks->vulkanDrawAttempts = frameNumber_;
        auto start = std::chrono::steady_clock::now();
        if (offscreen) {
            CommandFrame frame(d_, measure);
            auto target = d_.texture(frame.objects, {uint32_t(width), uint32_t(height)}, VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
            frame.record(list, target, d_.rgbaPipeline);
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
            std::chrono::steady_clock::duration acquireTime{};
            for (;;) {
                if (dirty_ || !chain_ || chain_->extent.width != uint32_t(width) ||
                    chain_->extent.height != uint32_t(height)) {
                    if (rebuilt || !rebuild({uint32_t(width), uint32_t(height)}))
                        throw DeferredFrame{};
                    rebuilt = true;
                }
                CommandFrame frame(d_, measure);
                VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                auto ready = d_.make<VkSemaphore>(frame.objects, "vkCreateSemaphore", d_.api.createSemaphore, info,
                                                 d_.api.destroySemaphore);
                uint32_t index{};
                constexpr uint64_t timeoutNs = 100000000;
                auto beforeAcquire = std::chrono::steady_clock::now();
                auto result = acquire_(d_.device, chain_->handle, timeoutNs, ready, {}, &index);
                auto elapsed = std::chrono::steady_clock::now() - beforeAcquire;
                acquireTime += elapsed;
                if (d_.hooks) {
                    d_.hooks->lastAcquire = result;
                    ++d_.hooks->acquireCalls;
                    d_.hooks->acquireTimeouts += result == VK_TIMEOUT;
                    d_.hooks->acquireTimeoutMs = timeoutNs / 1e6;
                    d_.hooks->maxAcquireMs = std::max(d_.hooks->maxAcquireMs,
                        std::chrono::duration<double, std::milli>(elapsed).count());
                }
                if (result == VK_TIMEOUT || result == VK_NOT_READY)
                    throw DeferredFrame{};
                if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                    dirty_ = true;
                    continue;
                }
                dirty_ = result == VK_SUBOPTIMAL_KHR;
                if (!dirty_)
                    presentationResult("vkAcquireNextImageKHR", result);
                auto image = chain_->images[index];
                frame.record(list, image, pipeline_, chain_->forceOpaque);
                d_.barrier(frame.cmd, image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
                auto done = chain_->presented[index];
                frame.submit(ready, done);
                if (submission)
                    *submission =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start - acquireTime).count();
                VkPresentInfoKHR show{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
                show.waitSemaphoreCount = show.swapchainCount = 1;
                show.pWaitSemaphores = &done;
                show.pSwapchains = &chain_->handle;
                show.pImageIndices = &index;
                auto beforePresent = std::chrono::steady_clock::now();
                result = present_(presentQueue_, &show);
                if (d_.hooks) {
                    d_.hooks->lastPresent = result;
                    d_.hooks->presentMs =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beforePresent)
                            .count();
                }
                if (displayed)
                    *displayed = std::chrono::steady_clock::now();
                frame.wait();
                if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                    dirty_ = true;
                    continue;
                }
                if (result == VK_SUBOPTIMAL_KHR)
                    dirty_ = true;
                else
                    presentationResult("vkQueuePresentKHR", result);
                return measure ? frame.gpuMilliseconds() : 0;
            }
        }
        return 0;
    }
};
