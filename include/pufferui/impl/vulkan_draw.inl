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
VkPipeline device::pipeline(VkFormat format, bool blend, bool glyphs)
{
    for (auto &p : pipelines)
        if (p.format == format) return glyphs ? p.text : (blend ? p.blend : p.copy);
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
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (text)
    {
        stages[0].module = text->vertex;
        stages[1].module = text->fragment;
        binding.stride = sizeof(glyph_instance);
        binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        const VkVertexInputAttributeDescription text_attributes[] = {
            {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32},
            {3, 0, VK_FORMAT_R32_UINT, 48}};
        vi.vertexAttributeDescriptionCount = 4;
        vi.pVertexAttributeDescriptions = text_attributes;
        pc.layout = text->pipeline_layout;
        attachment.blendEnable = VK_TRUE;
        if (!check(vkCreateGraphicsPipelines(native.device, VK_NULL_HANDLE, 1, &pc, nullptr,
                                             &pair.text),
                   "vkCreateGraphicsPipelines(Slug)"))
        {
            vkDestroyPipeline(native.device, pair.blend, nullptr);
            vkDestroyPipeline(native.device, pair.copy, nullptr);
            return VK_NULL_HANDLE;
        }
    }
#endif
    pipelines.push_back(pair);
    return blend ? pair.blend : pair.copy;
}
void device::set_target(texture_handle handle)
{
    flush_text();
    auto *t = static_cast<texture *>(handle);
    if (!t && current_surface) t = current_surface->scene;
    if (t && (!valid(t) || !t->target))
    {
        fail("invalid Vulkan render target");
        return;
    }
    if (target != t)
    {
        end_rendering();
        target = t;
        has_clip = t && t->clipped;
        clip = t ? t->clip : rect{};
    }
}
void device::clear(color c)
{
    flush_text();
    if (!target && current_surface) target = current_surface->scene;
    if (current_surface && current_surface->suspended && target == current_surface->scene) return;
    if (cache_active && cache_add(cache_event::CLEAR, nullptr, nullptr, 0, nullptr, 0, c)) return;
    if (!valid(target) || !commands()) return;
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
void device::paint(texture *sample, const vertex *v, i32 vc, const i32 *idx, i32 ic, bool blend,
                   i32 index_base)
{
    paint_guard drawing(*this);
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
        if (idx[i] < index_base || static_cast<i64>(idx[i]) - index_base >= vc)
        {
            fail("Vulkan index out of bounds");
            return;
        }
    if (!commands()) return;
    const VkDeviceSize chunk =
        frame->geometry_chunk_bytes
            ? frame->geometry_chunk_bytes
            : std::min<VkDeviceSize>(
                  1024 * 1024, options.upload_budget_bytes /
                                   (options.upload_mode == vulkan_upload_mode::STAGED ? 2 : 1));
    if (static_cast<VkDeviceSize>(vc) * 32 + static_cast<VkDeviceSize>(ic) * 4 > chunk &&
        chunk >= 108)
    {
        // Preserve triangle order and each original vertex's float arithmetic.
        // Dense index spans share their contiguous source vertices. A sparse
        // triangle uses three stack copies rather than a huge unused interval.
        for (i32 start = 0; start < ic;)
        {
            i32 end = start, low = 0x7fffffff, high = 0;
            while (end < ic)
            {
                const i32 lo =
                    std::min(low, std::min(idx[end], std::min(idx[end + 1], idx[end + 2])));
                const i32 hi =
                    std::max(high, std::max(idx[end], std::max(idx[end + 1], idx[end + 2])));
                if ((static_cast<VkDeviceSize>(hi) - lo + 1) * 32 +
                        static_cast<VkDeviceSize>(end - start + 3) * 4 >
                    chunk)
                    break;
                low = lo;
                high = hi;
                end += 3;
            }
            if (end == start)
            {
                const vertex triangle[] = {v[idx[start] - index_base],
                                           v[idx[start + 1] - index_base],
                                           v[idx[start + 2] - index_base]};
                const i32 local[] = {0, 1, 2};
                paint(sample, triangle, 3, local, 3, blend);
                start += 3;
            }
            else
            {
                paint(sample, v + low - index_base, high - low + 1, idx + start, end - start, blend,
                      low);
                start = end;
            }
        }
        return;
    }
    double left = 0, top = 0, right = target->width, bottom = target->height;
    if (has_clip)
    {
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) ||
            !std::isfinite(clip.h))
            return;
        const double x = std::trunc(static_cast<double>(clip.x)),
                     y = std::trunc(static_cast<double>(clip.y));
        left = x > 0 ? x : 0;
        top = y > 0 ? y : 0;
        const double end_x = x + std::trunc(static_cast<double>(clip.w));
        const double end_y = y + std::trunc(static_cast<double>(clip.h));
        if (end_x < right) right = end_x;
        if (end_y < bottom) bottom = end_y;
    }
    if (right <= left || bottom <= top) return;
    if (replay_clipped)
    {
        if (replay_clip.x > left) left = replay_clip.x;
        if (replay_clip.y > top) top = replay_clip.y;
        if (replay_clip.right() < right) right = replay_clip.right();
        if (replay_clip.bottom() < bottom) bottom = replay_clip.bottom();
        if (right <= left || bottom <= top) return;
    }
    const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(vc) * 32;
    const VkDeviceSize index_bytes = static_cast<VkDeviceSize>(ic) * 4;
    VkDeviceSize offset = 0;
    page *p = upload(vertex_bytes + index_bytes, offset, true);
    if (!p) return;
    struct gpu_vertex
    {
        float x, y, u, v, r, g, b, a;
    };
    auto *vertices = reinterpret_cast<gpu_vertex *>(p->mapped + offset);
    for (i32 i = 0; i < vc; ++i)
        vertices[i] = {v[i].x,
                       v[i].y,
                       v[i].u,
                       v[i].v,
                       BYTE_CHANNELS.values[v[i].c.r],
                       BYTE_CHANNELS.values[v[i].c.g],
                       BYTE_CHANNELS.values[v[i].c.b],
                       BYTE_CHANNELS.values[v[i].c.a]};
    if (!index_base)
        std::memcpy(p->mapped + offset + vertex_bytes, idx, static_cast<usize>(index_bytes));
    else
    {
        auto *out = reinterpret_cast<i32 *>(p->mapped + offset + vertex_bytes);
        for (i32 i = 0; i < ic; ++i) out[i] = idx[i] - index_base;
    }
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
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = p->device_buffer;
        barrier.offset = offset;
        barrier.size = copy.size;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(frame->command, &dep);
    }
    // Transition sampled images outside a render instance; LOAD preserves the
    // already recorded prefix when streaming updates or effects split a pass.
    if (sample->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || sample->ready)
        end_rendering();
    transition(sample, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!start_rendering(target)) return;
    const VkPipeline pipe = pipeline(target->format, blend);
    if (!pipe) return;
    VkRect2D scissor{{static_cast<i32>(left), static_cast<i32>(top)},
                     {static_cast<u32>(right - left), static_cast<u32>(bottom - top)}};
    vkCmdBindVertexBuffers(frame->command, 0, 1, &geometry_buffer, &offset);
    vkCmdBindIndexBuffer(frame->command, geometry_buffer, offset + vertex_bytes,
                         VK_INDEX_TYPE_UINT32);
    const VkDescriptorSet descriptor = sample->descriptors[!blend || sample->linear ? 1 : 0];
    bind_graphics(pipe, pipeline_layout,
                  {static_cast<u32>(target->width), static_cast<u32>(target->height)}, scissor,
                  descriptor, 0);
    vkCmdDrawIndexed(frame->command, static_cast<u32>(ic), 1, 0, 0, 0);
    target->last_use = sample->last_use = next_serial;
    ++target->revision;
    ++stats.draws;
    stats.vertices += vc;
    stats.indices += ic;
}
void device::draw(texture_handle tex, const vertex *v, i32 vc, const i32 *idx, i32 ic)
{
    flush_text();
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
} // namespace vk_detail
#endif
