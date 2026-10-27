// Exact final-alpha residency for the integrated native and compute painters.
// Fixed CPU keys and prepared per-slot request pages; no draw-path allocation.
// Publication belongs to a submitted recording token, never a discarded tape.
// Integer texels retain the original float bits, including small coverages.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
bool device::initialize_glyph_source()
{
    if (options.painter != vulkan_painter_mode::INTEGRATED || glyph_source_image) return true;
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(native.physical_device, VK_FORMAT_R32_UINT, &format);
    const auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if ((format.optimalTilingFeatures & required) != required)
        return fail("integrated painter requires sampled/storage R32_UINT");
    u32 capacity = 128;
    while (capacity && capacity * 65 > properties.limits.maxImageDimension2D) capacity /= 2;
    // A one-texel dummy keeps the descriptor contract valid on a tiny budget.
    // No key is admitted when the actual residency allocation is unavailable.
    if (options.vector_text && capacity >= 8)
        glyph_source_image = make_texture(
            {.width = 64,
             .height = static_cast<i32>(capacity * 65),
             .format = VK_FORMAT_R32_UINT,
             .extra_usage = VK_IMAGE_USAGE_STORAGE_BIT,
             .allocation_budget = options.cache_budget_bytes > stats.gpu_sdf_cache_image_bytes
                                      ? options.cache_budget_bytes - stats.gpu_sdf_cache_image_bytes
                                      : 0});
    if (glyph_source_image)
        glyph_source_capacity = capacity;
    else if (result == VK_SUCCESS)
        glyph_source_image = make_texture({.width = 1,
                                           .height = 1,
                                           .format = VK_FORMAT_R32_UINT,
                                           .extra_usage = VK_IMAGE_USAGE_STORAGE_BIT});
    if (!glyph_source_image) return false;
    glyph_source_image->intrinsic = true;
    stats.gpu_sdf_cache_image_bytes += glyph_source_image->allocation;
    transition(glyph_source_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const VkDescriptorSetLayout layouts[] = {descriptor_layout, sdf_layout};
    const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pc.setLayoutCount = 2;
    pc.pSetLayouts = layouts;
    pc.pushConstantRangeCount = 1;
    pc.pPushConstantRanges = &range;
    if (!check(vkCreatePipelineLayout(native.device, &pc, nullptr, &glyph_source_layout),
               "glyph source pipeline layout"))
        return false;
    glyph_source_fill = compute_pipeline(*this, glyph_source_layout, vulkan_glyph_source_fill,
                                         sizeof(vulkan_glyph_source_fill));
    glyph_source_publish = compute_pipeline(*this, glyph_source_layout, vulkan_glyph_source_publish,
                                            sizeof(vulkan_glyph_source_publish));
    return glyph_source_fill && glyph_source_publish;
}
bool device::prepare_glyph_source(frame_slot &slot)
{
    if (!glyph_source_capacity || slot.glyph_requests.buffer) return true;
    const u64 alignment =
        std::max<u64>(16, std::max(properties.limits.minStorageBufferOffsetAlignment,
                                   properties.limits.nonCoherentAtomSize));
    const u64 bytes = ((128 * 80 + alignment - 1) & ~(alignment - 1));
    // Optional residency cannot force a wait or resource creation in painting.
    if (stats.upload_allocated_bytes > options.upload_budget_bytes ||
        bytes + 65536 > options.upload_budget_bytes - stats.upload_allocated_bytes)
        return true;
    return allocate_buffer(slot.glyph_requests, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}
void device::cache_glyph_source(sdf_shape &shape, color tint)
{
    // Whole-image replay already retains every text pixel. Volatile programs
    // observed natively do not justify per-phase source-alpha admission/fills.
    // Fine-grained LOAD reuse and forced tile mode keep the exact glyph cache.
    if (primitive_commands.raster_only) return;
    if (!glyph_source_capacity || !slug_storage.device_buffer || !std::isfinite(shape.bounds.x) ||
        !std::isfinite(shape.bounds.y) || shape.bounds.w > 61 || shape.bounds.h > 61 ||
        shape.bounds.w <= 0 || shape.bounds.h <= 0)
        return;
    u64 hash = 1469598103934665603ull;
    for (usize offset = 0; offset < sizeof(shape); offset += sizeof(u64))
    {
        u64 word;
        std::memcpy(&word, reinterpret_cast<const u8 *>(&shape) + offset, sizeof(word));
        hash = (hash ^ word) * 1099511628211ull;
    }
    hash = (hash ^ tint.a) * 1099511628211ull;
    // Float positions and outline offsets frequently occupy only high bits.
    // Fold those bits before selecting the small set-associative table.
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdull;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ull;
    hash ^= hash >> 33;
    const u32 first = static_cast<u32>(hash) % (glyph_source_capacity / 8) * 8;
    glyph_source_entry *entry = nullptr;
    u32 selected = 0;
    for (u32 i = first; i < first + 8; ++i)
    {
        auto &candidate = glyph_sources[i];
        if (candidate.generation && candidate.hash == hash && candidate.alpha == tint.a &&
            std::memcmp(&candidate.shape, &shape, sizeof(shape)) == 0)
        {
            entry = &candidate;
            selected = i;
            ++glyph_source_hits;
            break;
        }
        if ((!candidate.generation || candidate.last_use <= known_completed) &&
            (!entry || candidate.last_use < entry->last_use))
        {
            entry = &candidate;
            selected = i;
        }
    }
    if (!entry) return;
    if (!entry->generation || entry->hash != hash || entry->alpha != tint.a ||
        std::memcmp(&entry->shape, &shape, sizeof(shape)) != 0)
    {
        if (glyph_source_generation == ~0ull) return;
        *entry = {};
        entry->shape = shape;
        entry->alpha = tint.a;
        entry->hash = hash;
        entry->generation = ++glyph_source_generation;
        ++glyph_source_misses;
    }
    entry->last_use = next_serial;
    shape.corners[0] = selected + 1;
    shape.corners[1] = static_cast<u32>(entry->generation);
    shape.corners[2] = static_cast<u32>(entry->generation >> 32);
    shape.corners[3] = tint.a;
}
bool device::fill_glyph_sources()
{
    if (!glyph_source_capacity || glyph_source_filling || !frame || !frame->recording ||
        !frame->glyph_requests.buffer || !slug_storage.used)
        return true;
    struct fill_request
    {
        sdf_shape shape;
        u32 slot, low, high, alpha;
    };
    static_assert(sizeof(fill_request) == 80);
    fill_request requests[128];
    u32 indices[128], count = 0;
    for (u32 i = 0; i < glyph_source_capacity; ++i)
    {
        const auto &entry = glyph_sources[i];
        if (!entry.generation || entry.ready || entry.last_use != next_serial ||
            entry.fill_token == frame->recording_token)
            continue;
        requests[count] = {entry.shape, i, static_cast<u32>(entry.generation),
                           static_cast<u32>(entry.generation >> 32), entry.alpha};
        indices[count++] = i;
    }
    if (!count) return true;
    const auto alignment =
        std::max<VkDeviceSize>(16, std::max(properties.limits.minStorageBufferOffsetAlignment,
                                            properties.limits.nonCoherentAtomSize));
    auto &storage = frame->glyph_requests;
    const auto offset = (storage.used + alignment - 1) & ~(alignment - 1);
    const auto bytes = static_cast<VkDeviceSize>(count) * sizeof(fill_request);
    if (offset > storage.capacity || bytes > storage.capacity - offset ||
        frame->sdf_descriptor_cursor + 3 >= frame->sdf_descriptors.size())
        return true;
    struct fill_guard
    {
        bool &busy;
        explicit fill_guard(bool &value) : busy(value) { busy = true; }
        ~fill_guard() { busy = false; }
    } guard(glyph_source_filling);
    // This also materializes any deferred prefix before mutating shared residency.
    if (!upload_slug_data()) return false;
    std::memcpy(storage.mapped + offset, requests, static_cast<usize>(bytes));
    storage.used = offset + bytes;
    if (!flush_page(storage)) return false;
    const VkDescriptorBufferInfo info[] = {{storage.buffer, offset, bytes},
                                           {slug_storage.device_buffer, 0, slug_storage.used},
                                           {storage.buffer, offset, 4},
                                           {storage.buffer, offset, 4}};
    const auto descriptors = sdf_descriptor(info);
    if (!descriptors) return false;
    end_rendering();
    transition(glyph_source_image, VK_IMAGE_LAYOUT_GENERAL);
    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, glyph_source_layout, 1,
                            1, &descriptors, 0, nullptr);
    const u32 push[] = {count, 0, 0, 0};
    vkCmdPushConstants(frame->command, glyph_source_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(push), push);
    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, glyph_source_fill);
    vkCmdDispatch(frame->command, 8, 8, count);
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(frame->command, &dep);
    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, glyph_source_publish);
    vkCmdDispatch(frame->command, (count + 63) / 64, 1, 1);
    transition(glyph_source_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    glyph_source_image->last_use = next_serial;
    for (u32 i = 0; i < count; ++i) glyph_sources[indices[i]].fill_token = frame->recording_token;
    glyph_source_fills += count;
    stats.upload_bytes += bytes;
    return true;
}
void device::submitted_glyph_sources(u64 token)
{
    for (u32 i = 0; i < glyph_source_capacity; ++i)
        if (glyph_sources[i].fill_token == token) glyph_sources[i].ready = true;
}
void device::release_glyph_source()
{
    if (glyph_source_fill) vkDestroyPipeline(native.device, glyph_source_fill, nullptr);
    if (glyph_source_publish) vkDestroyPipeline(native.device, glyph_source_publish, nullptr);
    if (glyph_source_layout) vkDestroyPipelineLayout(native.device, glyph_source_layout, nullptr);
    if (glyph_source_image)
    {
        stats.gpu_sdf_cache_image_bytes -= glyph_source_image->allocation;
        destroy_texture(glyph_source_image);
    }
    glyph_source_image = nullptr;
    glyph_source_capacity = 0;
}
} // namespace vk_detail
#endif
