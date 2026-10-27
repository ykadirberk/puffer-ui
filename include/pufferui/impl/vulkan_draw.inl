// Vulkan triangle/reference and bilinear copy recording.
// Preserve core positions, primitive order, float colors and straight alpha.
// Pipeline compilation occurs at device initialization, not first UI draw.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
struct byte_channel_table
{
    f32 values[256]{};
    constexpr byte_channel_table()
    {
        for (u32 i = 0; i < 256; ++i) values[i] = static_cast<f32>(i) / 255.0f;
    }
};
static constexpr byte_channel_table BYTE_CHANNELS{};

inline VkPipelineColorBlendAttachmentState native_blend_attachment(bool enabled)
{
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    attachment.blendEnable = enabled ? VK_TRUE : VK_FALSE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    return attachment;
}

VkPipeline device::pipeline(VkFormat format, bool blend, bool sdf)
{
    for (auto &p : pipelines)
        if (p.format == format) return sdf ? p.sdf : blend ? p.blend : p.copy;
    if (!vertex_shader)
    {
        VkShaderModuleCreateInfo sc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sc.codeSize = sizeof(vulkan_ui_vert);
        sc.pCode = vulkan_ui_vert;
        if (!check(vkCreateShaderModule(native.device, &sc, nullptr, &vertex_shader),
                   "vkCreateShaderModule(vertex)"))
            return VK_NULL_HANDLE;
        sc.codeSize = sizeof(vulkan_ui_frag);
        sc.pCode = vulkan_ui_frag;
        if (!check(vkCreateShaderModule(native.device, &sc, nullptr, &fragment_shader),
                   "vkCreateShaderModule(fragment)"))
            return VK_NULL_HANDLE;
    }
    pipeline_pair pair;
    pair.format = format;
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto &s : stages)
    {
        s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        s.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_shader;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_shader;
    VkVertexInputBindingDescription binding{0, 32, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                                                      {1, 0, VK_FORMAT_R32G32_SFLOAT, 8},
                                                      {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    auto attachment = native_blend_attachment(false);
    VkPipelineColorBlendStateCreateInfo cb{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &attachment;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo pc{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pc.pNext = &rendering;
    pc.stageCount = 2;
    pc.pStages = stages;
    pc.pVertexInputState = &vi;
    pc.pInputAssemblyState = &ia;
    pc.pViewportState = &vp;
    pc.pRasterizationState = &rs;
    pc.pMultisampleState = &ms;
    pc.pColorBlendState = &cb;
    pc.pDynamicState = &ds;
    pc.layout = pipeline_layout;
    attachment.blendEnable = VK_TRUE;
    if (!check(
            vkCreateGraphicsPipelines(native.device, VK_NULL_HANDLE, 1, &pc, nullptr, &pair.blend),
            "vkCreateGraphicsPipelines(blend)"))
        return VK_NULL_HANDLE;
    attachment.blendEnable = VK_FALSE;
    if (!check(
            vkCreateGraphicsPipelines(native.device, VK_NULL_HANDLE, 1, &pc, nullptr, &pair.copy),
            "vkCreateGraphicsPipelines(copy)"))
    {
        vkDestroyPipeline(native.device, pair.blend, nullptr);
        return VK_NULL_HANDLE;
    }
    if (paint_detail::sdf_painter(options.painter))
    {
        if (!sdf_vertex_shader)
        {
            VkShaderModuleCreateInfo sc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            sc.codeSize = sizeof(vulkan_sdf_vert);
            sc.pCode = vulkan_sdf_vert;
            if (!check(vkCreateShaderModule(native.device, &sc, nullptr, &sdf_vertex_shader),
                       "vkCreateShaderModule(SDF vertex)"))
                return VK_NULL_HANDLE;
            sc.codeSize = paint_detail::compute_painter(options.painter)
                              ? sizeof(vulkan_program_frag)
                              : sizeof(vulkan_sdf_frag);
            sc.pCode = paint_detail::compute_painter(options.painter) ? vulkan_program_frag
                                                                      : vulkan_sdf_frag;
            if (options.painter == vulkan_painter_mode::INTEGRATED)
            {
                sc.codeSize = sizeof(vulkan_integrated_frag);
                sc.pCode = vulkan_integrated_frag;
            }
            if (!check(vkCreateShaderModule(native.device, &sc, nullptr, &sdf_fragment_shader),
                       "vkCreateShaderModule(SDF fragment)"))
                return VK_NULL_HANDLE;
        }
        stages[0].module = sdf_vertex_shader;
        stages[1].module = sdf_fragment_shader;
        binding.stride = 48;
        VkVertexInputAttributeDescription sdf_attributes[4] = {
            attributes[0], attributes[1], attributes[2], {3, 0, VK_FORMAT_R32_UINT, 32}};
        vi.vertexAttributeDescriptionCount = 4;
        vi.pVertexAttributeDescriptions = sdf_attributes;
        attachment.blendEnable = VK_TRUE;
        if (!check(vkCreateGraphicsPipelines(native.device, VK_NULL_HANDLE, 1, &pc, nullptr,
                                             &pair.sdf),
                   "vkCreateGraphicsPipelines(SDF)"))
            return VK_NULL_HANDLE;
        if (options.painter == vulkan_painter_mode::INTEGRATED)
        {
            if (!packed_vertex_shader)
            {
                VkShaderModuleCreateInfo sc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                sc.codeSize = sizeof(vulkan_native_packed_vert);
                sc.pCode = vulkan_native_packed_vert;
                if (!check(vkCreateShaderModule(native.device, &sc, nullptr, &packed_vertex_shader),
                           "vkCreateShaderModule(packed vertex)"))
                {
                    for (auto pipe : {pair.sdf, pair.copy, pair.blend})
                        vkDestroyPipeline(native.device, pipe, nullptr);
                    return VK_NULL_HANDLE;
                }
            }
            stages[0].module = packed_vertex_shader;
            vi.vertexBindingDescriptionCount = vi.vertexAttributeDescriptionCount = 0;
            if (!check(vkCreateGraphicsPipelines(native.device, VK_NULL_HANDLE, 1, &pc, nullptr,
                                                 &pair.packed),
                       "vkCreateGraphicsPipelines(packed)"))
            {
                for (auto pipe : {pair.sdf, pair.copy, pair.blend})
                    vkDestroyPipeline(native.device, pipe, nullptr);
                return VK_NULL_HANDLE;
            }
        }
    }
    pipelines.push_back(pair);
    return sdf ? pair.sdf : blend ? pair.blend : pair.copy;
}
VkPipeline device::packed_pipeline(VkFormat format) const
{
    for (const auto &p : pipelines)
        if (p.format == format) return p.packed;
    return VK_NULL_HANDLE;
}
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
void device::bind_graphics(const graphics_binding &binding)
{
    auto &old = frame->graphics;
    if (old.pipeline != binding.pipeline)
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, binding.pipeline);
    const bool resized =
        old.extent.width != binding.extent.width || old.extent.height != binding.extent.height;
    if (resized)
    {
        const VkViewport viewport{
            0, 0, static_cast<f32>(binding.extent.width), static_cast<f32>(binding.extent.height),
            0, 1};
        vkCmdSetViewport(frame->command, 0, 1, &viewport);
    }
    if (std::memcmp(&old.scissor, &binding.scissor, sizeof(VkRect2D)))
        vkCmdSetScissor(frame->command, 0, 1, &binding.scissor);
    if (resized || old.routed != binding.routed || old.shape_base != binding.shape_base ||
        old.vertex_words != binding.vertex_words)
    {
        struct projection
        {
            f32 width, height;
            u32 routed, shape_base, vertex_words;
        } extent{static_cast<f32>(binding.extent.width), static_cast<f32>(binding.extent.height),
                 binding.routed, binding.shape_base, binding.vertex_words};
        const bool sdf_layout_active = paint_detail::sdf_painter(options.painter);
        // Compute-only push constants do not disturb vertex/fragment values.
        vkCmdPushConstants(
            frame->command, pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT | (sdf_layout_active ? VK_SHADER_STAGE_FRAGMENT_BIT : 0), 0,
            options.painter == vulkan_painter_mode::INTEGRATED ? sizeof(extent)
            : sdf_layout_active                                ? 16
                                                               : 8,
            &extent);
    }
    if (old.sample != binding.sample)
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0,
                                1, &binding.sample, 0, nullptr);
    old = binding;
}
VkDescriptorSet device::sdf_descriptor(const VkDescriptorBufferInfo (&buffers)[4])
{
    if (frame->sdf_descriptor_cursor >= frame->sdf_descriptors.size())
    {
        fail("unprepared SDF descriptor capacity");
        return VK_NULL_HANDLE;
    }
    const auto set = frame->sdf_descriptors[frame->sdf_descriptor_cursor++];
    VkWriteDescriptorSet writes[6]{};
    for (u32 i = 0; i < 4; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &buffers[i];
    }
    VkDescriptorImageInfo images[2]{};
    const bool integrated = options.painter == vulkan_painter_mode::INTEGRATED;
    if (integrated)
    {
        images[0] = {samplers[0], glyph_source_image->view,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[1] = {VK_NULL_HANDLE, glyph_source_image->view, VK_IMAGE_LAYOUT_GENERAL};
        for (u32 i = 0; i < 2; ++i)
        {
            auto &write = writes[4 + i];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = set;
            write.dstBinding = 4 + i;
            write.descriptorCount = 1;
            write.descriptorType =
                i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &images[i];
        }
    }
    vkUpdateDescriptorSets(native.device, integrated ? 6 : 4, writes, 0, nullptr);
    return set;
}
void device::set_target(texture_handle handle)
{
    auto *t = static_cast<texture *>(handle);
    if (!t && current_surface) t = current_surface->scene;
    if (t && (!valid(t) || !t->target))
    {
        fail("invalid Vulkan render target");
        return;
    }
    if (target != t)
    {
        if (!flush_program()) return;
        command_clip_valid = false;
        end_rendering();
        target = t;
        has_clip = t && t->clipped;
        clip = t ? t->clip : rect{};
    }
}
void device::clear(color c)
{
    if (!flush_program()) return;
    if (!target && current_surface) target = current_surface->scene;
    if (current_surface && current_surface->suspended && target == current_surface->scene) return;
    if (cache_active && cache_add(cache_event::CLEAR, nullptr, nullptr, 0, nullptr, 0, c)) return;
    if (!valid(target) || !commands()) return;
    target->producer = nullptr;
    retained_draw retained_clear;
    retained_clear.key[0] = 1;
    std::memcpy(&retained_clear.key[6], &c, sizeof(c));
    retained_add(retained_clear, target, nullptr, true);
    if (retained_deferred)
    {
        ++target->revision;
        return;
    }
    end_rendering();
    transition(target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue value{};
    value.float32[0] = c.r / 255.0f;
    value.float32[1] = c.g / 255.0f;
    value.float32[2] = c.b / 255.0f;
    value.float32[3] = c.a / 255.0f;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(frame->command, target->image, target->layout, &value, 1, &range);
    ++target->revision;
}
bool device::native_scissor(VkRect2D &out) const
{
    if (!target) return false;
    double left = 0, top = 0, right = target->width, bottom = target->height;
    if (has_clip)
    {
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) ||
            !std::isfinite(clip.h))
            return false;
        const double x = std::trunc(static_cast<double>(clip.x)),
                     y = std::trunc(static_cast<double>(clip.y));
        left = x > 0 ? x : 0;
        top = y > 0 ? y : 0;
        const double end_x = x + std::trunc(static_cast<double>(clip.w));
        const double end_y = y + std::trunc(static_cast<double>(clip.h));
        if (end_x < right) right = end_x;
        if (end_y < bottom) bottom = end_y;
    }
    if (right <= left || bottom <= top) return false;
    if (replay_clipped)
    {
        if (replay_clip.x > left) left = replay_clip.x;
        if (replay_clip.y > top) top = replay_clip.y;
        if (replay_clip.right() < right) right = replay_clip.right();
        if (replay_clip.bottom() < bottom) bottom = replay_clip.bottom();
        if (right <= left || bottom <= top) return false;
    }
    out = {{static_cast<i32>(left), static_cast<i32>(top)},
           {static_cast<u32>(right - left), static_cast<u32>(bottom - top)}};
    return true;
}
void device::paint(texture *sample, const vertex *v, i32 vc, const i32 *idx, i32 ic, bool blend,
                   const retained_draw *snapshot)
{
    struct recording_scope
    {
        u32 &depth;
        explicit recording_scope(u32 &value) : depth(value) { ++depth; }
        ~recording_scope() { --depth; }
    } recording(paint_depth);
    if (current_surface && current_surface->suspended && target == current_surface->scene) return;
    if (!valid(target) || !sample || !v || !idx || vc <= 0 || ic <= 0 || ic % 3) return;
    if (!valid(sample))
    {
        fail("foreign or retired Vulkan texture");
        return;
    }
    if (sample == target)
    {
        fail("Vulkan render target cannot sample itself");
        return;
    }
    for (i32 i = 0; i < ic; ++i)
        if (idx[i] < 0 || idx[i] >= vc)
        {
            fail("Vulkan index out of bounds");
            return;
        }
    VkRect2D native_clip{};
    if (!native_scissor(native_clip)) return;
    target->producer = nullptr;
    const double left = native_clip.offset.x, top = native_clip.offset.y;
    const double right = left + native_clip.extent.width, bottom = top + native_clip.extent.height;
    const bool sdf = blend && paint_detail::sdf_painter(options.painter);
    bool deferred = !snapshot && sdf && program_batching() && sdf_tape.valid;
    bool descriptor_fallback = snapshot != nullptr;
    if (!deferred && !flush_program()) return;
    if (deferred && program_count &&
        (program_count >= 63 || frame->sdf_descriptor_cursor + 2 >= frame->sdf_descriptors.size()))
    {
        if (!flush_program()) return;
        // A program needs one set in addition to each native draw. Spend a
        // final available set on complete raster rather than forcing a wait
        // solely to add another compute descriptor for this small suffix.
        if (frame->sdf_descriptor_cursor + 1 >= frame->sdf_descriptors.size())
        {
            deferred = false;
            descriptor_fallback = true;
            ++stats.gpu_sdf_capacity_bypasses;
        }
    }
    if (retaining && retaining->valid && !snapshot)
    {
        const usize source_bytes = static_cast<usize>(vc) * sizeof(vertex);
        const usize index_bytes = static_cast<usize>(ic) * sizeof(i32);
        if (source_bytes > UINT32_MAX || index_bytes > UINT32_MAX)
            retained_abort();
        else
        {
            retained_draw event;
            event.key[0] = 2;
            std::memcpy(&event.key[6], &native_clip, sizeof(native_clip));
            event.key[8] = blend;
            event.key[9] = deferred ? program_shape_end : 0;
            event.vertices = reinterpret_cast<const u8 *>(v);
            event.indices = reinterpret_cast<const u8 *>(idx);
            event.shapes = reinterpret_cast<const u8 *>(sdf_shapes.data() +
                                                        (deferred ? program_shape_end : 0));
            event.vertex_count = vc;
            event.index_bytes = static_cast<u32>(index_bytes);
            event.shape_bytes =
                sdf ? static_cast<u32>((sdf_shapes.size() - (deferred ? program_shape_end : 0)) *
                                       sizeof(sdf_shape))
                    : 0;
            event.stride = sizeof(vertex);
            event.span_bytes = sdf ? static_cast<u32>(sdf_spans.size() * 12) : 0;
            event.key[10] = sdf;
            const bool replaces = !blend && vc == 4 && ic == 6 && !has_clip && v[0].x <= 0 &&
                                  v[0].y <= 0 && v[2].x >= target->width &&
                                  v[2].y >= target->height;
            retained_add(event, target, sample, replaces);
            if (retained_deferred)
            {
                ++target->revision;
                return;
            }
        }
    }
    if (sdf && !commands()) return;
    if (sdf && !upload_slug_data()) return;
    if (sdf && !fill_glyph_sources()) return;
    if (sdf && frame->sdf_descriptor_cursor +
                       (options.painter == vulkan_painter_mode::INTEGRATED ? 2u : 1u) >
                   frame->sdf_descriptors.size())
    {
        // Reuse the already allocated sets after completion. Do this before
        // reserving the metadata/geometry lease, never between it and its draw.
        const auto serial = submit();
        if (!serial || !wait(serial) || !begin(*frame)) return;
        ++stats.budget_waits;
    }
    sdf_gpu_job *compute = nullptr;
    VkDescriptorSet shapes = VK_NULL_HANDLE;
    const bool packed =
        sdf && options.painter == vulkan_painter_mode::INTEGRATED &&
        packed_pipeline(target->format) &&
        static_cast<u64>(vc) * 24 + properties.limits.minStorageBufferOffsetAlignment <=
            properties.limits.maxStorageBufferRange;
    const VkDeviceSize stride = packed ? 24 : sdf ? 48 : 32;
    const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(vc) * stride;
    const VkDeviceSize index_bytes = static_cast<VkDeviceSize>(ic) * 4;
    const usize shape_count = snapshot ? snapshot->shape_bytes / sizeof(sdf_shape)
                                       : sdf_shapes.size() - (deferred ? program_shape_end : 0);
    const auto *shape_data =
        snapshot
            ? snapshot->shapes
            : reinterpret_cast<const u8 *>(sdf_shapes.data() + (deferred ? program_shape_end : 0));
    const VkDeviceSize bytes = sdf ? std::max<usize>(1, shape_count) * sizeof(sdf_shape) : 0;
    const VkDeviceSize alignment = sdf ? properties.limits.minStorageBufferOffsetAlignment : 1;
    const bool outline_upload = [&]()
    {
        if (!sdf || slug_data.empty()) return false;
        if (slug_storage.device_buffer && slug_storage.used == slug_data.size()) return false;
        // The resident prefix ends at a complete glyph. Appending an outline
        // beyond its budget must not evict old pages or penalize shape-only
        // batches. Snapshot payloads need not have sdf_shape alignment.
        for (usize i = 0; i < shape_count; ++i)
        {
            sdf_shape shape;
            std::memcpy(&shape, shape_data + i * sizeof(shape), sizeof(shape));
            if (shape.reserved[0] != 1) continue;
            u32 location;
            std::memcpy(&location, &shape.radii.bl, sizeof(location));
            if (!slug_storage.device_buffer || static_cast<u64>(location) * 8 >= slug_storage.used)
                return true;
        }
        return false;
    }();
    const bool resident_text =
        sdf && slug_storage.device_buffer && slug_storage.used >= 8 && !outline_upload;
    const VkDeviceSize outline_bytes = sdf ? (outline_upload ? slug_data.size() : 8) : 0;
    const VkDeviceSize outline_at = (bytes + alignment - 1) & ~(alignment - 1);
    const VkDeviceSize prefix = sdf ? outline_at + outline_bytes + alignment + 31 : 0;
    // Reserve metadata and geometry together. A budget-induced submission can
    // then occur only before descriptors/jobs are selected, never between their
    // creation and the graphics draw that consumes the same upload storage.
    VkDeviceSize base = 0;
    page *p = upload(prefix + vertex_bytes + index_bytes, base, true);
    if (!p) return;
    const VkDeviceSize at = (base + alignment - 1) & ~(alignment - 1);
    const VkDeviceSize offset =
        sdf ? (at + outline_at + outline_bytes + 31) & ~VkDeviceSize(31) : base;
    if (sdf)
    {
        if (options.painter == vulkan_painter_mode::INTEGRATED)
            seal_program_metadata(sample, native_clip, static_cast<u32>(ic));
        if (!deferred && !descriptor_fallback && sdf_tape.valid &&
            paint_detail::compute_painter(options.painter))
            compute = prepare_sdf_compute();
        if (!shape_count)
            std::memset(p->mapped + at, 0, sizeof(sdf_shape));
        else
            std::memcpy(p->mapped + at, shape_data, static_cast<usize>(bytes));
        if (!outline_upload)
            std::memset(p->mapped + at + outline_at, 0, 8);
        else
        {
            std::memcpy(p->mapped + at + outline_at, slug_data.data(), slug_data.size());
            stats.glyph_upload_bytes += slug_data.size();
        }
        if (resident_text) ++stats.glyph_resident_batches;
        VkDescriptorBufferInfo buffers[4] = {
            compute ? sdf_shape_data(compute) : VkDescriptorBufferInfo{p->buffer, at, bytes},
            resident_text ? VkDescriptorBufferInfo{slug_storage.device_buffer, 0, slug_storage.used}
                          : VkDescriptorBufferInfo{p->buffer, at + outline_at, outline_bytes},
            compute ? sdf_route(compute) : VkDescriptorBufferInfo{p->buffer, at, 4},
            compute ? sdf_lut() : VkDescriptorBufferInfo{p->buffer, at, 4}};
        VkDescriptorSet compute_shapes = VK_NULL_HANDLE;
        if (compute && packed)
        {
            compute_shapes = sdf_descriptor(buffers);
            if (!compute_shapes) return;
        }
        if (packed)
        {
            const VkDeviceSize start = offset & ~(alignment - 1);
            buffers[3] = {p->staged ? p->device_buffer : p->buffer, start,
                          offset - start + vertex_bytes};
        }
        shapes = sdf_descriptor(buffers);
        if (!shapes) return;
        if (compute && !execute_sdf_compute(compute, packed ? compute_shapes : shapes, buffers[0]))
            return;
    }
    struct gpu_vertex
    {
        float x, y, u, v, r, g, b, a;
    };
    const auto upload_vertices = [&]<bool USE_TABLE>()
    {
        constexpr VkDeviceSize vertex_stride = USE_TABLE ? 48 : 32;
        const auto channel = [](u8 value)
        {
            if constexpr (USE_TABLE)
                return BYTE_CHANNELS.values[value];
            else
                return value / 255.0f;
        };
        for (i32 i = 0; i < vc; ++i)
        {
            const gpu_vertex value = {v[i].x,
                                      v[i].y,
                                      v[i].u,
                                      v[i].v,
                                      channel(v[i].c.r),
                                      channel(v[i].c.g),
                                      channel(v[i].c.b),
                                      channel(v[i].c.a)};
            std::memcpy(p->mapped + offset + i * vertex_stride, &value, sizeof(value));
            if constexpr (USE_TABLE)
            {
                const u32 key = 0xffffffffu;
                std::memcpy(p->mapped + offset + i * vertex_stride + 32, &key, sizeof(key));
            }
        }
    };
    if (packed)
    {
        struct packed_vertex
        {
            f32 x, y, u, v;
            u32 rgba, shape;
        };
        static_assert(sizeof(packed_vertex) == 24);
        for (i32 i = 0; i < vc; ++i)
        {
            const color tint = v[i].c;
            const u32 rgba = static_cast<u32>(tint.r) | (static_cast<u32>(tint.g) << 8) |
                             (static_cast<u32>(tint.b) << 16) | (static_cast<u32>(tint.a) << 24);
            const packed_vertex value{v[i].x, v[i].y, v[i].u, v[i].v, rgba, 0xffffffffu};
            std::memcpy(p->mapped + offset + static_cast<u64>(i) * sizeof(value), &value,
                        sizeof(value));
        }
    }
    else if (sdf)
        upload_vertices.template operator()<true>();
    else
        upload_vertices.template operator()<false>();
    if (sdf)
    {
        const auto write_span = [&](usize vertex, u32 shape)
        {
            if (vertex + 4 > static_cast<usize>(vc) ||
                shape >= (snapshot ? shape_count : sdf_shapes.size()))
            {
                fail("SDF span outside batch");
                return false;
            }
            for (usize i = 0; i < 4; ++i)
                std::memcpy(p->mapped + offset + (vertex + i) * stride + (packed ? 20 : 32), &shape,
                            sizeof(shape));
            return true;
        };
        if (snapshot)
        {
            for (usize i = 0; i < snapshot->span_bytes / 12; ++i)
            {
                u64 vertex;
                u32 shape;
                std::memcpy(&vertex, snapshot->spans + i * 12, 8);
                std::memcpy(&shape, snapshot->spans + i * 12 + 8, 4);
                if (!write_span(static_cast<usize>(vertex), shape)) return;
            }
        }
        else
            for (const auto &span : sdf_spans)
                if (!write_span(span.vertex, span.shape)) return;
    }
    std::memcpy(p->mapped + offset + vertex_bytes, idx, static_cast<usize>(index_bytes));
    const VkBuffer geometry_buffer = p->staged ? p->device_buffer : p->buffer;
    if (p->staged)
    {
        end_rendering();
        const VkBufferCopy copy{offset, offset, vertex_bytes + index_bytes};
        // A previous indexed draw can read this buffer. Order its input reads
        // before another transfer, including the conservative indexed-fetch
        // range used by synchronization validation.
        VkBufferMemoryBarrier2 before{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        before.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        before.srcAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
        if (options.painter == vulkan_painter_mode::INTEGRATED)
        {
            before.srcStageMask |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            before.srcAccessMask |= VK_ACCESS_2_SHADER_READ_BIT;
        }
        before.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        before.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        before.srcQueueFamilyIndex = before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        before.buffer = p->device_buffer;
        before.size = VK_WHOLE_SIZE;
        VkDependencyInfo prior{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        prior.bufferMemoryBarrierCount = 1;
        prior.pBufferMemoryBarriers = &before;
        vkCmdPipelineBarrier2(frame->command, &prior);
        vkCmdCopyBuffer(frame->command, p->buffer, p->device_buffer, 1, &copy);
        VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
        if (options.painter == vulkan_painter_mode::INTEGRATED)
        {
            barrier.dstStageMask |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            barrier.dstAccessMask |= VK_ACCESS_2_SHADER_READ_BIT;
        }
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = p->device_buffer;
        barrier.offset = offset;
        barrier.size = copy.size;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(frame->command, &dep);
    }
    if (deferred)
    {
        if (!program_count) program_target = target;
        program_draws[program_count++] = {
            sample,
            geometry_buffer,
            offset,
            vertex_bytes,
            shapes,
            {{static_cast<i32>(left), static_cast<i32>(top)},
             {static_cast<u32>(right - left), static_cast<u32>(bottom - top)}},
            static_cast<u32>(vc),
            static_cast<u32>(ic),
            static_cast<u32>(program_shape_end),
            packed ? static_cast<u32>((offset & (alignment - 1)) / 4) : 0,
            packed};
        program_shape_end = sdf_shapes.size();
        program_command_end = sdf_tape.commands.size();
        program_payload_end = sdf_tape.payload.size();
        target->last_use = sample->last_use = next_serial;
        ++target->revision;
        ++stats.draws;
        stats.vertices += vc;
        stats.indices += ic;
        return;
    }
    // Transition sampled images outside a render instance; LOAD preserves the
    // already recorded prefix when streaming updates or effects split a pass.
    if (sample->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || sample->ready)
        end_rendering();
    transition(sample, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!start_rendering(target)) return;
    const VkPipeline pipe =
        packed ? packed_pipeline(target->format) : pipeline(target->format, blend, sdf);
    if (!pipe) return;
    VkRect2D scissor{{static_cast<i32>(left), static_cast<i32>(top)},
                     {static_cast<u32>(right - left), static_cast<u32>(bottom - top)}};
    bind_graphics({.pipeline = pipe,
                   .extent = {static_cast<u32>(target->width), static_cast<u32>(target->height)},
                   .scissor = scissor,
                   .sample = sample->descriptors[!blend || sample->linear ? 1 : 0],
                   .routed = compute ? 1u : 0u,
                   .vertex_words = packed ? static_cast<u32>((offset & (alignment - 1)) / 4) : 0});
    if (!packed) vkCmdBindVertexBuffers(frame->command, 0, 1, &geometry_buffer, &offset);
    vkCmdBindIndexBuffer(frame->command, geometry_buffer, offset + vertex_bytes,
                         VK_INDEX_TYPE_UINT32);
    if (sdf)
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1,
                                1, &shapes, 0, nullptr);
    if (compute && sdf_functions)
        replay_sdf_native(compute, 0);
    else
        vkCmdDrawIndexed(frame->command, static_cast<u32>(ic), 1, 0, 0, 0);
    target->last_use = sample->last_use = next_serial;
    ++target->revision;
    ++stats.draws;
    stats.vertices += vc;
    stats.indices += ic;
    if (compute) finish_sdf_compute(compute, shapes);
}
void device::draw(texture_handle tex, const vertex *v, i32 vc, const i32 *idx, i32 ic)
{
    struct batch_reset
    {
        device &owner;
        ~batch_reset()
        {
            owner.sdf_spans.clear();
            owner.primitive_commands.pending_shape = 0xffffffffu;
            if (!owner.program_count)
            {
                owner.sdf_shapes.clear();
                owner.sdf_tape.commands.clear();
                owner.sdf_tape.payload.clear();
                owner.sdf_tape.next_serial = 1;
                owner.sdf_tape.valid =
                    owner.sdf_tape.command_limit != 0 && !owner.native_only_program();
            }
        }
    } reset{*this};
    ++stats.requested_draws;
    if (vc > 0) stats.requested_vertices += static_cast<u64>(vc);
    if (ic > 0) stats.requested_indices += static_cast<u64>(ic);
    auto *sample = tex ? static_cast<texture *>(tex) : white;
    if (cache_active && cache_add(cache_event::DRAW, sample, v, vc, idx, ic)) return;
    paint(sample, v, vc, idx, ic, true);
}
bool device::start_rendering(texture *t)
{
    if (!commands() || !valid(t)) return false;
    if (rendering_target == t) return true;
    end_rendering();
    transition(t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = t->view;
    attachment.imageLayout = t->layout;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent = {static_cast<u32>(t->width), static_cast<u32>(t->height)};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &attachment;
    vkCmdBeginRendering(frame->command, &ri);
    rendering_target = t;
    return true;
}
void device::blit(texture_handle source, const rect *sr, texture_handle destination, const rect *dr)
{
    auto *s = static_cast<texture *>(source);
    if (!valid(s)) return;
    set_target(destination);
    if (!valid(target)) return;
    if (s == target)
    {
        auto *saved = target;
        texture *snapshot = make_texture(s->width, s->height, true, nullptr);
        if (!snapshot) return;
        blit(s, nullptr, snapshot, nullptr);
        blit(snapshot, sr, saved, dr);
        destroy_texture(snapshot);
        return;
    }
    const rect source_rect =
        sr ? *sr : rect::make(0, 0, static_cast<float>(s->width), static_cast<float>(s->height));
    const rect dest_rect = dr ? *dr
                              : rect::make(0, 0, static_cast<float>(target->width),
                                           static_cast<float>(target->height));
    if (source_rect.w <= 0 || source_rect.h <= 0 || dest_rect.w <= 0 || dest_rect.h <= 0) return;
    if (cache_active &&
        cache_add(cache_event::COPY, s, nullptr, 0, nullptr, 0, {}, &source_rect, &dest_rect))
        return;
    const float l = source_rect.x / s->width, t = source_rect.y / s->height;
    const float r = source_rect.right() / s->width, b = source_rect.bottom() / s->height;
    const color white_color{255, 255, 255, 255};
    const vertex v[] = {{dest_rect.x, dest_rect.y, l, t, white_color},
                        {dest_rect.right(), dest_rect.y, r, t, white_color},
                        {dest_rect.right(), dest_rect.bottom(), r, b, white_color},
                        {dest_rect.x, dest_rect.bottom(), l, b, white_color}};
    const i32 indices[] = {0, 1, 2, 0, 2, 3};
    paint(s, v, 4, indices, 6, false);
}
bool device::flush_program()
{
    if (!program_count || program_flushing) return true;
    program_flushing = true;
    const auto saved_target = target;
    const auto saved_clip = clip;
    const auto saved_has_clip = has_clip;
    target = program_target;
    has_clip = false;
    auto *compute = prepare_sdf_compute();
    VkDescriptorSet compute_shapes = VK_NULL_HANDLE;
    if (compute)
    {
        const VkDescriptorBufferInfo info[4] = {
            sdf_shape_data(compute),
            slug_storage.device_buffer ? VkDescriptorBufferInfo{slug_storage.device_buffer, 0,
                                                                std::max<u64>(8, slug_storage.used)}
                                       : sdf_shape_data(compute),
            sdf_route(compute), sdf_lut()};
        compute_shapes = sdf_descriptor(info);
        if (!compute_shapes || !execute_sdf_compute(compute, compute_shapes, info[0])) return false;
    }
    for (usize n = 0; n < program_count; ++n)
    {
        const auto &packet = program_draws[n];
        auto *sample = packet.sample;
        const auto geometry_buffer = packet.geometry;
        const auto offset = packet.offset, vertex_bytes = packet.vertex_bytes;
        const auto ic = packet.indices;
        const auto shapes = packet.shapes;
        if (compute)
        {
            const VkDescriptorBufferInfo route = sdf_route(compute);
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = shapes;
            write.dstBinding = 2;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &route;
            vkUpdateDescriptorSets(native.device, 1, &write, 0, nullptr);
        }
        // Transition sampled images outside a render instance; LOAD preserves the
        // already recorded prefix when streaming updates or effects split a pass.
        if (sample->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || sample->ready)
            end_rendering();
        transition(sample, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!start_rendering(target)) return false;
        const VkPipeline pipe =
            packet.packed ? packed_pipeline(target->format) : pipeline(target->format, true, true);
        if (!pipe) return false;
        VkRect2D scissor = packet.scissor;
        bind_graphics(
            {.pipeline = pipe,
             .extent = {static_cast<u32>(target->width), static_cast<u32>(target->height)},
             .scissor = scissor,
             .sample = sample->descriptors[sample->linear ? 1 : 0],
             .routed = compute ? 1u : 0u,
             .shape_base = packet.shape_base,
             .vertex_words = packet.vertex_words});
        if (!packet.packed) vkCmdBindVertexBuffers(frame->command, 0, 1, &geometry_buffer, &offset);
        vkCmdBindIndexBuffer(frame->command, geometry_buffer, offset + vertex_bytes,
                             VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1,
                                1, &shapes, 0, nullptr);
        if (compute && sdf_functions)
            replay_sdf_native(compute, static_cast<u32>(n));
        else
            vkCmdDrawIndexed(frame->command, static_cast<u32>(ic), 1, 0, 0, 0);
    }
    if (compute && !finish_sdf_compute(compute, compute_shapes)) return false;
    program_count = 0;
    const usize shapes_left = sdf_shapes.size() - program_shape_end;
    const usize commands_left = sdf_tape.commands.size() - program_command_end;
    if (shapes_left)
        std::memmove(sdf_shapes.data(), sdf_shapes.data() + program_shape_end,
                     shapes_left * sizeof(sdf_shape));
    if (commands_left)
        std::memmove(sdf_tape.commands.data(), sdf_tape.commands.data() + program_command_end,
                     commands_left * sizeof(paint_detail::command));
    sdf_shapes.resize(shapes_left);
    sdf_tape.commands.resize(commands_left);
    if (program_payload_end)
    {
        const usize payload_left = sdf_tape.payload.size() - program_payload_end;
        if (payload_left)
            std::memmove(sdf_tape.payload.data(), sdf_tape.payload.data() + program_payload_end,
                         payload_left);
        sdf_tape.payload.resize(payload_left);
        for (auto &command : sdf_tape.commands)
            command.payload_offset -= static_cast<u32>(program_payload_end);
    }
    for (auto &command : sdf_tape.commands)
        if (command.operation == 2) command.resource -= static_cast<u32>(program_shape_end);
    for (auto &span : sdf_spans) span.shape -= static_cast<u32>(program_shape_end);
    if (primitive_commands.pending_shape != 0xffffffffu)
        primitive_commands.pending_shape -= static_cast<u32>(program_shape_end);
    program_shape_end = program_command_end = program_payload_end = 0;
    sdf_tape.next_serial = commands_left + 1;
    sdf_tape.valid = sdf_tape.command_limit != 0 && !native_only_program();
    target = saved_target;
    clip = saved_clip;
    has_clip = saved_has_clip;
    program_flushing = false;
    return result == VK_SUCCESS;
}
} // namespace vk_detail
#endif
