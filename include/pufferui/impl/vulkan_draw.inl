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
static_assert(sizeof(vertex) == 20 && offsetof(vertex, c) == 16);
static_assert(sizeof(color) == 4 && offsetof(color, r) == 0 && offsetof(color, g) == 1 &&
              offsetof(color, b) == 2 && offsetof(color, a) == 3);
VkPipeline device::pipeline(VkFormat format, bool blend, bool glyphs, u32 filter)
{
    for (auto &p : pipelines)
        if (p.format == format)
            return filter ? p.filters[filter - 1] : (glyphs ? p.text : (blend ? p.blend : p.copy));
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
    const VkBool32 compact = options.compact_vertices;
    const VkSpecializationMapEntry vertex_entry{0, 0, sizeof(compact)};
    const VkSpecializationInfo vertex_specialization{1, &vertex_entry, sizeof(compact), &compact};
    stages[0].pSpecializationInfo = &vertex_specialization;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_shader;
    VkVertexInputBindingDescription binding{0, static_cast<u32>(vertex_stride()),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, 8},
        {2, 0, options.compact_vertices ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R32G32B32A32_UINT,
         16}};
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
    const VkSpecializationMapEntry filter_entry{0, 0, sizeof(u32)};
    for (u32 mode = 1; format == VK_FORMAT_R8G8B8A8_UNORM && mode <= 4; ++mode)
    {
        attachment.blendEnable = mode == 4 ? VK_TRUE : VK_FALSE;
        const VkSpecializationInfo specialization{1, &filter_entry, sizeof(mode), &mode};
        stages[1].pSpecializationInfo = &specialization;
        if (!check(vkCreateGraphicsPipelines(native.device, VK_NULL_HANDLE, 1, &pc, nullptr,
                                             &pair.filters[mode - 1]),
                   "vkCreateGraphicsPipelines(blur)"))
        {
            for (auto created : pair.filters)
                if (created) vkDestroyPipeline(native.device, created, nullptr);
            vkDestroyPipeline(native.device, pair.blend, nullptr);
            vkDestroyPipeline(native.device, pair.copy, nullptr);
            return VK_NULL_HANDLE;
        }
    }
    stages[1].pSpecializationInfo = nullptr;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (text)
    {
        stages[0].module = text->vertex;
        stages[0].pSpecializationInfo = nullptr;
        stages[1].module = text->fragment;
        binding.stride = sizeof(glyph_instance);
        binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        const VkVertexInputAttributeDescription text_attributes[] = {
            {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32},
            {3, 0, VK_FORMAT_R32G32B32A32_UINT, 48}};
        vi.vertexAttributeDescriptionCount = 4;
        vi.pVertexAttributeDescriptions = text_attributes;
        pc.layout = text->pipeline_layout;
        const VkBool32 coverage = format == VK_FORMAT_R32_SFLOAT;
        const VkSpecializationMapEntry entry{0, 0, sizeof(coverage)};
        const VkSpecializationInfo specialization{1, &entry, sizeof(coverage), &coverage};
        stages[1].pSpecializationInfo = &specialization;
        attachment.blendEnable = !coverage;
        if (coverage) attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
        if (!check(vkCreateGraphicsPipelines(native.device, VK_NULL_HANDLE, 1, &pc, nullptr,
                                             &pair.text),
                   "vkCreateGraphicsPipelines(Slug)"))
        {
            vkDestroyPipeline(native.device, pair.blend, nullptr);
            vkDestroyPipeline(native.device, pair.copy, nullptr);
            for (auto created : pair.filters)
                if (created) vkDestroyPipeline(native.device, created, nullptr);
            return VK_NULL_HANDLE;
        }
    }
