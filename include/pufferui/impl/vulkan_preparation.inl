// Prepared native painter resources. Included by the single implementation TU.
// Device kernels are immutable; slots own descriptor sets and bounded arenas.
// Previous programs stay in the other arena bank until this slot completes.
// Resource creation happens at surface preparation or a completed frame boundary.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
sdf_slot_resources::~sdf_slot_resources()
{
    if (!dev) return;
    delete[] job_storage;
    if (descriptors) dev->vkDestroyDescriptorPool(dev->native.device, descriptors, nullptr);
    gpu_bin_job owner;
    owner.dev = dev;
    for (auto &bank : banks) owner.free_storage(bank);
    for (auto &bank : inputs) owner.free_storage(bank);
    for (auto &bank : readable) owner.free_storage(bank);
    owner.free_storage(metadata);
}

bool device::prepare_sdf_descriptors(frame_slot &slot)
{
    if (!slot.sdf_descriptors.empty()) return true;
    const u32 count = options.painter == vulkan_painter_mode::INTEGRATED ? 128 : 64;
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count * 4},
                                          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count},
                                          {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, count}};
    VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pc.maxSets = count;
    pc.poolSizeCount = options.painter == vulkan_painter_mode::INTEGRATED ? 3 : 1;
    pc.pPoolSizes = sizes;
    VkDescriptorPool pool{};
    if (!check(vkCreateDescriptorPool(native.device, &pc, nullptr, &pool),
               "vkCreateDescriptorPool(SDF prepared)"))
        return false;
    slot.sdf_pools.push_back(pool);
    VkDescriptorSetLayout layouts[128];
    std::fill(std::begin(layouts), std::end(layouts), sdf_layout);
    slot.sdf_descriptors.resize(count);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool;
    ai.descriptorSetCount = count;
    ai.pSetLayouts = layouts;
    if (!check(vkAllocateDescriptorSets(native.device, &ai, slot.sdf_descriptors.data()),
               "vkAllocateDescriptorSets(SDF prepared)"))
    {
        slot.sdf_descriptors.clear();
        return false;
    }
    slot.sdf_sets = count;
    stats.descriptor_sets += count;
    return true;
}

