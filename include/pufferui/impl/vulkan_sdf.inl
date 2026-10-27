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
    VkDescriptorBufferInfo producer_controls[64]{};
    u64 dependency_offset = 0;
    u32 ordinal = 0;
    gpu_bin_job bin;
    page mask;
    page history[6], state;
    page previous[5], shape_data, shape_input;
    i32 published_bank = -1;
    texture *saved = nullptr, *native_before = nullptr;
    texture *alpha_source = nullptr;
    bool cache_enabled = false, reset_history = true;
    VkDescriptorSetLayout image_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet image_set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline painter = VK_NULL_HANDLE, args = VK_NULL_HANDLE;
    VkPipeline compare = VK_NULL_HANDLE, compact = VK_NULL_HANDLE, restore = VK_NULL_HANDLE,
               commit = VK_NULL_HANDLE;
    VkPipeline native_snapshot = VK_NULL_HANDLE, native_capture = VK_NULL_HANDLE;
    u32 parameters[20]{};
    u64 harvested = 0, eligible_pixels = 0;
    ~sdf_gpu_job()
    {
        auto *d = bin.dev;
        if (!d) return;
        for (auto *image : d->textures)
            if (image->producer == this) image->producer = nullptr;
        for (auto &producer : d->program_producers)
            if (producer == this) producer = nullptr;
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
        if (native_before)
        {
            d->stats.gpu_sdf_cache_image_bytes -= native_before->allocation;
            d->destroy_texture(native_before);
        }
    }
    bool prepare(device &d, sdf_slot_resources &resources, u32 capacity)
    {
        alpha_source = d.alpha_sample;
        const u32 width = static_cast<u32>(d.target->width),
                  height = static_cast<u32>(d.target->height);
        const u32 tx = (width + 31) / 32, ty = (height + 31) / 32;
        const u32 cx = (width + 255) / 256, cy = (height + 255) / 256;
        const auto commands = d.command_view();
        const auto shapes = d.shape_view();
        const auto payload = d.command_payload_view();
        const u64 count = commands.size(), words = (count + 31) / 32;
        const u64 tiles = static_cast<u64>(tx) * ty;
        const u64 command_bytes = count * sizeof(paint_detail::command);
        const u64 dependency_bytes = d.sdf_functions ? 64 * 16 : 0;
        const u64 sizes[] = {command_bytes + payload.size() + dependency_bytes, cx * cy * words * 4,
                             (tiles * 2 + 1) * 4, static_cast<u64>(capacity) * 4,
                             d.sdf_functions ? 80u : 64u};
        const u64 pixels = static_cast<u64>(d.target->width) * d.target->height;
        const u64 replay_bytes = d.sdf_functions ? 64 * sizeof(VkDrawIndexedIndirectCommand) : 0;
        const u64 extra[] = {((pixels + 31) / 32) * 4, tiles * 16 + replay_bytes,
                             (shapes.empty() ? 1 : shapes.size()) * sizeof(sdf_shape)};
        const u64 alignment = std::max<u64>(4, d.properties.limits.minStorageBufferOffsetAlignment);
        const auto aligned = [alignment](u64 n)
        { return (std::max<u64>(4, n) + alignment - 1) & ~(alignment - 1); };
        u64 required = aligned(extra[2]);
        for (u32 i = 0; i < 4; ++i) required += aligned(sizes[i]);
        const u64 input_required = aligned(sizes[0]) + aligned(extra[2]);
        const u64 read_required = aligned(sizes[4]) + aligned(extra[0]) + aligned(extra[1]);
        resources.required_bytes += required + input_required + read_required;
        auto &arena = resources.banks[resources.bank];
        auto &input_arena = resources.inputs[0];
        auto &read_arena = resources.readable[resources.bank];
        resources.role_peaks[0] = std::max(resources.role_peaks[0], arena.used + required);
        resources.role_peaks[1] =
            std::max(resources.role_peaks[1], input_arena.used + input_required);
        resources.role_peaks[2] =
            std::max(resources.role_peaks[2], read_arena.used + read_required);
        if (width > resources.width || height > resources.height || !arena.buffer ||
            required > arena.capacity - arena.used ||
            input_required > input_arena.capacity - input_arena.used ||
            read_required > read_arena.capacity - read_arena.used ||
            (tiles + 63) / 64 > d.properties.limits.maxComputeWorkGroupCount[0] ||
            (count + 63) / 64 > d.properties.limits.maxComputeWorkGroupCount[0])
        {
            return false;
        }
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
            bin.arena = i == 4 ? &read_arena : &arena;
            if (!bin.storage(bin.buffers[i], sizes[i])) return false;
        }
        mask = state = shape_data = {};
        bin.arena = &read_arena;
        if (!bin.storage(mask, ((pixels + 31) / 32) * 4)) return false;
        if (!bin.storage(state, tiles * 16 + replay_bytes)) return false;
        bin.arena = &arena;
        if (!bin.storage(shape_data, extra[2])) return false;
        bin.arena = &input_arena;
        bin.command_input = shape_input = {};
        if (!bin.storage(bin.command_input, sizes[0]) || !bin.storage(shape_input, extra[2]))
            return false;
        bin.arena = &arena;
        if (count)
            std::memcpy(bin.command_input.mapped, commands.data(),
                        static_cast<usize>(command_bytes));
        if (!payload.empty())
            std::memcpy(bin.command_input.mapped + command_bytes, payload.data(), payload.size());
        dependency_offset = command_bytes + payload.size();
        std::fill(std::begin(producer_controls), std::end(producer_controls),
                  VkDescriptorBufferInfo{});
        if (dependency_bytes)
        {
            std::memset(bin.command_input.mapped + dependency_offset, 1,
                        static_cast<usize>(dependency_bytes));
            for (u32 packet = 0; packet < 64; ++packet)
                if (const auto *producer = d.program_producers[packet];
                    producer && producer != this)
                    producer_controls[packet] = {producer->bin.buffers[4].buffer,
                                                 producer->bin.buffers[4].offset + 12, 12};
            for (const auto &command : commands)
            {
                u32 index = 0;
                const u64 at = command_bytes + command.payload_offset;
                std::memcpy(&index, bin.command_input.mapped + at + 68, 4);
                if (index && index <= 64)
                {
                    const u32 word = static_cast<u32>((dependency_offset + (index - 1) * 16) / 4);
                    std::memcpy(bin.command_input.mapped + at + 68, &word, 4);
                }
            }
        }
        bin.command_input.used = sizes[0];
        if (shapes.empty())
            std::memset(shape_input.mapped, 0, sizeof(sdf_shape));
        else
            std::memcpy(shape_input.mapped, shapes.data(), extra[2]);
        shape_input.used = extra[2];
        if (!d.flush_page(bin.command_input) || !d.flush_page(shape_input)) return false;
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
        cache_enabled = saved && d.options.tile_cache && (!d.sdf_functions || native_before) &&
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
        slug_data.reserve(options.vector_text ? 16 * 1024 * 1024 : 0);
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
        if (!initialize_glyph_source() || !prepare_glyph_source(utility)) return false;
    }
    if (!paint_detail::compute_painter(options.painter)) return true;
    // Deferred kernel creation must preserve the device's capability contract.
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(native.physical_device, VK_FORMAT_R8G8B8A8_UNORM, &format);
    if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
        properties.limits.maxPerStageDescriptorStorageBuffers < 16)
        return fail("compute SDF needs RGBA8 storage and sixteen storage-buffer bindings");
    if (options.painter == vulkan_painter_mode::INTEGRATED && options.whole_program_cache)
        return true;
    return initialize_sdf_compute_kernels();
}
bool device::initialize_sdf_compute_kernels()
{
    if (!paint_detail::compute_painter(options.painter)) return true;
    if (sdf_compute_layout || (sdf_functions && !sdf_functions->valid)) return true;
    if (!initialize_bin_kernels()) return false;
    const bool integrated = options.painter == vulkan_painter_mode::INTEGRATED;
    if (integrated)
    {
        sdf_functions = new native_function_cache();
        if (!sdf_functions->initialize(*this))
        {
            sdf_functions->release();
            return result == VK_SUCCESS; // bounded budget rejection retains complete raster
        }
        stats.gpu_sdf_cache_image_bytes += sdf_functions->atlas->allocation;
    }
    else
    {
        sdf_blend = new rgba8_blend_lut();
        const bool initialized = sdf_blend->initialize(*this);
        stats.gpu_sdf_calibration_ms = sdf_blend->preparation_ms;
        if (!initialized) return false;
    }
    const u32 binding_count = integrated ? 12u : 9u;
    VkDescriptorSetLayoutBinding bindings[12]{};
    for (u32 i = 0; i < binding_count; ++i)
        bindings[i] = {i,
                       (i == 9 || i == 11)             ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                       : (i == 0 || i == 6 || i == 10) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                       : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                       1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lc.bindingCount = binding_count;
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
    const u32 *sources[] = {integrated ? vulkan_integrated_paint : vulkan_sdf_paint,
                            vulkan_paint_args};
    const usize sizes[] = {integrated ? sizeof(vulkan_integrated_paint) : sizeof(vulkan_sdf_paint),
                           sizeof(vulkan_paint_args)};
    for (u32 i = 0; i < 2; ++i)
    {
        sdf_pipelines[i] = compute_pipeline(
            *this, i == 1 ? bin_pipeline_layout : sdf_compute_layout, sources[i], sizes[i]);
        if (!sdf_pipelines[i]) return false;
    }
    if (integrated)
    {
        native_replay_pipeline = compute_pipeline(
            *this, sdf_compute_layout, vulkan_integrated_replay, sizeof(vulkan_integrated_replay));
        if (!native_replay_pipeline) return false;
    }
    return !options.tile_cache || options.adaptive_compute || initialize_sdf_cache_kernels();
}
bool device::initialize_sdf_cache_kernels()
{
    const bool integrated = options.painter == vulkan_painter_mode::INTEGRATED;
    const u32 *sources[] = {integrated ? vulkan_integrated_compare : vulkan_sdf_compare,
                            vulkan_sdf_compact,
                            integrated ? vulkan_integrated_restore : vulkan_sdf_restore,
                            integrated ? vulkan_integrated_commit : vulkan_sdf_commit};
    const usize sizes[] = {
        integrated ? sizeof(vulkan_integrated_compare) : sizeof(vulkan_sdf_compare),
        sizeof(vulkan_sdf_compact),
        integrated ? sizeof(vulkan_integrated_restore) : sizeof(vulkan_sdf_restore),
        integrated ? sizeof(vulkan_integrated_commit) : sizeof(vulkan_sdf_commit)};
    for (u32 i = 0; i < 4; ++i)
    {
        if (sdf_pipelines[i + 2]) continue;
        sdf_pipelines[i + 2] = compute_pipeline(*this, sdf_compute_layout, sources[i], sizes[i]);
        if (!sdf_pipelines[i + 2]) return false;
    }
    if (integrated)
    {
        const u32 *native_sources[] = {vulkan_integrated_snapshot, vulkan_integrated_capture};
        const usize native_sizes[] = {sizeof(vulkan_integrated_snapshot),
                                      sizeof(vulkan_integrated_capture)};
        for (u32 i = 0; i < 2; ++i)
        {
            if (!native_cache_pipelines[i])
                native_cache_pipelines[i] =
                    compute_pipeline(*this, sdf_compute_layout, native_sources[i], native_sizes[i]);
            if (!native_cache_pipelines[i]) return false;
        }
    }
    return true;
}
void device::release_sdf_compute()
{
    release_glyph_source();
    free_page(slug_storage);
    delete sdf_blend;
    sdf_blend = nullptr;
    if (sdf_functions && sdf_functions->atlas)
        stats.gpu_sdf_cache_image_bytes -= sdf_functions->atlas->allocation;
    delete sdf_functions;
    sdf_functions = nullptr;
    for (auto pipeline : sdf_pipelines)
        if (pipeline) vkDestroyPipeline(native.device, pipeline, nullptr);
    for (auto pipeline : native_cache_pipelines)
        if (pipeline) vkDestroyPipeline(native.device, pipeline, nullptr);
    if (native_replay_pipeline) vkDestroyPipeline(native.device, native_replay_pipeline, nullptr);
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
    const auto commands = command_view();
    const auto shapes = shape_view();
    const auto payload = command_payload_view();
    const bool blend_ready = options.painter == vulkan_painter_mode::INTEGRATED
                                 ? sdf_functions && sdf_functions->valid
                                 : sdf_blend && sdf_blend->valid;
    if (!paint_detail::compute_painter(options.painter) || !sdf_tape.valid || commands.empty() ||
        !blend_ready || !target || target->format != VK_FORMAT_R8G8B8A8_UNORM ||
        !(target->usage & VK_IMAGE_USAGE_STORAGE_BIT))
        return nullptr;
    sdf_cpu_phase phase{stats.sdf_prepare_ms};
    for (const auto &command : commands)
        if (command.operation > 2 ||
            (command.operation == 2 && command.resource >= shapes.size()) ||
            (sdf_functions && (command.payload_offset > payload.size() ||
                               command.payload_size > payload.size() - command.payload_offset ||
                               command.payload_size < 96 || command.payload_size % 4)))
            return nullptr; // malformed metadata always selects complete raster replay
    if (options.adaptive_compute)
    {
        bool eligible = options.tile_cache && commands.size() >= 2048;
        for (const auto &command : commands)
            if (command.operation == 0 || (command.flags >> 24) != 255 ||
                (command.operation == 2 && shapes[command.resource].reserved[0] == 1))
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
    for (const auto &c : commands)
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
        resources->inputs[0].used = 0;
        resources->readable[resources->bank].used = 0;
        resources->pending = false;
        frame->sdf_jobs.clear();
    }
    auto *job = resources->jobs[index];
    job->ordinal = static_cast<u32>(index);
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
bool device::live_producer(texture *image) const
{
    if (!image || !image->producer || !frame || !frame->recording ||
        image->producer_token != frame->recording_token)
        return false;
    const auto &job = *image->producer;
    return job.cache_enabled && job.bin.recorded && job.bin.serial == next_serial &&
           job.parameters[17] == static_cast<u32>(image->generation) &&
           job.parameters[18] == static_cast<u32>(image->generation >> 32);
}
u32 device::producer_role(texture *image) const
{
    return live_producer(image) ? image->producer->ordinal + 1 : 0;
}
VkDescriptorBufferInfo device::sdf_shape_data(sdf_gpu_job *job)
{
    return {job->shape_data.buffer, job->shape_data.offset, job->shape_data.capacity};
}
VkDescriptorBufferInfo device::sdf_lut()
{
    const auto &p =
        sdf_functions ? sdf_functions->storage.buffers[0] : sdf_blend->storage.buffers[1];
    return {p.buffer, 0, p.capacity};
}
void device::replay_sdf_native(sdf_gpu_job *job, u32 packet)
{
    vkCmdDrawIndexedIndirect(frame->command, job->state.buffer,
                             job->state.offset + static_cast<u64>(job->bin.parameters[11]) * 16 +
                                 static_cast<u64>(packet) * sizeof(VkDrawIndexedIndirectCommand),
                             1, sizeof(VkDrawIndexedIndirectCommand));
}
bool device::execute_sdf_compute(sdf_gpu_job *job, VkDescriptorSet shapes,
                                 const VkDescriptorBufferInfo &shape_data)
{
    sdf_cpu_phase phase{stats.sdf_record_ms};
    auto &bin = job->bin;
    end_rendering();
    if (sdf_functions && !sdf_functions->record_begin()) return false;
    const VkBufferCopy shape_copy{job->shape_input.offset, job->shape_data.offset,
                                  job->shape_input.used};
    vkCmdCopyBuffer(frame->command, job->shape_input.buffer, job->shape_data.buffer, 1,
                    &shape_copy);
    if (!bin.record()) return false;
    if (sdf_functions)
    {
        bin.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
        for (u32 packet = 0; packet < 64; ++packet)
        {
            const auto &control = job->producer_controls[packet];
            if (!control.buffer) continue;
            const VkBufferCopy copy{
                control.offset, bin.buffers[0].offset + job->dependency_offset + packet * 16, 12};
            vkCmdCopyBuffer(frame->command, control.buffer, bin.buffers[0].buffer, 1, &copy);
        }
    }
    if (sdf_cache_faults & 2u)
    {
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdFillBuffer(frame->command, bin.buffers[4].buffer, bin.buffers[4].offset + 12, 4, 1);
    }
    vkCmdFillBuffer(frame->command, job->mask.buffer, job->mask.offset, job->mask.capacity, 0);
    if (sdf_functions)
        vkCmdFillBuffer(frame->command, job->state.buffer,
                        job->state.offset + static_cast<u64>(bin.parameters[11]) * 16,
                        64 * sizeof(VkDrawIndexedIndirectCommand), 0);
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
    auto *alpha_input = job->alpha_source ? job->alpha_source : white;
    if (sdf_functions) transition(alpha_input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    transition(target, VK_IMAGE_LAYOUT_GENERAL);
    if (job->cache_enabled) transition(job->saved, VK_IMAGE_LAYOUT_GENERAL);
    if (job->cache_enabled && job->native_before)
        transition(job->native_before, VK_IMAGE_LAYOUT_GENERAL);
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
    VkWriteDescriptorSet writes[12]{};
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
    const VkDescriptorImageInfo function_image{
        samplers[0], sdf_functions ? sdf_functions->atlas->view : VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (sdf_functions)
    {
        auto &write = writes[9];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = job->image_set;
        write.dstBinding = 9;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &function_image;
    }
    const VkDescriptorImageInfo before_image{
        VK_NULL_HANDLE, job->native_before ? job->native_before->view : target->view,
        VK_IMAGE_LAYOUT_GENERAL};
    if (sdf_functions)
    {
        auto &write = writes[10];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = job->image_set;
        write.dstBinding = 10;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &before_image;
    }
    const VkDescriptorImageInfo alpha_image{samplers[0], alpha_input->view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (sdf_functions)
    {
        auto &write = writes[11];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = job->image_set;
        write.dstBinding = 11;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &alpha_image;
    }
    vkUpdateDescriptorSets(native.device, sdf_functions ? 12u : 9u, writes, 0, nullptr);
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
        if (sdf_functions)
            vkCmdDispatch(frame->command, tiles < max_x ? tiles : max_x,
                          tiles / max_x + (tiles % max_x != 0), 1);
        else
            vkCmdDispatch(frame->command, (tiles + 63) / 64, 1, 1);
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        if (sdf_functions)
        {
            vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->native_snapshot);
            vkCmdDispatch(frame->command, tiles < max_x ? tiles : max_x,
                          tiles / max_x + (tiles % max_x != 0), 1);
            bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        }
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
    if (sdf_functions)
    {
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, native_replay_pipeline);
        vkCmdDispatch(frame->command, (bin.parameters[8] + 63) / 64, 1, 1);
        bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    }
    if (job->cache_enabled && !sdf_functions)
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
    target->last_use = next_serial;
    if (sdf_functions)
    {
        if (!sdf_functions->record_prepare()) return false;
    }
    else
        sdf_blend->storage.serial = next_serial;
    return true;
}
bool device::finish_sdf_compute(sdf_gpu_job *job, VkDescriptorSet shapes)
{
    if (!sdf_functions || !job || !job->cache_enabled) return true;
    end_rendering();
    transition(target, VK_IMAGE_LAYOUT_GENERAL);
    transition(job->saved, VK_IMAGE_LAYOUT_GENERAL);
    transition(job->native_before, VK_IMAGE_LAYOUT_GENERAL);
    auto &bin = job->bin;
    bin.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    const VkDescriptorSet sets[] = {bin.descriptor, shapes, job->image_set};
    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->layout, 0, 3, sets,
                            0, nullptr);
    vkCmdPushConstants(frame->command, job->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(job->parameters), job->parameters);
    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->native_capture);
    const u32 tiles = bin.parameters[11], max_x = properties.limits.maxComputeWorkGroupCount[0];
    vkCmdDispatch(frame->command, tiles < max_x ? tiles : max_x,
                  tiles / max_x + (tiles % max_x != 0), 1);
    bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, job->commit);
    vkCmdDispatch(frame->command, 1, 1, 1);
    bin.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    job->saved->last_use = job->native_before->last_use = next_serial;
    target->producer = job;
    target->producer_token = frame->recording_token;
    return result == VK_SUCCESS;
}
void device::harvest_sdf_stats()
{
    if (!paint_detail::compute_painter(options.painter)) return;
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
            const auto *counters = reinterpret_cast<const u32 *>(control.mapped);
            job->eligible_pixels =
                static_cast<u64>(counters[6]) + (job->cache_enabled ? counters[8] : 0);
            stats.gpu_sdf_pixels += counters[6];
            if (sdf_functions)
            {
                for (u32 i = 0; i < 4; ++i) integrated_counts[i] += counters[9 + i];
                integrated_alpha_pixels += counters[15];
                glyph_source_gpu_hits += counters[16];
                native_replay_counts[0] += counters[13];
                native_replay_counts[1] += counters[14];
            }
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