#endif
    pipelines.push_back(pair);
    return filter ? pair.filters[filter - 1]
                  : (glyphs ? pair.text : (blend ? pair.blend : pair.copy));
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
inline constexpr bool INTEGER_SIMD = PUI_VK_INTEGER_SIMD != 0;
#if defined(PUFFERUI_VULKAN_INTEGER_TESTS)
struct integer_work
{
    u64 validation_vectors = 0, validation_scalars = 0, planning_vectors = 0, planning_scalars = 0;
};
inline thread_local integer_work index_work;
#endif
template <bool Simd = INTEGER_SIMD>
inline bool valid_geometry_indices(const i32 *indices, i32 count, i32 base, i32 vertices)
{
    i32 at = 0;
#if PUI_VK_INTEGER_SIMD
    if constexpr (Simd)
    {
        // Clamp once in 64 bits. An interval crossing INT_MAX admits no wrapped
        // negative values; comparisons stay signed even for an INT_MIN base.
        const i64 upper = std::min<i64>(0x7fffffff, static_cast<i64>(base) + vertices - 1);
        const __m128i lower4 = _mm_set1_epi32(base);
        const __m128i upper4 = _mm_set1_epi32(static_cast<i32>(upper));
        for (; count - at >= 16; at += 16)
        {
            __m128i invalid = _mm_setzero_si128();
            for (i32 j = 0; j < 16; j += 4)
            {
                const __m128i value =
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(indices + at + j));
                invalid = _mm_or_si128(invalid, _mm_or_si128(_mm_cmpgt_epi32(lower4, value),
                                                             _mm_cmpgt_epi32(value, upper4)));
#if defined(PUFFERUI_VULKAN_INTEGER_TESTS)
                ++index_work.validation_vectors;
#endif
            }
            if (_mm_movemask_epi8(invalid)) return false;
        }
    }