bool device::prepare_sdf_surface(surface &s)
{
    if (!prepare_alpha(s)) return false;
    if (!prepare_kawase(s)) return false;
    if (!paint_detail::sdf_painter(options.painter) || !s.scene) return true;
    const u32 width = static_cast<u32>(s.scene->width), height = static_cast<u32>(s.scene->height);
    const u64 pixels = static_cast<u64>(width) * height;
    const u64 alignment = std::max<u64>(4, properties.limits.minStorageBufferOffsetAlignment);
    const auto aligned = [alignment](u64 n) { return (n + alignment - 1) & ~(alignment - 1); };
    const u64 history_words = options.painter == vulkan_painter_mode::INTEGRATED ? 10u : 7u;
    for (auto &slot : s.slots)
    {
        if (!prepare_sdf_descriptors(slot)) return false;
        if (!prepare_glyph_source(slot)) return false;
        // Reserve geometry before recording UI draws. Explicit tiny budgets
        // retain the existing submission/wait path instead of hiding work.
        if (slot.pages.empty())
        {
            const u64 available = options.upload_budget_bytes > stats.upload_allocated_bytes
                                      ? options.upload_budget_bytes - stats.upload_allocated_bytes
                                      : 0;
            const u64 copies = options.upload_mode == vulkan_upload_mode::STAGED ? 2 : 1;
            const u64 bytes =
                std::min((std::max<u64>(1024 * 1024, pixels * 10) + 65535) & ~65535ull,
                         available / (s.slots.size() * copies));
            if (bytes >= 4096)
            {
                page fresh;
                if (!allocate_buffer(
                        fresh, bytes,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT))
                    return false;
                fresh.staged = options.upload_mode == vulkan_upload_mode::STAGED;
                if (fresh.staged && !allocate_device_buffer(fresh))
                {
                    free_page(fresh);
                    return false;
                }
                slot.pages.push_back(fresh);
            }
        }
    }
    if (!paint_detail::compute_painter(options.painter)) return true;
    // Adaptive routing may use raster for the entire scene. Do not reserve
    // history images and arenas until an actual eligible program requests them.
    const bool observed_compute =
        options.painter == vulkan_painter_mode::INTEGRATED && options.whole_program_cache;
    if ((options.adaptive_compute || observed_compute) && !s.sdf_compute_requested) return true;
    if (!initialize_sdf_compute_kernels()) return false;
    if (options.painter == vulkan_painter_mode::INTEGRATED &&
        (!sdf_functions || !sdf_functions->valid))
        return true;
    if (options.tile_cache && !initialize_sdf_cache_kernels()) return false;
    // Present-mode changes rebuild the swapchain but do not change the prepared
    // painter's extent. Keep its resident buffers and histories in that case.
    bool prepared = true;
    for (auto &slot : s.slots)
        prepared = prepared && slot.sdf_resources && slot.sdf_resources->width == width &&
                   slot.sdf_resources->height == height &&
                   slot.sdf_resources->cache_requested == options.tile_cache &&
                   slot.sdf_resources->budget_stamp == options.cache_budget_bytes;
    if (prepared) return true;
    for (auto &slot : s.slots) destroy_sdf_jobs(slot);
    const u64 available =
        options.gpu_command_budget_bytes > stats.gpu_metadata_allocated_bytes
            ? options.gpu_command_budget_bytes - stats.gpu_metadata_allocated_bytes
            : 0;
    if (available < s.slots.size() * 8192) return true; // exact raster fallback
    constexpr u32 jobs = sdf_slot_resources::MAX_JOBS;
    for (auto &slot : s.slots)
    {
        auto *resources = new sdf_slot_resources();
        slot.sdf_resources = resources;
        resources->dev = this;
        resources->width = width;
        resources->height = height;
        resources->budget_stamp = options.cache_budget_bytes;
        resources->cache_requested = options.tile_cache;
        resources->jobs.reserve(jobs);
        slot.sdf_jobs.reserve(jobs);
        resources->job_storage = new sdf_gpu_job[jobs]();
        for (u32 i = 0; i < jobs; ++i)
        {
            auto *job = &resources->job_storage[i];
            job->bin.dev = this;
            job->bin.descriptor_layout = bin_descriptor_layout;
            job->bin.layout = bin_pipeline_layout;
            std::copy(std::begin(bin_pipelines), std::end(bin_pipelines), job->bin.pipelines);
            job->image_layout = sdf_image_layout;
            job->layout = sdf_compute_layout;
            job->painter = sdf_pipelines[0];
            job->args = sdf_pipelines[1];
            job->compare = sdf_pipelines[2];
            job->compact = sdf_pipelines[3];
            job->restore = sdf_pipelines[4];
            job->commit = sdf_pipelines[5];
            job->native_snapshot = native_cache_pipelines[0];
            job->native_capture = native_cache_pipelines[1];
            resources->jobs.push_back(job);
            ++stats.sdf_job_creations;
        }
    }
    // Decide image eligibility once, before reserving history storage. A failed
    // budget probe is remembered by this prepared surface, never retried in draw.
    bool image_budget_full = !options.tile_cache || !options.cache_budget_bytes;
    // A cache entry owns a full-size image even when it restores only a small
    // region. Bound the prepared image working set independently of the maximum
    // budget; uncached epochs retain the same complete painter output.
    constexpr u32 cache_jobs = 2;
    for (u32 i = 0; i < cache_jobs && !image_budget_full; ++i)
        for (auto &slot : s.slots)
        {
            const u64 used = stats.gpu_sdf_cache_image_bytes;
            if (used >= options.cache_budget_bytes)
            {
                image_budget_full = true;
                break;
            }
            ++stats.gpu_sdf_cache_image_attempts;
            auto *saved = make_texture({.width = static_cast<i32>(width),
                                        .height = static_cast<i32>(height),
                                        .render_target = true,
                                        .extra_usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                        .allocation_budget = options.cache_budget_bytes - used});
            if (!saved)
            {
                if (result != VK_SUCCESS) return false;
                image_budget_full = true;
                break;
            }
            slot.sdf_resources->jobs[i]->saved = saved;
            saved->intrinsic = true;
            transition(saved, VK_IMAGE_LAYOUT_GENERAL);
            stats.gpu_sdf_cache_image_bytes += saved->allocation;
            if (sdf_functions)
            {
                const u64 remaining = options.cache_budget_bytes - stats.gpu_sdf_cache_image_bytes;
                auto *before = make_texture({.width = static_cast<i32>(width),
                                             .height = static_cast<i32>(height),
                                             .render_target = true,
                                             .extra_usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                             .allocation_budget = remaining});
                if (!before)
                {
                    slot.sdf_resources->jobs[i]->saved = nullptr;
                    stats.gpu_sdf_cache_image_bytes -= saved->allocation;
                    destroy_texture(saved);
                    if (result != VK_SUCCESS) return false;
                    image_budget_full = true;
                    break;
                }
                slot.sdf_resources->jobs[i]->native_before = before;
                before->intrinsic = true;
                transition(before, VK_IMAGE_LAYOUT_GENERAL);
                stats.gpu_sdf_cache_image_bytes += before->allocation;
            }
        }
    const u64 tiles = ((static_cast<u64>(width) + 31) / 32) * ((height + 31) / 32);
    u64 meta_total = 0, bank_count = 0;
    for (auto &slot : s.slots)
    {
        u64 bytes = 0;
        bool history = false;
        for (auto *job : slot.sdf_resources->jobs)
        {
            bytes += aligned(options.tile_cache ? (tiles * 2 + history_words) * 4 : 4);
            history = history || job->saved;
        }
        meta_total += bytes;
        bank_count += history ? 2 : 1;
    }
    const u64 budget_left = options.gpu_command_budget_bytes - stats.gpu_metadata_allocated_bytes;
    const u64 bank_limit = budget_left > meta_total ? (budget_left - meta_total) / bank_count : 0;
    // Separate GPU work, CPU staging and the small readable route/telemetry
    // region. Only staging is duplicated; GPU work is never mapped.
    const u64 wanted = (std::max<u64>(1024 * 1024, pixels * 19 / 2) + 65535) & ~65535ull;
    // Leave alignment/implementation allocation-requirement slack per allocation.
    const u64 bank_bytes = std::min(wanted, bank_limit > 12288 ? bank_limit - 12288 : 0) & ~4095ull;
    const u64 input_bytes = (bank_bytes * 9 / 20) & ~4095ull;
    const u64 read_bytes = (bank_bytes / 10) & ~4095ull;
    const u64 work_bytes = bank_bytes - input_bytes - read_bytes;
    for (auto &slot : s.slots)
    {
        auto &resources = *slot.sdf_resources;
        gpu_bin_job allocator;
        allocator.dev = this;
        u64 meta_bytes = 0;
        bool history = false;
        for (auto *job : resources.jobs)
        {
            meta_bytes += aligned(options.tile_cache ? (tiles * 2 + history_words) * 4 : 4);
            history = history || job->saved;
        }
        bool ready = bank_bytes >= 4096 && allocator.storage(resources.metadata, meta_bytes);
        for (u32 bank = 0; ready && bank < (history ? 2u : 1u); ++bank)
        {
            ready = input_bytes >= 4096 && read_bytes >= 4096 &&
                    allocator.storage(resources.banks[bank], work_bytes, storage_role::DEVICE) &&
                    allocator.storage(resources.readable[bank], read_bytes, storage_role::READABLE);
            // Previous commands/shapes are retained in GPU storage. Staging is
            // overwritten only after this slot completes, so it needs one bank.
            if (ready && !bank) ready = allocator.storage(resources.inputs[0], input_bytes);
        }
        if (!ready)
        {
            if (result != VK_SUCCESS) return false;
            destroy_sdf_jobs(slot);
            continue;
        }
        std::memset(resources.metadata.mapped, 0, static_cast<usize>(meta_bytes));
        resources.metadata.used = meta_bytes;
        if (!flush_page(resources.metadata)) return false;
        u64 offset = 0;
        for (auto *job : resources.jobs)
        {
            auto &meta = job->history[5];
            meta = resources.metadata;
            meta.offset = offset;
            meta.mapped += offset;
            meta.capacity = aligned(options.tile_cache ? (tiles * 2 + history_words) * 4 : 4);
            meta.allocation = 0;
            meta.borrowed = true;
            offset += meta.capacity;
        }
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, jobs * 12},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, jobs * (sdf_functions ? 3u : 2u)},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, jobs * (sdf_functions ? 2u : 1u)}};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets = jobs * 2;
        pc.poolSizeCount = options.painter == vulkan_painter_mode::INTEGRATED ? 3 : 2;
        pc.pPoolSizes = sizes;
        if (!check(vkCreateDescriptorPool(native.device, &pc, nullptr, &resources.descriptors),
                   "prepared compute descriptor pool"))
            return false;
        VkDescriptorSetLayout layouts[jobs * 2];
        VkDescriptorSet sets[jobs * 2];
        for (u32 i = 0; i < jobs; ++i)
        {
            layouts[i * 2] = bin_descriptor_layout;
            layouts[i * 2 + 1] = sdf_image_layout;
        }
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = resources.descriptors;
        ai.descriptorSetCount = jobs * 2;
        ai.pSetLayouts = layouts;
        if (!check(vkAllocateDescriptorSets(native.device, &ai, sets),
                   "prepared compute descriptors"))
            return false;
        for (u32 i = 0; i < jobs; ++i)
        {
            resources.jobs[i]->bin.descriptor = sets[i * 2];
            resources.jobs[i]->image_set = sets[i * 2 + 1];
        }
        stats.descriptor_sets += jobs * 2;
    }
    return true;
}

