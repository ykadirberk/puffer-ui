// Actual compute SDF command interpreter, with disjoint per-pixel raster replay.
// Only finite RGBA8, fully-covered suffixes are interpreted by compute.
// Every other pixel uses the complete original ordered graphics program.
// Frame slots own jobs until their submission has completed.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
struct sdf_cpu_phase
{
    f64 &total;
    f64 start = wall_ms();
    ~sdf_cpu_phase() { total += wall_ms() - start; }
};
struct sdf_gpu_job
{
    gpu_bin_job bin;
    page mask;
    page history[6], state;
    texture *saved = nullptr;
    bool cache_enabled = false, reset_history = true;
    VkDescriptorSetLayout image_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet image_set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline painter = VK_NULL_HANDLE, args = VK_NULL_HANDLE;
    VkPipeline compare = VK_NULL_HANDLE, compact = VK_NULL_HANDLE, restore = VK_NULL_HANDLE,
               commit = VK_NULL_HANDLE;
    u32 parameters[20]{};
    u64 harvested = 0;
    ~sdf_gpu_job()
    {
        auto *d = bin.dev;
        if (!d) return;
        if (bin.serial && bin.serial < d->next_serial) d->wait(bin.serial);
        if (image_set) --d->stats.descriptor_sets;
        if (painter) d->vkDestroyPipeline(d->native.device, painter, nullptr);
        if (args) d->vkDestroyPipeline(d->native.device, args, nullptr);
        for (auto pipeline : {compare, compact, restore, commit})
            if (pipeline) d->vkDestroyPipeline(d->native.device, pipeline, nullptr);
        if (layout) d->vkDestroyPipelineLayout(d->native.device, layout, nullptr);
        if (pool) d->vkDestroyDescriptorPool(d->native.device, pool, nullptr);
        if (image_layout) d->vkDestroyDescriptorSetLayout(d->native.device, image_layout, nullptr);
        bin.free_storage(mask);
        bin.free_storage(state);
        for (auto &p : history) bin.free_storage(p);
        if (saved)
        {
            d->stats.gpu_sdf_cache_image_bytes -= saved->allocation;
            d->destroy_texture(saved);
        }
    }
    bool initialize(device &d, u32 capacity)
    {
        if (!bin.initialize(d, d.sdf_tape, static_cast<u32>(d.target->width),
                            static_cast<u32>(d.target->height), 32, 256, capacity))
            return false;
        const u64 pixels = static_cast<u64>(d.target->width) * d.target->height;
        if (!bin.storage(mask, ((pixels + 31) / 32) * 4)) return false;
        if (!bin.resize_storage(4, 64) ||
            !bin.storage(state, static_cast<u64>(bin.parameters[11]) * 16))
            return false;
        for (auto &p : history)
            if (!bin.storage(p, 4)) return false;
        VkDescriptorSetLayoutBinding bindings[9]{};
        for (u32 i = 0; i < 9; ++i)
            bindings[i] = {i,
                           (i == 0 || i == 6) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                              : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lc.bindingCount = 9;
        lc.pBindings = bindings;
        if (!d.check(d.vkCreateDescriptorSetLayout(d.native.device, &lc, nullptr, &image_layout),
                     "SDF compute image layout"))
            return false;
        const VkDescriptorSetLayout layouts[] = {bin.descriptor_layout, d.sdf_layout, image_layout};
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters)};
        VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pc.setLayoutCount = 3;
        pc.pSetLayouts = layouts;
        pc.pushConstantRangeCount = 1;
        pc.pPushConstantRanges = &range;
        if (!d.check(d.vkCreatePipelineLayout(d.native.device, &pc, nullptr, &layout),
                     "SDF compute layout"))
            return false;
        painter = compute_pipeline(d, layout, vulkan_sdf_paint, sizeof(vulkan_sdf_paint));
        args = compute_pipeline(d, bin.layout, vulkan_paint_args, sizeof(vulkan_paint_args));
        compare = compute_pipeline(d, layout, vulkan_sdf_compare, sizeof(vulkan_sdf_compare));
        compact = compute_pipeline(d, layout, vulkan_sdf_compact, sizeof(vulkan_sdf_compact));
        restore = compute_pipeline(d, layout, vulkan_sdf_restore, sizeof(vulkan_sdf_restore));
        commit = compute_pipeline(d, layout, vulkan_sdf_commit, sizeof(vulkan_sdf_commit));
        if (!painter || !args || !compare || !compact || !restore || !commit) return false;
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7}};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 2;
        pool_info.pPoolSizes = sizes;
        if (!d.check(d.vkCreateDescriptorPool(d.native.device, &pool_info, nullptr, &pool),
                     "SDF compute image pool"))
            return false;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &image_layout;
        if (!d.check(d.vkAllocateDescriptorSets(d.native.device, &ai, &image_set),
                     "SDF compute image set"))
            return false;
        ++d.stats.descriptor_sets;
        return true;
    }
    bool configure_history(device &d)
    {
        cache_enabled = false;
        if (!d.options.tile_cache || !d.options.cache_budget_bytes) return true;
        const u64 sizes[] = {bin.buffers[0].capacity,
                             bin.buffers[2].capacity,
                             bin.buffers[3].capacity,
                             (d.sdf_shapes.empty() ? 1 : d.sdf_shapes.size()) * sizeof(sdf_shape),
                             mask.capacity,
                             (static_cast<u64>(bin.parameters[11]) * 2 + 7) * 4};
        for (u32 i = 0; i < 6; ++i)
            if (history[i].capacity < sizes[i])
            {
                page fresh;
                if (!bin.storage(fresh, sizes[i]))
                {
                    bin.free_storage(fresh);
                    return true;
                }
                bin.free_storage(history[i]);
                history[i] = fresh;
                reset_history = true;
            }
        if (!saved)
        {
            const u64 used = d.stats.gpu_sdf_cache_image_bytes;
            if (used >= d.options.cache_budget_bytes) return true;
            saved = d.make_texture({.width = d.target->width,
                                    .height = d.target->height,
                                    .render_target = true,
                                    .extra_usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                    .allocation_budget = d.options.cache_budget_bytes - used});
            if (!saved) return d.result == VK_SUCCESS;
            d.stats.gpu_sdf_cache_image_bytes += saved->allocation;
            reset_history = true;
        }
        cache_enabled = true;
        return true;
    }
};
bool device::initialize_sdf_compute()
{
    if (options.painter != vulkan_painter_mode::COMPUTE_SDF) return true;
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(native.physical_device, VK_FORMAT_R8G8B8A8_UNORM, &format);
    if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
        properties.limits.maxPerStageDescriptorStorageBuffers < 16)
        return fail("compute SDF needs RGBA8 storage and sixteen storage-buffer bindings");
    sdf_blend = new rgba8_blend_lut();
    const bool initialized = sdf_blend->initialize(*this);
    stats.gpu_sdf_calibration_ms = sdf_blend->preparation_ms;
    return initialized;
}
void device::release_sdf_compute()
{
    delete sdf_blend;
    sdf_blend = nullptr;
}
void device::destroy_sdf_jobs(frame_slot &slot)
{
    for (auto *job : slot.sdf_jobs) delete job;
    slot.sdf_jobs.clear();
}
sdf_gpu_job *device::prepare_sdf_compute()
{
    if (options.painter != vulkan_painter_mode::COMPUTE_SDF || !sdf_tape.valid ||
        sdf_tape.commands.empty() || !sdf_blend || !sdf_blend->valid || !target ||
        target->format != VK_FORMAT_R8G8B8A8_UNORM || !(target->usage & VK_IMAGE_USAGE_STORAGE_BIT))
        return nullptr;
    sdf_cpu_phase phase{stats.sdf_prepare_ms};
    for (const auto &command : sdf_tape.commands)
        if (command.operation > 2 ||
            (command.operation == 2 && command.resource >= sdf_shapes.size()))
            return nullptr; // malformed metadata always selects complete raster replay
    if (options.adaptive_compute)
    {
        bool eligible = options.tile_cache && sdf_tape.commands.size() >= 2048;
        for (const auto &command : sdf_tape.commands)
            if (command.operation == 0 || (command.flags >> 24) != 255 ||
                (command.operation == 2 && sdf_shapes[command.resource].reserved[0] == 1))
            {
                eligible = false;
                break;
            }
        if (sdf_adaptive_cooldown || !eligible)
        {
            if (sdf_adaptive_cooldown) --sdf_adaptive_cooldown;
            ++stats.gpu_sdf_bypasses;
            return nullptr;
        }
    }
    u64 capacity = 0;
    for (const auto &c : sdf_tape.commands)
    {
        const double l = std::floor(
            std::clamp(static_cast<double>(c.x0), 0.0, static_cast<double>(target->width)) / 32);
        const double r = std::ceil(
            std::clamp(static_cast<double>(c.x1), 0.0, static_cast<double>(target->width)) / 32);
        const double t = std::floor(
            std::clamp(static_cast<double>(c.y0), 0.0, static_cast<double>(target->height)) / 32);
        const double b = std::ceil(
            std::clamp(static_cast<double>(c.y1), 0.0, static_cast<double>(target->height)) / 32);
        capacity += static_cast<u64>((r - l) * (b - t));
        if (capacity > 0xffffffffu || capacity > options.gpu_command_budget_bytes / 4)
            return nullptr;
    }
    const usize index = frame->sdf_job_cursor++;
    if (index == frame->sdf_jobs.size()) frame->sdf_jobs.push_back(nullptr);
    auto *&job = frame->sdf_jobs[index];
    if (job && (job->bin.parameters[0] != static_cast<u32>(target->width) ||
                job->bin.parameters[1] != static_cast<u32>(target->height)))
    {
        delete job;
        job = nullptr;
    }
    if (!job)
    {
        job = new sdf_gpu_job();
        if (!job->initialize(*this, static_cast<u32>(capacity)))
        {
            delete job;
            job = nullptr;
            return nullptr;
        }
        ++stats.sdf_job_creations;
    }
    else if (!job->bin.prepare_again(sdf_tape, static_cast<u32>(capacity)))
        return nullptr;
    if (!job->configure_history(*this)) return nullptr;
    return job;
}
VkDescriptorBufferInfo device::sdf_route(sdf_gpu_job *job)
{
    return {job->mask.buffer, 0, job->mask.capacity};
}
VkDescriptorBufferInfo device::sdf_lut()
{
    const auto &p = sdf_blend->storage.buffers[1];
    return {p.buffer, 0, p.capacity};
}
bool device::execute_sdf_compute(sdf_gpu_job *job, VkDescriptorSet shapes,
                                 const VkDescriptorBufferInfo &shape_data)
{
    sdf_cpu_phase phase{stats.sdf_record_ms};
    auto &bin = job->bin;
    if (!bin.record()) return false;
    if (sdf_cache_faults & 2u)
    {
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdFillBuffer(frame->command, bin.buffers[4].buffer, 12, 4, 1);
    }
    vkCmdFillBuffer(frame->command, job->mask.buffer, 0, VK_WHOLE_SIZE, 0);
    bin.barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    std::memcpy(job->parameters, bin.parameters, sizeof(bin.parameters));
    job->parameters[7] = properties.limits.maxComputeWorkGroupCount[0];
    float clip[4] = {0, 0, static_cast<float>(target->width), static_cast<float>(target->height)};
    if (has_clip)
    {
        clip[0] = max2(0.0f, std::trunc(this->clip.x));
        clip[1] = max2(0.0f, std::trunc(this->clip.y));
        clip[2] = min2(clip[2], std::trunc(this->clip.x) + std::trunc(this->clip.w));
        clip[3] = min2(clip[3], std::trunc(this->clip.y) + std::trunc(this->clip.h));
    }
    std::memcpy(job->parameters + 12, clip, sizeof(clip));
    job->parameters[16] = job->cache_enabled ? 1u : 0u;
    job->parameters[17] = static_cast<u32>(target->generation);
    job->parameters[18] = static_cast<u32>(target->generation >> 32);
    job->parameters[19] = sdf_cache_faults;
    transition(target, VK_IMAGE_LAYOUT_GENERAL);
    if (job->cache_enabled) transition(job->saved, VK_IMAGE_LAYOUT_GENERAL);
    VkDescriptorImageInfo images[2]{};
    images[0].imageView = target->view;
    images[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images[1].imageView = job->cache_enabled ? job->saved->view : target->view;
    images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorBufferInfo buffers[7]{};
    for (u32 i = 0; i < 5; ++i) buffers[i] = {job->history[i].buffer, 0, job->history[i].capacity};
    buffers[5] = {job->state.buffer, 0, job->state.capacity};
    buffers[6] = {job->history[5].buffer, 0, job->history[5].capacity};
    VkWriteDescriptorSet writes[9]{};
    u32 image_index = 0, buffer_index = 0;
    for (u32 i = 0; i < 9; ++i)
    {
        auto &write = writes[i];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = job->image_set;
        write.dstBinding = i;
        write.descriptorCount = 1;
        if (i == 0 || i == 6)
        {
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            write.pImageInfo = &images[image_index++];
        }
        else
        {
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &buffers[buffer_index++];
        }
    }
    vkUpdateDescriptorSets(native.device, 9, writes, 0, nullptr);
    const VkDescriptorSet sets[] = {bin.descriptor, shapes, job->image_set};
    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->layout, 0, 3, sets,
                            0, nullptr);
    vkCmdPushConstants(frame->command, job->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(job->parameters), job->parameters);
    const u32 tiles = bin.parameters[11], max_x = properties.limits.maxComputeWorkGroupCount[0];
    if (job->cache_enabled)
    {
        if (job->reset_history)
        {
            vkCmdFillBuffer(frame->command, job->history[5].buffer, 0, VK_WHOLE_SIZE, 0);
            bin.barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
            job->reset_history = false;
        }
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->compare);
        vkCmdDispatch(frame->command, (tiles + 63) / 64, 1, 1);
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->compact);
        vkCmdDispatch(frame->command, 1, 1, 1);
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->restore);
        vkCmdDispatch(frame->command, tiles < max_x ? tiles : max_x,
                      tiles / max_x + (tiles % max_x != 0), 1);
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    }
    else
    {
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, bin.layout, 0, 1,
                                &bin.descriptor, 0, nullptr);
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->args);
        vkCmdPushConstants(frame->command, bin.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(bin.parameters), job->parameters);
        vkCmdDispatch(frame->command, 1, 1, 1);
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->layout, 0, 3,
                                sets, 0, nullptr);
        vkCmdPushConstants(frame->command, job->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(job->parameters), job->parameters);
    }
    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->painter);
    vkCmdDispatchIndirect(frame->command, bin.buffers[4].buffer, 0);
    if (job->cache_enabled)
    {
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkBuffer src[] = {bin.buffers[0].buffer, bin.buffers[2].buffer, bin.buffers[3].buffer,
                                shape_data.buffer, job->mask.buffer};
        const VkDeviceSize sizes[] = {bin.buffers[0].capacity, bin.buffers[2].capacity,
                                      bin.buffers[3].capacity, shape_data.range,
                                      job->mask.capacity};
        for (u32 i = 0; i < 5; ++i)
        {
            const VkBufferCopy copy{i == 3 ? shape_data.offset : 0, 0, sizes[i]};
            vkCmdCopyBuffer(frame->command, src[i], job->history[i].buffer, 1, &copy);
        }
        const VkBufferCopy hash_copy{static_cast<VkDeviceSize>(tiles) * 4, 4,
                                     static_cast<VkDeviceSize>(tiles) * 8};
        vkCmdCopyBuffer(frame->command, job->state.buffer, job->history[5].buffer, 1, &hash_copy);
        transition(target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transition(job->saved, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstSubresource = copy.srcSubresource;
        copy.extent = {static_cast<u32>(target->width), static_cast<u32>(target->height), 1};
        vkCmdCopyImage(frame->command, target->image, target->layout, job->saved->image,
                       job->saved->layout, 1, &copy);
        job->saved->last_use = next_serial;
        bin.barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        // Restore the descriptor's advertised layouts before the commit shader;
        // it statically shares the complete set layout with the painter.
        transition(target, VK_IMAGE_LAYOUT_GENERAL);
        transition(job->saved, VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->commit);
        vkCmdDispatch(frame->command, 1, 1, 1);
    }
    bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    ++stats.gpu_sdf_batches;
    target->last_use = sdf_blend->storage.serial = next_serial;
    return true;
}
void device::harvest_sdf_stats()
{
    if (options.painter != vulkan_painter_mode::COMPUTE_SDF) return;
    const u64 done = completed();
    const auto harvest = [&](frame_slot &slot)
    {
        for (auto *job : slot.sdf_jobs)
        {
            if (!job || !job->bin.serial || job->bin.serial > done ||
                job->harvested == job->bin.serial)
                continue;
            const auto &control = job->bin.buffers[4];
            if (!control.coherent)
            {
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = control.memory;
                range.size = VK_WHOLE_SIZE;
                if (!check(vkInvalidateMappedMemoryRanges(native.device, 1, &range),
                           "SDF telemetry invalidate"))
                    return;
            }
            stats.gpu_sdf_pixels += reinterpret_cast<const u32 *>(control.mapped)[6];
            if (options.adaptive_compute)
            {
                const auto *words = reinterpret_cast<const u32 *>(control.mapped);
                const u64 covered =
                    static_cast<u64>(words[6]) + (job->cache_enabled ? words[8] : 0);
                const u64 area = static_cast<u64>(job->bin.parameters[0]) * job->bin.parameters[1];
                if (covered < area - area / 4) sdf_adaptive_cooldown = 512;
            }
            stats.gpu_sdf_cache_hits += reinterpret_cast<const u32 *>(control.mapped)[7];
            if (job->cache_enabled)
            {
                stats.gpu_sdf_cache_misses += reinterpret_cast<const u32 *>(control.mapped)[5];
                stats.gpu_sdf_cached_pixels += reinterpret_cast<const u32 *>(control.mapped)[8];
            }
            ++stats.gpu_sdf_telemetry_reads;
            job->harvested = job->bin.serial;
        }
    };
    harvest(utility);
    for (auto *surface : surfaces)
        for (auto &slot : surface->slots) harvest(slot);
}
} // namespace vk_detail
#endif
