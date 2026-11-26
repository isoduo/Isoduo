// Private device commands and draw resources used by both Vulkan targets.
#pragma once
#include "vulkan_shaders/draw_vert.inc"
#include "vulkan_shaders/draw_frag.inc"

struct DeviceCommands {
    Session &session;
    template <class F> F load(const char *name) {
        auto address = session.getDeviceProcAddr(session.device, name);
        if (!address)
            throw StartupError{{Kind::contextCreation, {}, {}, utf16FromUtf8(name)}};
        return reinterpret_cast<F>(address);
    }
    PFN_vkCreateBuffer CreateBuffer = load<PFN_vkCreateBuffer>("vkCreateBuffer");
    PFN_vkDestroyBuffer DestroyBuffer = load<PFN_vkDestroyBuffer>("vkDestroyBuffer");
    std::function<std::remove_pointer_t<PFN_vkGetBufferMemoryRequirements>> GetBufferMemoryRequirements =
        load<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements");
    PFN_vkAllocateMemory AllocateMemory = load<PFN_vkAllocateMemory>("vkAllocateMemory");
    PFN_vkFreeMemory FreeMemory = load<PFN_vkFreeMemory>("vkFreeMemory");
    PFN_vkBindBufferMemory BindBufferMemory = load<PFN_vkBindBufferMemory>("vkBindBufferMemory");
    PFN_vkMapMemory MapMemory = load<PFN_vkMapMemory>("vkMapMemory");
    PFN_vkUnmapMemory UnmapMemory = load<PFN_vkUnmapMemory>("vkUnmapMemory");
    std::function<std::remove_pointer_t<PFN_vkCreateImage>> CreateImage = load<PFN_vkCreateImage>("vkCreateImage");
    PFN_vkDestroyImage DestroyImage = load<PFN_vkDestroyImage>("vkDestroyImage");
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements =
        load<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements");
    PFN_vkBindImageMemory BindImageMemory = load<PFN_vkBindImageMemory>("vkBindImageMemory");
    PFN_vkCreateImageView CreateImageView = load<PFN_vkCreateImageView>("vkCreateImageView");
    PFN_vkDestroyImageView DestroyImageView = load<PFN_vkDestroyImageView>("vkDestroyImageView");
    PFN_vkCreateCommandPool CreateCommandPool = load<PFN_vkCreateCommandPool>("vkCreateCommandPool");
    PFN_vkDestroyCommandPool DestroyCommandPool = load<PFN_vkDestroyCommandPool>("vkDestroyCommandPool");
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers =
        load<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers");
    PFN_vkBeginCommandBuffer BeginCommandBuffer = load<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer");
    PFN_vkEndCommandBuffer EndCommandBuffer = load<PFN_vkEndCommandBuffer>("vkEndCommandBuffer");
    PFN_vkCmdPipelineBarrier2 CmdPipelineBarrier2 = load<PFN_vkCmdPipelineBarrier2>("vkCmdPipelineBarrier2");
    PFN_vkCmdBeginRendering CmdBeginRendering = load<PFN_vkCmdBeginRendering>("vkCmdBeginRendering");
    PFN_vkCmdEndRendering CmdEndRendering = load<PFN_vkCmdEndRendering>("vkCmdEndRendering");
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage = load<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage");
    PFN_vkCmdCopyImage cmdCopyImage = load<PFN_vkCmdCopyImage>("vkCmdCopyImage");
    PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer = load<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer");
    PFN_vkCmdClearAttachments CmdClearAttachments = load<PFN_vkCmdClearAttachments>("vkCmdClearAttachments");
    PFN_vkCmdSetViewport CmdSetViewport = load<PFN_vkCmdSetViewport>("vkCmdSetViewport");
    std::function<std::remove_pointer_t<PFN_vkCmdSetScissor>> CmdSetScissor = load<PFN_vkCmdSetScissor>("vkCmdSetScissor");
    PFN_vkCmdBindPipeline CmdBindPipeline = load<PFN_vkCmdBindPipeline>("vkCmdBindPipeline");
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = load<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets");
    PFN_vkCmdPushConstants CmdPushConstants = load<PFN_vkCmdPushConstants>("vkCmdPushConstants");
    PFN_vkCreateQueryPool CreateQueryPool = load<PFN_vkCreateQueryPool>("vkCreateQueryPool");
    PFN_vkDestroyQueryPool DestroyQueryPool = load<PFN_vkDestroyQueryPool>("vkDestroyQueryPool");
    PFN_vkCmdResetQueryPool CmdResetQueryPool = load<PFN_vkCmdResetQueryPool>("vkCmdResetQueryPool");
    PFN_vkCmdWriteTimestamp2 CmdWriteTimestamp2 = load<PFN_vkCmdWriteTimestamp2>("vkCmdWriteTimestamp2");
    PFN_vkGetQueryPoolResults GetQueryPoolResults = load<PFN_vkGetQueryPoolResults>("vkGetQueryPoolResults");
    PFN_vkCmdDraw CmdDraw = load<PFN_vkCmdDraw>("vkCmdDraw");
    PFN_vkCreateSampler CreateSampler = load<PFN_vkCreateSampler>("vkCreateSampler");
    PFN_vkDestroySampler DestroySampler = load<PFN_vkDestroySampler>("vkDestroySampler");
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout =
        load<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout");
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout =
        load<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout");
    PFN_vkCreateDescriptorPool CreateDescriptorPool = load<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool");
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool = load<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool");
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets =
        load<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets");
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets = load<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets");
    PFN_vkCreatePipelineLayout CreatePipelineLayout = load<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout");
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout = load<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout");
    PFN_vkCreateShaderModule CreateShaderModule = load<PFN_vkCreateShaderModule>("vkCreateShaderModule");
    PFN_vkDestroyShaderModule DestroyShaderModule = load<PFN_vkDestroyShaderModule>("vkDestroyShaderModule");
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines =
        load<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines");
    PFN_vkDestroyPipeline DestroyPipeline = load<PFN_vkDestroyPipeline>("vkDestroyPipeline");
    PFN_vkGetDeviceQueue GetDeviceQueue = load<PFN_vkGetDeviceQueue>("vkGetDeviceQueue");
    std::function<std::remove_pointer_t<PFN_vkQueueSubmit>> QueueSubmit = load<PFN_vkQueueSubmit>("vkQueueSubmit");
    PFN_vkCreateFence CreateFence = load<PFN_vkCreateFence>("vkCreateFence");
    PFN_vkDestroyFence DestroyFence = load<PFN_vkDestroyFence>("vkDestroyFence");
    std::function<std::remove_pointer_t<PFN_vkWaitForFences>> WaitForFences = load<PFN_vkWaitForFences>("vkWaitForFences");
    PFN_vkDeviceWaitIdle DeviceWaitIdle = load<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle");
    PFN_vkQueueWaitIdle QueueWaitIdle = load<PFN_vkQueueWaitIdle>("vkQueueWaitIdle");
    PFN_vkCreateSemaphore CreateSemaphore = load<PFN_vkCreateSemaphore>("vkCreateSemaphore");
    PFN_vkDestroySemaphore DestroySemaphore = load<PFN_vkDestroySemaphore>("vkDestroySemaphore");
};

