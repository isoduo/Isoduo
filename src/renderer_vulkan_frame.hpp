#pragma once
struct AtlasUpload {
    VkDeviceSize offset;
    VkExtent2D size;
    VkImage restored{};
};
struct GlyphBatch {
    const std::vector<Drawing::RestoredPage>& restored;
    std::vector<ResolvedGlyph> glyphs;
    std::vector<int> uploadAt;
    std::vector<unsigned char> bytes{0};
    std::vector<AtlasUpload> uploads{{0, {1, 1}}};
    VkExtent2D size{1, 1};
    int append(const AtlasPage &page, Rect r) {
        while (bytes.size() % 4)
            bytes.push_back(0);
        VkExtent2D extent{uint32_t(r.w), uint32_t(r.h)};
        AtlasUpload upload{bytes.size(), extent};
        if (r.x == 0 && r.y == 0 && r.w == page.width && r.h == page.height) {
            for (const auto& saved : restored)
                if (saved.page == &page && saved.pixels == page.pixels) {
                    upload.restored = saved.image;
                    break;
                }
        }
        if (!upload.restored) {
            for (int y = 0; y < r.h; ++y) {
                auto row = page.pixels.begin() + size_t(y + r.y) * page.width + r.x;
                bytes.insert(bytes.end(), row, row + r.w);
            }
        }
        uploads.push_back(upload);
        size.width = std::max(size.width, extent.width);
        size.height = std::max(size.height, extent.height);
        return int(uploads.size() - 1);
    }
    GlyphBatch(const DrawList &list, const std::vector<Drawing::RestoredPage>& pagesToRestore) : restored(pagesToRestore), glyphs(list.commands.size()), uploadAt(list.commands.size(), -1) {
        auto resolve = [&](size_t i) {
            auto &c = list.commands[i];
            return c.resolve ? c.resolve() : ResolvedGlyph{c.rect, c.coverage};
        };
        bool segmented = false;
        for (int attempt = 0; attempt < 2; ++attempt) {
            segmented = false;
            for (size_t i = 0; i < glyphs.size(); ++i) {
                glyphs[i] = resolve(i);
                if (glyphs[i].reset) {
                    segmented = true;
                    break;
                }
            }
            if (!segmented)
                break;
        }
        std::vector<const AtlasPage *> pages;
        for (auto &g : glyphs) {
            if (segmented)
                break;
            auto *page = g.region.page;
            if (page && std::find(pages.begin(), pages.end(), page) == pages.end())
                pages.push_back(page);
            segmented = pages.size() > 4;
        }
        std::vector<int> pageUploads;
        if (!segmented)
            for (auto *page : pages)
                pageUploads.push_back(append(*page, {0, 0, page->width, page->height}));
        int previous = 0;
        for (size_t i = 0; i < glyphs.size(); ++i) {
            if (segmented)
                glyphs[i] = resolve(i);
            auto &region = glyphs[i].region;
            if (!region.page || region.rect.w <= 0 || region.rect.h <= 0)
                continue;
            int index;
            if (segmented) {
                index = append(*region.page, region.rect);
                region.rect.x = region.rect.y = 0;
            } else
                index = pageUploads[std::find(pages.begin(), pages.end(), region.page) - pages.begin()];
            if (index != previous)
                uploadAt[i] = index;
            previous = index;
        }
    }
};
struct CommandFrame {
    Drawing &d;
    Resources objects;
    VkCommandBuffer cmd{};
    VkFence fence{};
    VkQueryPool timestamps{};
    explicit CommandFrame(Drawing &drawing, bool measure = false) : d(drawing) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = *d.selected.graphicsQueue;
        auto pool = d.make<VkCommandPool>(objects, "vkCreateCommandPool", d.api.createCommandPool, poolInfo,
                                          d.api.destroyCommandPool);
        VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        info.commandPool = pool;
        info.commandBufferCount = 1;
        d.checked("vkAllocateCommandBuffers", d.api.allocateCommandBuffers(d.device, &info, &cmd));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        d.checked("vkBeginCommandBuffer", d.api.beginCommandBuffer(cmd, &begin));
        if (measure && d.selected.timestampBits) {
            VkQueryPoolCreateInfo pool{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            pool.queryType = VK_QUERY_TYPE_TIMESTAMP;
            pool.queryCount = 2;
            timestamps =
                d.make<VkQueryPool>(objects, "vkCreateQueryPool", d.api.createQueryPool, pool, d.api.destroyQueryPool);
            d.api.cmdResetQueryPool(cmd, timestamps, 0, 2);
            d.api.cmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, timestamps, 0);
        }
        VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence = d.make<VkFence>(objects, "vkCreateFence", d.api.createFence, f, d.api.destroyFence);
    }
    double gpuMilliseconds() {
        if (!timestamps)
            return std::numeric_limits<double>::quiet_NaN();
        uint64_t values[2]{};
        d.checked("vkGetQueryPoolResults", d.api.getQueryPoolResults(d.device, timestamps, 0, 2, sizeof(values), values,
                                                                     sizeof(uint64_t), VK_QUERY_RESULT_64_BIT));
        auto mask = UINT64_MAX >> (64 - d.selected.timestampBits);
        return double((values[1] - values[0]) & mask) * d.timestampPeriod / 1e6;
    }
    void copyMemory(Buffer buffer, unsigned char *bytes, size_t size, bool upload) {
        void *mapped{};
        d.checked("vkMapMemory", d.api.mapMemory(d.device, buffer.memory, 0, size, 0, &mapped));
        if (upload)
            std::memcpy(mapped, bytes, size);
        else
            std::memcpy(bytes, mapped, size);
        d.api.unmapMemory(d.device, buffer.memory);
    }
    void record(const DrawList &list, Texture target, VkPipeline pipeline, bool opaque = false) {
        auto &api = d.api;
        GlyphBatch batch(list,d.restored);
        auto staging = d.buffer(objects, batch.bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        copyMemory(staging, batch.bytes.data(), batch.bytes.size(), true);
        auto atlas = d.texture(objects, batch.size, VK_FORMAT_R8_UNORM,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        VkDescriptorPoolSize count{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &count;
        auto pool = d.make<VkDescriptorPool>(objects, "vkCreateDescriptorPool", api.createDescriptorPool, poolInfo,
                                             api.destroyDescriptorPool);
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &d.descriptors;
        VkDescriptorSet set{};
        d.checked("vkAllocateDescriptorSets", api.allocateDescriptorSets(d.device, &alloc, &set));
        VkDescriptorImageInfo sampled{d.sampler, atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.descriptorType = count.type;
        write.descriptorCount = 1;
        write.pImageInfo = &sampled;
        api.updateDescriptorSets(d.device, 1, &write, 0, nullptr);
        auto upload = [&](int index, VkImageLayout before) {
            auto &region = batch.uploads[index];
            d.barrier(cmd, atlas.image, before, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.bufferOffset = region.offset;
            copy.imageExtent = {region.size.width, region.size.height, 1};
            if (region.restored) {
                VkImageCopy imageCopy{};
                imageCopy.srcSubresource = imageCopy.dstSubresource = copy.imageSubresource;
                imageCopy.extent = copy.imageExtent;
                api.cmdCopyImage(cmd, region.restored, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
            } else {
                api.cmdCopyBufferToImage(cmd, staging.handle, atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            }
            d.barrier(cmd, atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        };
        upload(0, VK_IMAGE_LAYOUT_UNDEFINED);
        d.barrier(cmd, target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = target.view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.clearValue.color.float32[3] = opaque ? 1.f : 0.f;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo render{VK_STRUCTURE_TYPE_RENDERING_INFO};
        render.renderArea.extent = target.extent;
        render.layerCount = render.colorAttachmentCount = 1;
        render.pColorAttachments = &attachment;
        api.cmdBeginRendering(cmd, &render);
        VkViewport viewport{0, 0, float(target.extent.width), float(target.extent.height), 0, 1};
        api.cmdSetViewport(cmd, 0, 1, &viewport);
        api.cmdSetScissor(cmd, 0, 1, &render.renderArea);
        api.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        api.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.layout, 0, 1, &set, 0, nullptr);
        const Rect bounds{0, 0, int(target.extent.width), int(target.extent.height)};
        bool emptyClip = false;
        for (size_t i = 0; i < list.commands.size(); ++i) {
            auto &c = list.commands[i];
            using Type = DrawCommand::Type;
            if (c.type == Type::beginGroup && d.beginLabel) {
                VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
                label.pLabelName = c.owner.c_str();
                d.beginLabel(cmd, &label);
            } else if (c.type == Type::endGroup && d.endLabel) {
                d.endLabel(cmd);
            } else if (c.type == Type::clear) {
                VkClearAttachment clear{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
                clear.clearValue.color = {
                    {c.color.r / 255.f, c.color.g / 255.f, c.color.b / 255.f, opaque ? 1.f : c.color.a / 255.f}};
                VkClearRect rect{render.renderArea, 0, 1};
                api.cmdClearAttachments(cmd, 1, &clear, 1, &rect);
            } else if (c.type == Type::clip) {
                auto clip = intersect(bounds, c.rect);
                emptyClip = clip.w <= 0 || clip.h <= 0;
                if (!emptyClip) {
                    VkRect2D rect{{clip.x, clip.y}, {uint32_t(clip.w), uint32_t(clip.h)}};
                    api.cmdSetScissor(cmd, 0, 1, &rect);
                }
            } else if (c.type == Type::fill || c.type == Type::rounded || c.type == Type::glyph) {
                if (batch.uploadAt[i] >= 0) {
                    api.cmdEndRendering(cmd);
                    upload(batch.uploadAt[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    d.barrier(cmd, target.image, attachment.imageLayout, attachment.imageLayout);
                    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    api.cmdBeginRendering(cmd, &render);
                }
                auto &glyph = batch.glyphs[i];
                auto r = glyph.rect;
                if (emptyClip || r.w <= 0 || r.h <= 0 || (c.type == Type::glyph && !glyph.region.page))
                    continue;
                Push p{{float(r.x), float(r.y), float(r.w), float(r.h)},
                       {c.color.r / 255.f, c.color.g / 255.f, c.color.b / 255.f, c.color.a / 255.f},
                       {viewport.width, viewport.height},
                       {float(glyph.region.rect.x), float(glyph.region.rect.y)},
                       float(c.radius),
                       c.type == Type::glyph     ? 2
                       : c.type == Type::rounded ? 1
                                                 : 0};
                api.cmdPushConstants(cmd, d.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                     sizeof(p), &p);
                api.cmdDraw(cmd, 6, 1, 0, 0);
            }
        }
        api.cmdEndRendering(cmd);
    }
    void submit(VkSemaphore acquired = {}, VkSemaphore complete = {}) {
        if (timestamps)
            d.api.cmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, timestamps, 1);
        d.checked("vkEndCommandBuffer", d.api.endCommandBuffer(cmd));
        // Gate the starting timestamp on image availability as well as the draw.
        VkPipelineStageFlags stage = timestamps ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &cmd;
        info.waitSemaphoreCount = acquired ? 1 : 0;
        info.pWaitSemaphores = &acquired;
        info.pWaitDstStageMask = &stage;
        info.signalSemaphoreCount = complete ? 1 : 0;
        info.pSignalSemaphores = &complete;
        d.checked("vkQueueSubmit", d.api.queueSubmit(d.queue, 1, &info, fence));
    }
    void wait() { d.checked("vkWaitForFences", d.api.waitForFences(d.device, 1, &fence, VK_TRUE, UINT64_MAX)); }
};
Image readPixels(Drawing &d, const DrawList &list, int width, int height) {
    CommandFrame frame(d);
    VkExtent2D extent{uint32_t(width), uint32_t(height)};
    auto image = d.texture(frame.objects, extent, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    Image result{width, height, std::vector<unsigned char>(size_t(width) * height * 4)};
    auto buffer = d.buffer(frame.objects, result.pixels.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    frame.record(list, image, d.rgbaPipeline);
    d.barrier(frame.cmd, image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {extent.width, extent.height, 1};
    d.api.cmdCopyImageToBuffer(frame.cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle, 1, &copy);
    VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    host.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &host;
    d.api.cmdPipelineBarrier2(frame.cmd, &dep);
    frame.submit();
    frame.wait();
    frame.copyMemory(buffer, result.pixels.data(), result.pixels.size(), false);
    return result;
}
