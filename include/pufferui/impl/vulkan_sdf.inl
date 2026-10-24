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
    page previous[5], shape_data;
    i32 published_bank = -1;
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
        if (pool) d->vkDestroyDescriptorPool(d->native.device, pool, nullptr);
        bin.free_storage(mask);
        bin.free_storage(state);
        for (auto &p : history) bin.free_storage(p);
        if (saved)
        {
            d->stats.gpu_sdf_cache_image_bytes -= saved->allocation;
            d->destroy_texture(saved);
        }
    }
    bool prepare(device &d, sdf_slot_resources &resources, u32 capacity)
    {
        const u32 width = static_cast<u32>(d.target->width),
                  height = static_cast<u32>(d.target->height);
        const u32 tx = (width + 31) / 32, ty = (height + 31) / 32;
        const u32 cx = (width + 255) / 256, cy = (height + 255) / 256;
        const u64 count = d.sdf_tape.commands.size(), words = (count + 31) / 32;
        const u64 tiles = static_cast<u64>(tx) * ty;
        const u64 sizes[] = {count * sizeof(paint_detail::command), cx * cy * words * 4,
                             (tiles * 2 + 1) * 4, static_cast<u64>(capacity) * 4, 64};
        const u64 pixels = static_cast<u64>(d.target->width) * d.target->height;
        const u64 extra[] = {((pixels + 31) / 32) * 4, tiles * 16,
                             (d.sdf_shapes.empty() ? 1 : d.sdf_shapes.size()) * sizeof(sdf_shape)};
        const u64 alignment = std::max<u64>(4, d.properties.limits.minStorageBufferOffsetAlignment);
        u64 required = 0;
        for (const auto size : sizes)
            required += (std::max<u64>(4, size) + alignment - 1) & ~(alignment - 1);
        for (const auto size : extra) required += (size + alignment - 1) & ~(alignment - 1);
        resources.required_bytes += required;
        auto &arena = resources.banks[resources.bank];
        if (width > resources.width || height > resources.height || !arena.buffer ||
            required > arena.capacity - arena.used ||
            (tiles + 63) / 64 > d.properties.limits.maxComputeWorkGroupCount[0] ||
            (count + 63) / 64 > d.properties.limits.maxComputeWorkGroupCount[0])
            return false;
        reset_history = published_bank < 0 || published_bank == static_cast<i32>(resources.bank) ||
                        bin.parameters[0] != width || bin.parameters[1] != height;
        std::copy(std::begin(history), std::begin(history) + 5, previous);
        const u32 parameters[] = {width,
                                  height,
                                  32,
                                  256,
                                  tx,
                                  ty,
                                  cx,
                                  cy,
                                  static_cast<u32>(count),
                                  static_cast<u32>(words),
                                  capacity,
                                  static_cast<u32>(tiles)};
        std::copy(std::begin(parameters), std::end(parameters), bin.parameters);
        bin.recorded = false;
        bin.serial = 0;
        bin.arena = &arena;
        for (u32 i = 0; i < 5; ++i)
        {
            bin.buffers[i] = {};
            if (!bin.storage(bin.buffers[i], sizes[i])) return false;
        }
        mask = state = shape_data = {};
        if (!bin.storage(mask, ((pixels + 31) / 32) * 4)) return false;
        if (!bin.storage(state, tiles * 16) || !bin.storage(shape_data, extra[2])) return false;
        if (count) std::memcpy(bin.buffers[0].mapped, d.sdf_tape.commands.data(), sizes[0]);
        bin.buffers[0].used = sizes[0];
        if (d.sdf_shapes.empty())
            std::memset(shape_data.mapped, 0, sizeof(sdf_shape));
        else
            std::memcpy(shape_data.mapped, d.sdf_shapes.data(), extra[2]);
        shape_data.used = extra[2];
        if (!d.flush_page(bin.buffers[0]) || !d.flush_page(shape_data)) return false;
        VkDescriptorBufferInfo infos[5]{};
        VkWriteDescriptorSet writes[5]{};
        for (u32 i = 0; i < 5; ++i)
        {
            infos[i] = {bin.buffers[i].buffer, bin.buffers[i].offset, bin.buffers[i].capacity};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = bin.descriptor;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        d.vkUpdateDescriptorSets(d.native.device, 5, writes, 0, nullptr);
        cache_enabled = saved && d.options.tile_cache &&
                        (!d.current_surface || !d.current_surface->sdf_preparation_pending) &&
                        saved->width >= d.target->width && saved->height >= d.target->height;
        history[0] = bin.buffers[0];
        history[1] = bin.buffers[2];
        history[2] = bin.buffers[3];
        history[3] = shape_data;
        history[4] = mask;
        if (reset_history) std::copy(std::begin(history), std::begin(history) + 5, previous);
        published_bank = static_cast<i32>(resources.bank);
        return true;
    }
};
bool device::initialize_sdf_compute()
{
    if (paint_detail::sdf_painter(options.painter))
    {
        sdf_shapes.reserve(65536);
        sdf_spans.reserve(65536);
        slug_data.reserve(1024 * 1024);
        if (!prepare_sdf_descriptors(utility)) return false;
        if (options.vector_text)
        {
            const u64 bytes = std::min<u64>(512 * 1024, options.upload_budget_bytes / 8);
            if (bytes >= 4096)
            {
                if (!allocate_buffer(slug_storage, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
                    return false;
                // Append-only coherent writes cannot overlap an older GPU read.
                // Noncoherent heaps retain the per-submission upload path.
                if (!slug_storage.coherent)
                    free_page(slug_storage);
                else if (!allocate_device_buffer(slug_storage))
                    return false;
            }
        }
    }
    if (options.painter != vulkan_painter_mode::COMPUTE_SDF) return true;
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(native.physical_device, VK_FORMAT_R8G8B8A8_UNORM, &format);
    if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
        properties.limits.maxPerStageDescriptorStorageBuffers < 16)
        return fail("compute SDF needs RGBA8 storage and sixteen storage-buffer bindings");
    sdf_blend = new rgba8_blend_lut();
    const bool initialized = sdf_blend->initialize(*this);
    stats.gpu_sdf_calibration_ms = sdf_blend->preparation_ms;
    if (!initialized) return false;
    VkDescriptorSetLayoutBinding bindings[9]{};
    for (u32 i = 0; i < 9; ++i)
        bindings[i] = {i,
                       (i == 0 || i == 6) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                       1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lc.bindingCount = 9;
    lc.pBindings = bindings;
    if (!check(vkCreateDescriptorSetLayout(native.device, &lc, nullptr, &sdf_image_layout),
               "SDF compute image layout"))
        return false;
    const VkDescriptorSetLayout layouts[] = {bin_descriptor_layout, sdf_layout, sdf_image_layout};
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 80};
    VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pc.setLayoutCount = 3;
    pc.pSetLayouts = layouts;
    pc.pushConstantRangeCount = 1;
    pc.pPushConstantRanges = &range;
    if (!check(vkCreatePipelineLayout(native.device, &pc, nullptr, &sdf_compute_layout),
               "SDF compute layout"))
        return false;
    const u32 *sources[] = {vulkan_sdf_paint, vulkan_paint_args};
    const usize sizes[] = {sizeof(vulkan_sdf_paint), sizeof(vulkan_paint_args)};
    for (u32 i = 0; i < 2; ++i)
    {
        sdf_pipelines[i] = compute_pipeline(
            *this, i == 1 ? bin_pipeline_layout : sdf_compute_layout, sources[i], sizes[i]);
        if (!sdf_pipelines[i]) return false;
    }
    return !options.tile_cache || options.adaptive_compute || initialize_sdf_cache_kernels();
}
bool device::initialize_sdf_cache_kernels()
{
    const u32 *sources[] = {vulkan_sdf_compare, vulkan_sdf_compact, vulkan_sdf_restore,
                            vulkan_sdf_commit};
    const usize sizes[] = {sizeof(vulkan_sdf_compare), sizeof(vulkan_sdf_compact),
                           sizeof(vulkan_sdf_restore), sizeof(vulkan_sdf_commit)};
    for (u32 i = 0; i < 4; ++i)
    {
        if (sdf_pipelines[i + 2]) continue;
        sdf_pipelines[i + 2] = compute_pipeline(*this, sdf_compute_layout, sources[i], sizes[i]);
        if (!sdf_pipelines[i + 2]) return false;
    }
    return true;
}
void device::release_sdf_compute()
{
    free_page(slug_storage);
    delete sdf_blend;
    sdf_blend = nullptr;
    for (auto pipeline : sdf_pipelines)
        if (pipeline) vkDestroyPipeline(native.device, pipeline, nullptr);
    for (auto pipeline : bin_pipelines)
        if (pipeline) vkDestroyPipeline(native.device, pipeline, nullptr);
    if (sdf_compute_layout) vkDestroyPipelineLayout(native.device, sdf_compute_layout, nullptr);
    if (bin_pipeline_layout) vkDestroyPipelineLayout(native.device, bin_pipeline_layout, nullptr);
    if (sdf_image_layout) vkDestroyDescriptorSetLayout(native.device, sdf_image_layout, nullptr);
    if (bin_descriptor_layout)
        vkDestroyDescriptorSetLayout(native.device, bin_descriptor_layout, nullptr);
}
void device::destroy_sdf_jobs(frame_slot &slot)
{
    delete slot.sdf_resources;
    slot.sdf_resources = nullptr;
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
    auto *resources = frame->sdf_resources;
    if (!resources && options.adaptive_compute && current_surface)
    {
        if (!current_surface->sdf_compute_requested)
        {
            current_surface->sdf_compute_requested = true;
            current_surface->sdf_preparation_pending = true;
        }
        ++stats.gpu_sdf_bypasses;
        return nullptr;
    }
    const usize index = frame->sdf_job_cursor++;
    if (!resources || index >= resources->jobs.size())
    {
        ++stats.gpu_sdf_capacity_bypasses;
        return nullptr;
    }
    if (resources->pending)
    {
        resources->peak_required_bytes =
            std::max(resources->peak_required_bytes, resources->required_bytes);
        resources->required_bytes = 0;
        if (resources->banks[1].buffer) resources->bank ^= 1;
        resources->banks[resources->bank].used = 0;
        resources->pending = false;
        frame->sdf_jobs.clear();
    }
    auto *job = resources->jobs[index];
    if (!job->prepare(*this, *resources, static_cast<u32>(capacity)))
    {
        ++stats.gpu_sdf_capacity_bypasses;
        return nullptr;
    }
    frame->sdf_jobs.push_back(job); // capacity is reserved by slot preparation
    return job;
}
VkDescriptorBufferInfo device::sdf_route(sdf_gpu_job *job)
{
    return {job->mask.buffer, job->mask.offset, job->mask.capacity};
}
VkDescriptorBufferInfo device::sdf_shape_data(sdf_gpu_job *job)
{
    return {job->shape_data.buffer, job->shape_data.offset, job->shape_data.capacity};
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
        vkCmdFillBuffer(frame->command, bin.buffers[4].buffer, bin.buffers[4].offset + 12, 4, 1);
    }
    vkCmdFillBuffer(frame->command, job->mask.buffer, job->mask.offset, job->mask.capacity, 0);
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
    for (u32 i = 0; i < 5; ++i)
        buffers[i] = {job->previous[i].buffer, job->previous[i].offset, job->previous[i].capacity};
    buffers[5] = {job->state.buffer, job->state.offset, job->state.capacity};
    buffers[6] = {job->history[5].buffer, job->history[5].offset, job->history[5].capacity};
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
            vkCmdFillBuffer(frame->command, job->history[5].buffer, job->history[5].offset,
                            job->history[5].capacity, 0);
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
    vkCmdDispatchIndirect(frame->command, bin.buffers[4].buffer, bin.buffers[4].offset);
    if (job->cache_enabled)
    {
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
        // Current commands/work/list/shape/route stay immutable in this slot's
        // arena bank. The next use writes the other bank, so no copies are needed.
        const VkBufferCopy hash_copy{job->state.offset + static_cast<VkDeviceSize>(tiles) * 4,
                                     job->history[5].offset + 4,
                                     static_cast<VkDeviceSize>(tiles) * 8};
        vkCmdCopyBuffer(frame->command, job->state.buffer, job->history[5].buffer, 1, &hash_copy);
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