// Handles are released in reverse creation order at the enclosing frame or device boundary.
struct Resources {
    std::vector<std::function<void()>> cleanup;
    ~Resources() {
        for (auto i = cleanup.rbegin(); i != cleanup.rend(); ++i)
            (*i)();
    }
    Resources() = default;
    Resources(const Resources &) = delete;
    template <class T, class F> T keep(T handle, F destroy, VkDevice device) {
        cleanup.emplace_back([=] { destroy(device, handle, nullptr); });
        return handle;
    }
};
struct Push {
    float rect[4], tint[4], screen[2], atlas[2], radius;
    int kind;
};

struct Buffer {
    VkBuffer handle;
    VkDeviceMemory memory;
};
struct Texture {
    VkImage image;
    VkImageView view;
    VkExtent2D extent;
};
struct Drawing {
    std::shared_ptr<Session> owner;
    Session &session;
    Selection selected;
    struct DeviceOwner {
        Session& session;
        ~DeviceOwner() { session.destroyDevice(std::exchange(session.device, {}), nullptr); }
    } deviceOwner{session};
    DeviceCommands api;
    VkDevice device;
    VkQueue queue{};
    VkPhysicalDeviceMemoryProperties memory{};
    RendererTestHooks *hooks;
    RendererCapabilities caps;
    bool running = false;
    float timestampPeriod{};
    PFN_vkCmdBeginDebugUtilsLabelEXT beginLabel{};
    PFN_vkCmdEndDebugUtilsLabelEXT endLabel{};
    Resources resources;
    // Restored images remain copy sources while the corresponding CPU page is unchanged.
    struct RestoredPage {
        const AtlasPage* page;
        std::vector<unsigned char> pixels;
        VkImage image;
    };
    std::vector<RestoredPage> restored;
    VkSampler sampler{};
    VkDescriptorSetLayout descriptors{};
    VkPipelineLayout layout{};
    VkPipeline rgbaPipeline{};
    Drawing(VulkanWindow window, bool surface, RendererTestHooks *testHooks, std::shared_ptr<Session> retained = {})
        : owner(retained ? std::move(retained) : std::make_shared<Session>()), session(*owner), selected(initialize(session, window, testHooks, surface)), api{session}, device(session.device),
          hooks(testHooks) {
        api.GetDeviceQueue(device, *selected.graphicsQueue, 0, &queue);
        session.function<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(
            selected.physical, &memory);
        VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver};
        session.function<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2")(selected.physical,
                                                                                               &props);
        timestampPeriod = props.properties.limits.timestampPeriod;
        if (session.debugUtils) {
            beginLabel = api.load<PFN_vkCmdBeginDebugUtilsLabelEXT>("vkCmdBeginDebugUtilsLabelEXT");
            endLabel = api.load<PFN_vkCmdEndDebugUtilsLabelEXT>("vkCmdEndDebugUtilsLabelEXT");
        }
        auto version = props.properties.apiVersion;
        caps.version = utf16FromUtf8("Vulkan " + versionString(version) + "." +
                                     std::to_string(VK_API_VERSION_PATCH(version)) + " " + driver.driverInfo);
        caps.vendor = utf16FromUtf8(driver.driverName);
        caps.renderer = utf16FromUtf8(props.properties.deviceName);
        if (session.previousDevice && session.previousDevice!=selected.physical && session.sink) {
            RenderDiagnostic event;
            event.backend=u"Vulkan";
            event.severity=DiagnosticSeverity::info;
            event.nativeMessage=std::string("Renderer rebuilt on ")+props.properties.deviceName;
            session.sink(std::move(event));
        }
        session.previousDevice=selected.physical;
        caps.renderBackend = RenderBackendKind::vulkan;
        caps.resetDetection = true;
        caps.debugContext = session.validationEnabled;
        caps.debugMessages = caps.debugContext && session.validationMessenger;
        VkSamplerCreateInfo sample{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sample.addressModeU = sample.addressModeV = sample.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler = make<VkSampler>(resources, "vkCreateSampler", api.CreateSampler, sample, api.DestroySampler);
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                             VK_SHADER_STAGE_FRAGMENT_BIT, &sampler};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.bindingCount = 1;
        set.pBindings = &binding;
        descriptors = make<VkDescriptorSetLayout>(resources, "vkCreateDescriptorSetLayout",
                                                  api.CreateDescriptorSetLayout, set, api.DestroyDescriptorSetLayout);
        VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
        VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline.setLayoutCount = pipeline.pushConstantRangeCount = 1;
        pipeline.pSetLayouts = &descriptors;
        pipeline.pPushConstantRanges = &range;
        layout = make<VkPipelineLayout>(resources, "vkCreatePipelineLayout", api.CreatePipelineLayout, pipeline,
                                        api.DestroyPipelineLayout);
        rgbaPipeline = createPipeline(VK_FORMAT_R8G8B8A8_UNORM);
    }
    bool simulatedLoss = false;
    std::string faultReport() {
        if (!selected.faultExtension || simulatedLoss) return {};
        if (std::string_view(selected.faultExtension)==VK_KHR_DEVICE_FAULT_EXTENSION_NAME) {
            auto get=api.load<PFN_vkGetDeviceFaultReportsKHR>("vkGetDeviceFaultReportsKHR");
            uint32_t count=0;
            if (get(device,0,&count,nullptr)!=VK_SUCCESS || !count) return {};
            std::vector<VkDeviceFaultInfoKHR> reports(count,{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR});
            auto result=get(device,0,&count,reports.data());
            std::string detail;
            if (result==VK_SUCCESS || result==VK_INCOMPLETE)
                for (uint32_t i=0;i<count;++i) detail+=std::string(reports[i].description)+"\n";
            return detail;
        }
        auto get=api.load<PFN_vkGetDeviceFaultInfoEXT>("vkGetDeviceFaultInfoEXT");
        VkDeviceFaultCountsEXT counts{VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
        if (get(device,&counts,nullptr)!=VK_SUCCESS) return {};
        std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
        std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
        VkDeviceFaultInfoEXT info{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
        info.pAddressInfos=addresses.data();
        info.pVendorInfos=vendors.data();
        counts.vendorBinarySize=0;
        auto result=get(device,&counts,&info);
        return result==VK_SUCCESS || result==VK_INCOMPLETE ? info.description : "";
    }
    void diagnostic(const char* call,VkResult result,RendererFailureKind kind) {
        if (!session.sink) return;
        RenderDiagnostic event;
        event.backend=u"Vulkan";
        event.severity=DiagnosticSeverity::error;
        event.nativeCode=uint32_t(result);
        event.nativeMessage=std::string(call)+": "+resultName(result);
        if (kind==RendererFailureKind::deviceReset) {
            event.kind=DiagnosticKind::deviceReset;
            event.backendDetail=faultReport();
        }
        session.sink(std::move(event));
    }
    void checked(const char *call, VkResult result, Kind failure = Kind::resource) {
        if (running && result != VK_SUCCESS) {
            auto kind =
                result == VK_ERROR_DEVICE_LOST ? RendererFailureKind::deviceReset : RendererFailureKind::resourceFailed;
            if (kind != RendererFailureKind::deviceReset &&
                (std::string_view(call) == "vkQueueSubmit" || std::string_view(call) == "vkWaitForFences" ||
                 std::string_view(call) == "vkEndCommandBuffer" || std::string_view(call) == "vkBeginCommandBuffer"))
                kind = RendererFailureKind::frameError;
            diagnostic(call,result,kind);
            throw FrameError{{kind, uint32_t(result), utf16FromUtf8(std::string(call) + ": " + resultName(result))},
                             {}};
        }
        if (result != VK_SUCCESS)
            throw StartupError{{failure, {}, {}, utf16FromUtf8(std::string(call) + ": " + resultName(result))}};
    }
    template <class T, class F, class Info, class D>
    T make(Resources &owner, const char *name, F create, const Info &info, D destroy, Kind failure = Kind::resource) {
        T handle{};
        checked(name, invoke(hooks, name, [&] { return create(device, &info, nullptr, &handle); }), failure);
        return owner.keep(handle, destroy, device);
    }
    VkPipeline createPipeline(VkFormat format) {
        Resources modules;
        auto module = [&](const auto &bytes) {
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = sizeof(bytes);
            info.pCode = bytes;
            return make<VkShaderModule>(modules, "vkCreateShaderModule", api.CreateShaderModule, info,
                                        api.DestroyShaderModule, Kind::shader);
        };
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[0].module = module(draw_vert);
        stages[1].module = module(draw_frag);
        stages[0].pName = stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vertices{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo states{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        states.dynamicStateCount = 2;
        states.pDynamicStates = dynamic;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering};
        info.stageCount = 2;
        info.pStages = stages;
        info.layout = layout;
        info.pVertexInputState = &vertices;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pColorBlendState = &blend;
        info.pDynamicState = &states;
        VkPipeline pipeline{};
        checked("vkCreateGraphicsPipelines",
                invoke(hooks, "vkCreateGraphicsPipelines",
                       [&] { return api.CreateGraphicsPipelines(device, {}, 1, &info, nullptr, &pipeline); }),
                Kind::shader);
        return resources.keep(pipeline, api.DestroyPipeline, device);
    }
    VkDeviceMemory allocate(Resources &owner, VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if (!(req.memoryTypeBits & (1u << i)) || (memory.memoryTypes[i].propertyFlags & flags) != flags)
                continue;
            VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            info.allocationSize = req.size;
            info.memoryTypeIndex = i;
            return make<VkDeviceMemory>(owner, "vkAllocateMemory", api.AllocateMemory, info, api.FreeMemory);
        }
        if (running)
            throw FrameError{{RendererFailureKind::resourceFailed, 0, u"No compatible Vulkan memory type"}, {}};
        throw StartupError{{Kind::resource, {}, {}, u"No compatible Vulkan memory type"}};
    }
    Buffer buffer(Resources &owner, VkDeviceSize bytes, VkBufferUsageFlags usage) {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = bytes;
        info.usage = usage;
        auto handle = make<VkBuffer>(owner, "vkCreateBuffer", api.CreateBuffer, info, api.DestroyBuffer);
        VkMemoryRequirements req{};
        api.GetBufferMemoryRequirements(device, handle, &req);
        auto mem = allocate(owner, req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::iter_swap(owner.cleanup.end() - 1, owner.cleanup.end() - 2);
        checked("vkBindBufferMemory", api.BindBufferMemory(device, handle, mem, 0));
        return {handle, mem};
    }
    Texture texture(Resources &owner, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage) {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {extent.width, extent.height, 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.usage = usage;
        auto image = make<VkImage>(owner, "vkCreateImage", api.CreateImage, info, api.DestroyImage);
        VkMemoryRequirements req{};
        api.GetImageMemoryRequirements(device, image, &req);
        auto mem = allocate(owner, req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        std::iter_swap(owner.cleanup.end() - 1, owner.cleanup.end() - 2);
        checked("vkBindImageMemory", api.BindImageMemory(device, image, mem, 0));
        return {image, view(owner, image, format), extent};
    }
    VkImageView view(Resources &owner, VkImage image, VkFormat format) {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        return make<VkImageView>(owner, "vkCreateImageView", api.CreateImageView, info, api.DestroyImageView);
    }
    void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout before, VkImageLayout after) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask =
            before == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        b.oldLayout = before;
        b.newLayout = after;
        b.image = image;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        api.CmdPipelineBarrier2(cmd, &dep);
    }
};