#endif
    for (; at < count; ++at)
    {
#if defined(PUFFERUI_VULKAN_INTEGER_TESTS)
        ++index_work.validation_scalars;
#endif
        if (indices[at] < base || static_cast<i64>(indices[at]) - base >= vertices) return false;
    }
    return true;
}
void device::paint(texture *sample, const vertex *v, i32 vc, const i32 *idx, i32 ic, bool blend,
                   i32 index_base, const blur_filter *filter, u32 kernel)
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
    if (!valid_geometry_indices(idx, ic, index_base, vc))
    {
        fail("Vulkan index out of bounds");
        return;
    }
    paint_validated(sample, {v, vc, idx, ic, index_base}, {blend, filter, kernel});
}
struct geometry_span
{
    i32 end, low, high;
};
#if defined(PUFFERUI_VULKAN_INDEXED_TESTS)
struct geometry_span_work
{
    u64 blocks = 0, triangles = 0;
};
inline thread_local geometry_span_work span_work;
#endif
template <bool Simd = INTEGER_SIMD>
inline geometry_span plan_geometry_span(const i32 *indices, i32 start, i32 count,
                                        VkDeviceSize stride, VkDeviceSize capacity)
{
    geometry_span span{start, 0x7fffffff, 0};
    if (count - start >= 24)
    {
        // A sparse first triangle can exceed the arena on its own. Keep that
        // fallback at one triangle's reads instead of probing an entire block.
#if defined(PUFFERUI_VULKAN_INDEXED_TESTS)
        ++span_work.triangles;
#endif
        const i32 low = std::min(indices[start], std::min(indices[start + 1], indices[start + 2]));
        const i32 high =
            std::max(0, std::max(indices[start], std::max(indices[start + 1], indices[start + 2])));
        if ((static_cast<VkDeviceSize>(high) - low + 1) * stride + 3 * sizeof(i32) > capacity)
            return span;
    }
    constexpr i32 block = Simd && INTEGER_SIMD ? 192 : 24;
    while (span.end < count)
    {
        // A fitting block proves that every prefix fits: its vertex interval
        // and index bytes can only grow. Refine a boundary with the original
        // triangle rule so uploads, sparse fallback and draw order stay exact.
        if (count - span.end >= block)
        {
#if defined(PUFFERUI_VULKAN_INDEXED_TESTS)
            ++span_work.blocks;
#endif
            i32 low = 0x7fffffff, high = 0;
#if PUI_VK_INTEGER_SIMD
            if constexpr (Simd)
            {
                __m128i lower4 = _mm_set1_epi32(low), upper4 = _mm_setzero_si128();
                const auto minimum = [](__m128i a, __m128i b)
                {
                    const auto cmp = _mm_cmpgt_epi32(a, b);
                    return _mm_or_si128(_mm_and_si128(cmp, b), _mm_andnot_si128(cmp, a));
                };
                const auto maximum = [](__m128i a, __m128i b)
                {
                    const auto cmp = _mm_cmpgt_epi32(a, b);
                    return _mm_or_si128(_mm_and_si128(cmp, a), _mm_andnot_si128(cmp, b));
                };
                for (i32 i = 0; i < block; i += 4)
                {
                    const __m128i value =
                        _mm_loadu_si128(reinterpret_cast<const __m128i *>(indices + span.end + i));
                    lower4 = minimum(lower4, value);
                    upper4 = maximum(upper4, value);
#if defined(PUFFERUI_VULKAN_INTEGER_TESTS)
                    ++index_work.planning_vectors;
#endif
                }
                lower4 = minimum(lower4, _mm_shuffle_epi32(lower4, 0x4e));
                lower4 = minimum(lower4, _mm_shuffle_epi32(lower4, 0xb1));
                upper4 = maximum(upper4, _mm_shuffle_epi32(upper4, 0x4e));
                upper4 = maximum(upper4, _mm_shuffle_epi32(upper4, 0xb1));
                low = _mm_cvtsi128_si32(lower4);
                high = _mm_cvtsi128_si32(upper4);
            }
            else
#endif
                for (i32 i = 0; i < block; ++i)
                {
                    low = std::min(low, indices[span.end + i]);
                    high = std::max(high, indices[span.end + i]);
#if defined(PUFFERUI_VULKAN_INTEGER_TESTS)
                    ++index_work.planning_scalars;
#endif
                }
            low = std::min(span.low, low);
            high = std::max(span.high, high);
            if ((static_cast<VkDeviceSize>(high) - low + 1) * stride +
                    static_cast<VkDeviceSize>(span.end - start + block) * 4 <=
                capacity)
            {
                span.low = low;
                span.high = high;
                span.end += block;
                continue;
            }
        }
#if defined(PUFFERUI_VULKAN_INDEXED_TESTS)
        ++span_work.triangles;
#endif
        const i32 low =
            std::min(span.low, std::min(indices[span.end],
                                        std::min(indices[span.end + 1], indices[span.end + 2])));
        const i32 high =
            std::max(span.high, std::max(indices[span.end],
                                         std::max(indices[span.end + 1], indices[span.end + 2])));
        if ((static_cast<VkDeviceSize>(high) - low + 1) * stride +
                static_cast<VkDeviceSize>(span.end - start + 3) * 4 >
            capacity)
            break;
        span.low = low;
        span.high = high;
        span.end += 3;
    }
    return span;
}
inline i32 geometry_vertex_offset(i32 base, i32 count)
{
    // Borrowed devices need no fullDrawIndexUint32 feature. Keep raw indices
    // within Vulkan's guaranteed 24-bit range, and retain CPU rebasing for
    // negative/internal bases (including INT_MIN) or larger original ranges.
    return base > 0 && count > 0 && static_cast<i64>(base) + count - 1 <= 0x00ffffff ? -base : 0;
}
void device::paint_validated(texture *sample, const indexed_geometry &geometry,
                             const paint_style &style)
{
    const auto &[v, vc, idx, ic, index_base] = geometry;
    const auto &[blend, filter, kernel] = style;
    if (!commands()) return;
    const VkDeviceSize chunk =
        frame->geometry_chunk_bytes
            ? frame->geometry_chunk_bytes
            : std::min<VkDeviceSize>(
                  1024 * 1024, options.upload_budget_bytes /
                                   (options.upload_mode == vulkan_upload_mode::STAGED ? 2 : 1));
    const VkDeviceSize stride = vertex_stride();
    if (static_cast<VkDeviceSize>(vc) * stride + static_cast<VkDeviceSize>(ic) * 4 > chunk &&
        chunk >= 3 * (stride + sizeof(i32)))
    {
        // Preserve triangle order and each original vertex's float arithmetic.
        // Dense index spans share their contiguous source vertices. A sparse
        // triangle uses three stack copies rather than a huge unused interval.
        for (i32 start = 0; start < ic;)
        {
            const auto [end, low, high] = plan_geometry_span(idx, start, ic, stride, chunk);
            if (end == start)
            {
                const vertex triangle[] = {v[idx[start] - index_base],
                                           v[idx[start + 1] - index_base],
                                           v[idx[start + 2] - index_base]};
                const i32 local[] = {0, 1, 2};
                paint_validated(sample, {triangle, 3, local, 3, 0}, style);
                start += 3;
            }
            else
            {
                paint_validated(
                    sample, {v + low - index_base, high - low + 1, idx + start, end - start, low},
                    style);
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
    const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(vc) * stride;
    const VkDeviceSize index_bytes = static_cast<VkDeviceSize>(ic) * 4;
    VkDeviceSize offset = 0;
    page *p = upload(vertex_bytes + index_bytes, offset, true);
    if (!p) return;
    const i32 vertex_offset = geometry_vertex_offset(index_base, vc);
    // Integer byte attributes are expanded to the original exact float bits in
    // ui.vert. Positions/UVs and all four color bytes can be copied verbatim.
    if (options.compact_vertices)
        std::memcpy(p->mapped + offset, v, static_cast<usize>(vertex_bytes));
    else
    {
        auto *vertices = reinterpret_cast<legacy_vertex *>(p->mapped + offset);
        for (i32 i = 0; i < vc; ++i)
            vertices[i] = {v[i].x,
                           v[i].y,
                           v[i].u,
                           v[i].v,
                           BYTE_CHANNELS.values[v[i].c.r],
                           BYTE_CHANNELS.values[v[i].c.g],
                           BYTE_CHANNELS.values[v[i].c.b],
                           BYTE_CHANNELS.values[v[i].c.a]};
    }
    if (!index_base || vertex_offset)
        std::memcpy(p->mapped + offset + vertex_bytes, idx, static_cast<usize>(index_bytes));
    else
    {
        auto *out = reinterpret_cast<i32 *>(p->mapped + offset + vertex_bytes);
        for (i32 i = 0; i < ic; ++i) out[i] = idx[i] - index_base;
    }
    const VkBuffer geometry_buffer = p->staged ? p->device_buffer : p->buffer;
    // STAGED copies are consolidated in submit(), ahead of graphics.
    // Transition sampled images outside a render instance; LOAD preserves the
    // already recorded prefix when streaming updates or effects split a pass.
    if (sample->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || sample->ready)
        end_rendering();
    transition(sample, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!start_rendering(target)) return;
    const VkPipeline pipe = pipeline(target->format, blend, false, kernel);
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
    if (filter)
    {
        // Preserve every binary32 coefficient inside Vulkan's minimum 128-byte
        // push budget. Gaussian needs one signed axis step; Kawase reuses the
        // otherwise unused center-weight word for its second axis step.
        blur_constants constants;
        std::copy_n(filter->low, 2, constants.low);
        std::copy_n(filter->high, 2, constants.high);
        std::copy_n(filter->weights, GAUSSIAN_PAIR_COUNT + 1, constants.weights);
        std::copy_n(filter->offsets, GAUSSIAN_PAIR_COUNT, constants.offsets);
        constants.step = filter->step[0];
        if (kernel == static_cast<u32>(blur_kernel::GAUSSIAN))
        {
            if (constants.step == 0) constants.step = -filter->step[1];
        }
        else
            constants.weights[0] = filter->step[1];
        vkCmdPushConstants(frame->command, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 8,
                           sizeof(constants), &constants);
    }
    vkCmdDrawIndexed(frame->command, static_cast<u32>(ic), 1, 0, vertex_offset, 0);
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
    const auto kind = gaussian_compositing ? cache_event::GAUSSIAN_COMPOSITE : cache_event::DRAW;
    if (cache_active && cache_add(kind, sample, v, vc, idx, ic)) return;
    paint(sample, v, vc, idx, ic, true, 0, nullptr, gaussian_compositing ? 4 : 0);
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
    attachment.loadOp =
        discard_target == t ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent = {static_cast<u32>(t->width), static_cast<u32>(t->height)};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &attachment;
    vkCmdBeginRendering(frame->command, &ri);
    if (discard_target == t) discard_target = nullptr;
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