// Called only after this slot completes. Move its bounded image leases rather
// than creating new images when a later program is more useful than the prefix.
inline void rebalance_sdf_history(sdf_slot_resources &resources)
{
    const usize count = resources.jobs.size();
    if (count > sdf_slot_resources::MAX_JOBS) return;
    bool selected[sdf_slot_resources::MAX_JOBS]{};
    usize images = 0;
    for (auto *job : resources.jobs) images += job->saved != nullptr;
    for (usize i = 0; i < images; ++i)
    {
        usize best = count;
        for (usize j = 0; j < count; ++j)
            if (!selected[j] &&
                (best == count ||
                 resources.jobs[j]->eligible_pixels > resources.jobs[best]->eligible_pixels ||
                 (resources.jobs[j]->eligible_pixels == resources.jobs[best]->eligible_pixels &&
                  resources.jobs[j]->saved && !resources.jobs[best]->saved)))
                best = j;
        if (best < count) selected[best] = true;
    }
    for (usize i = 0; i < count; ++i)
        if (!selected[i] && resources.jobs[i]->saved)
            for (usize j = 0; j < count; ++j)
                if (selected[j] && !resources.jobs[j]->saved)
                {
                    std::swap(resources.jobs[i]->saved, resources.jobs[j]->saved);
                    resources.jobs[i]->published_bank = resources.jobs[j]->published_bank = -1;
                    break;
                }
}

bool device::begin_sdf_slot(frame_slot &slot)
{
    slot.sdf_descriptor_cursor = 0;
    slot.kawase_cursor = 0;
    if (auto *resources = slot.sdf_resources)
    {
        // Readback/upload-only submissions must not age or overwrite painter
        // history. Advance banks only when another actual painter epoch starts.
        resources->pending = true;
        for (auto *job : resources->jobs)
            if (std::find(slot.sdf_jobs.begin(), slot.sdf_jobs.end(), job) == slot.sdf_jobs.end())
                job->eligible_pixels = 0;
        rebalance_sdf_history(*resources);
    }
    return true;
}

bool device::prepare_geometry(const geometry_capacity &capacity)
{
    if (!paint_detail::sdf_painter(options.painter) || paint_depth) return false;
    const usize shapes = std::min<usize>(capacity.vertices / 4, 65536);
    if (sdf_shapes.capacity() < shapes) sdf_shapes.reserve(shapes);
    if (sdf_spans.capacity() < shapes) sdf_spans.reserve(shapes);
    // Native slot uploads and compute storage are prepared from the target's
    // pixel extent. This hint does not promise an unbounded geometry reservation.
    return frame && !frame->pages.empty();
}

bool device::upload_slug_data()
{
    if (!slug_storage.device_buffer || slug_storage.used <= slug_committed) return true;
    retained_abort(); // append-only glyph uploads must actually execute
    end_rendering();
    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = slug_storage.device_buffer;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(frame->command, &dependency);
    const VkBufferCopy copy{slug_committed, slug_committed, slug_storage.used - slug_committed};
    vkCmdCopyBuffer(frame->command, slug_storage.buffer, slug_storage.device_buffer, 1, &copy);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    vkCmdPipelineBarrier2(frame->command, &dependency);
    stats.upload_bytes += copy.size;
    slug_committed = slug_storage.used;
    return true;
}
} // namespace vk_detail
#endif
