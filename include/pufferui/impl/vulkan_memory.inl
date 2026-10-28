// Vulkan command and mapped upload ownership.
// Slots are reused only after their own timeline completion.
// Non-coherent mappings are explicitly flushed/invalidated.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
inline f64 wall_ms()
{
    return std::chrono::duration<f64, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
u32 device::memory_type(u32 bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
{
    for (u32 pass = 0; pass < 2; ++pass)
        for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) &&
                (memory_properties.memoryTypes[i].propertyFlags & required) == required &&
                (pass || (memory_properties.memoryTypes[i].propertyFlags & preferred) == preferred))
                return i;
    return UINT32_MAX;
}
u64 device::completed()
{
    if (known_completed >= next_serial - 1) return known_completed;
    u64 value = 0;
    if (timeline)
        check(vkGetSemaphoreCounterValue(native.device, timeline, &value),
              "vkGetSemaphoreCounterValue");
    known_completed = std::max(known_completed, value);
    return known_completed;
}
bool device::wait(u64 value)
{
    if (value <= known_completed) return true;
    if (value >= next_serial) return false; // only values already submitted by this backend
    const f64 start = wall_ms();
    VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wi.semaphoreCount = 1;
    wi.pSemaphores = &timeline;
    wi.pValues = &value;
    const bool okay = check(vkWaitSemaphores(native.device, &wi, UINT64_MAX), "vkWaitSemaphores");
    stats.slot_wait_ms += wall_ms() - start;
    if (okay) known_completed = std::max(known_completed, value);
    return okay;
}
bool device::begin(frame_slot &slot)
{
    if (result != VK_SUCCESS) return false;
    if (slot.recording)
    {
        frame = &slot;
        return true;
    }
    if (!wait(slot.serial)) return false;
    collect();
    if (!slot.pool)
    {
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pc.queueFamilyIndex = native.queue_family;
        pc.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        if (!check(vkCreateCommandPool(native.device, &pc, nullptr, &slot.pool),
                   "vkCreateCommandPool"))
            return false;
        VkCommandBufferAllocateInfo ac{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ac.commandPool = slot.pool;
        ac.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ac.commandBufferCount = 1;
        if (!check(vkAllocateCommandBuffers(native.device, &ac, &slot.command),
                   "vkAllocateCommandBuffers"))
            return false;
    }
    else if (!check(vkResetCommandPool(native.device, slot.pool, 0), "vkResetCommandPool"))
        return false;
    for (auto &p : slot.pages) p.used = 0;
    slot.waits.clear();
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(slot.command, &bi), "vkBeginCommandBuffer")) return false;
    slot.graphics = {};
    if (stats.timestamps_supported)
    {
        if (!slot.queries)
        {
            VkQueryPoolCreateInfo qc{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qc.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qc.queryCount = 2;
            if (!check(vkCreateQueryPool(native.device, &qc, nullptr, &slot.queries),
                       "vkCreateQueryPool"))
                return false;
        }
        vkCmdResetQueryPool(slot.command, slot.queries, 0, 2);
        vkCmdWriteTimestamp2(slot.command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, slot.queries, 0);
    }
    slot.recording = true;
    frame = &slot;
    return true;
}
bool device::commands()
{
    if (result != VK_SUCCESS) return false;
    if (frame && frame->recording) return true;
    return begin(frame ? *frame : utility);
}
void device::end_rendering()
{
    if (rendering_target && frame && frame->recording) vkCmdEndRendering(frame->command);
    rendering_target = nullptr;
}
bool device::flush_page(page &p)
{
    if (p.coherent || !p.used) return true;
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = p.memory;
    range.size = VK_WHOLE_SIZE;
    return check(vkFlushMappedMemoryRanges(native.device, 1, &range), "vkFlushMappedMemoryRanges");
}
u64 device::submit(VkSemaphore acquired, VkSemaphore finished)
{
    flush_text();
    if (!frame || !frame->recording || result != VK_SUCCESS) return 0;
    end_rendering();
    if (frame->queries)
        vkCmdWriteTimestamp2(frame->command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame->queries,
                             1);
    for (auto &p : frame->pages)
        if (!flush_page(p)) return 0;
    if (!check(vkEndCommandBuffer(frame->command), "vkEndCommandBuffer")) return 0;
    VkSemaphoreSubmitInfo ai{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    if (acquired)
    {
        ai.semaphore = acquired;
        ai.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        frame->waits.push_back(ai);
    }
    VkSemaphoreSubmitInfo signals[2]{};
    signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[0].semaphore = timeline;
    signals[0].value = next_serial;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[1].semaphore = finished;
    signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cb.commandBuffer = frame->command;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = static_cast<u32>(frame->waits.size());
    si.pWaitSemaphoreInfos = frame->waits.data();
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cb;
    si.signalSemaphoreInfoCount = finished ? 2u : 1u;
    si.pSignalSemaphoreInfos = signals;
    frame->recording = false;
    const f64 start = wall_ms();
    const bool submitted =
        check(vkQueueSubmit2(native.queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit2");
    stats.submit_ms += wall_ms() - start;
    if (!submitted) return 0;
    frame->serial = next_serial++;
    frame->query_serial = frame->serial;
    ++stats.submissions;
    return frame->serial;
}
bool device::allocate_buffer(page &p, VkDeviceSize bytes, VkBufferUsageFlags usage)
{
    if (!bytes || bytes > options.upload_budget_bytes)
        return fail("Vulkan buffer exceeds upload budget");
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = bytes;
    bc.usage = usage;
    bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(native.device, &bc, nullptr, &p.buffer), "vkCreateBuffer"))
        return false;
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(native.device, p.buffer, &req);
    if (!reserve_mapped(req.size))
    {
        free_page(p);
        return false;
    }
    const auto preferred = usage == VK_BUFFER_USAGE_TRANSFER_DST_BIT
                               ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT
                               : VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const u32 type =
        memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, preferred);
    if (type == UINT32_MAX)
    {
        free_page(p);
        return fail("host-visible Vulkan memory unavailable");
    }
    VkMemoryAllocateInfo ac{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ac.allocationSize = req.size;
    ac.memoryTypeIndex = type;
    if (!check(vkAllocateMemory(native.device, &ac, nullptr, &p.memory),
               "vkAllocateMemory(buffer)"))
    {
        free_page(p);
        return false;
    }
    p.allocation = req.size;
    stats.upload_allocated_bytes += p.allocation;
    if (stats.upload_allocated_bytes > stats.peak_upload_allocated_bytes)
        stats.peak_upload_allocated_bytes = stats.upload_allocated_bytes;
    if (!check(vkBindBufferMemory(native.device, p.buffer, p.memory, 0), "vkBindBufferMemory") ||
        !check(vkMapMemory(native.device, p.memory, 0, VK_WHOLE_SIZE, 0,
                           reinterpret_cast<void **>(&p.mapped)),
               "vkMapMemory"))
    {
        free_page(p);
        return false;
    }
    p.capacity = bytes;
    ++stats.buffer_allocations;
    if (paint_depth) ++stats.draw_buffer_allocations;
    p.allocation = req.size;
    p.coherent = (memory_properties.memoryTypes[type].propertyFlags &
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    return true;
}
void device::free_page(page &p)
{
    if (p.mapped) vkUnmapMemory(native.device, p.memory);
    if (p.buffer) vkDestroyBuffer(native.device, p.buffer, nullptr);
    if (p.memory) vkFreeMemory(native.device, p.memory, nullptr);
    if (p.device_buffer) vkDestroyBuffer(native.device, p.device_buffer, nullptr);
    if (p.device_memory) vkFreeMemory(native.device, p.device_memory, nullptr);
    stats.upload_allocated_bytes -= p.allocation + p.device_allocation;
    stats.device_upload_allocated_bytes -= p.device_allocation;
    p = {};
}
bool device::reserve_mapped(VkDeviceSize bytes)
{
    if (bytes > options.upload_budget_bytes)
        return fail("mapped allocation exceeds Vulkan upload budget");
    if (stats.upload_allocated_bytes <= options.upload_budget_bytes - bytes) return true;
    ++stats.budget_waits;
    const u64 done = completed();
    for (bool may_wait : {false, true})
    {
        auto trim = [&](frame_slot &slot)
        {
            if (stats.upload_allocated_bytes <= options.upload_budget_bytes - bytes ||
                &slot == frame || slot.recording || (!may_wait && slot.serial > done))
                return;
            if (!wait(slot.serial)) return;
            for (auto &p : slot.pages) free_page(p);
            slot.pages.clear();
            slot.geometry_chunk_bytes = 0;
        };
        trim(utility);
        for (surface *s : surfaces)
            for (auto &slot : s->slots) trim(slot);
        if (stats.upload_allocated_bytes <= options.upload_budget_bytes - bytes) return true;
    }
    if (frame)
    {
        if (frame->recording && !submit()) return false;
        if (!wait(frame->serial)) return false;
        for (auto &p : frame->pages) free_page(p);
        frame->pages.clear();
        frame->geometry_chunk_bytes = 0;
        if (!begin(*frame)) return false;
    }
    return stats.upload_allocated_bytes <= options.upload_budget_bytes - bytes ||
           fail("Vulkan mapped budget exhausted");
}
bool device::allocate_device_buffer(page &p)
{
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = p.capacity;
    bc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
               VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (!check(vkCreateBuffer(native.device, &bc, nullptr, &p.device_buffer),
               "vkCreateBuffer(device geometry)"))
        return false;
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(native.device, p.device_buffer, &req);
    if (!reserve_mapped(req.size)) return false;
    const u32 type = memory_type(req.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) return fail("device-local geometry memory unavailable");
    VkMemoryAllocateInfo ac{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ac.allocationSize = req.size;
    ac.memoryTypeIndex = type;
    if (!check(vkAllocateMemory(native.device, &ac, nullptr, &p.device_memory),
               "vkAllocateMemory(device geometry)"))
        return false;
    p.device_allocation = req.size;
    stats.upload_allocated_bytes += req.size;
    stats.device_upload_allocated_bytes += req.size;
    if (stats.upload_allocated_bytes > stats.peak_upload_allocated_bytes)
        stats.peak_upload_allocated_bytes = stats.upload_allocated_bytes;
    const bool okay = check(vkBindBufferMemory(native.device, p.device_buffer, p.device_memory, 0),
                            "vkBindBufferMemory(device geometry)");
    if (okay)
    {
        ++stats.buffer_allocations;
        if (paint_depth) ++stats.draw_buffer_allocations;
    }
    return okay;
}
page *device::upload(VkDeviceSize bytes, VkDeviceSize &offset, bool geometry)
{
    const bool staged = geometry && options.upload_mode == vulkan_upload_mode::STAGED;
    if (!commands() || !bytes || bytes > options.upload_budget_bytes)
    {
        fail("invalid Vulkan upload size");
        return nullptr;
    }
    for (auto &p : frame->pages)
    {
        if (p.staged != staged || p.geometry != geometry) continue;
        const VkDeviceSize at = (p.used + 31) & ~VkDeviceSize(31);
        if (at <= p.capacity && bytes <= p.capacity - at)
        {
            offset = at;
            p.used = at + bytes;
            stats.upload_bytes += bytes;
            if (geometry) frame->geometry_frame_bytes += (bytes + 31) & ~VkDeviceSize(31);
            return &p;
        }
    }
    const bool prepared =
        geometry && frame->geometry_chunk_bytes >= bytes &&
        std::any_of(frame->pages.begin(), frame->pages.end(), [&](const page &p)
                    { return p.geometry && p.staged == staged && p.capacity >= bytes; });
    if (prepared)
    {
        // The prepared arena is full. Its commands reference only the scene,
        // not the acquired swapchain image. Complete this ordered prefix and
        // reuse the same slot before the eventual present/blit submission.
        frame_slot *slot = frame;
        const u64 serial = submit();
        if (!serial || !wait(serial) || !begin(*slot)) return nullptr;
        return upload(bytes, offset, geometry);
    }
    VkDeviceSize capacity = 1024 * 1024;
    while (capacity < bytes && capacity <= options.upload_budget_bytes / 2) capacity *= 2;
    if (capacity < bytes) capacity = bytes;
    if (capacity > options.upload_budget_bytes) capacity = bytes;
    if (staged && capacity > options.upload_budget_bytes / 2) capacity = bytes;
    page fresh;
    if (!allocate_buffer(fresh, capacity,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        return nullptr;
    stats.upload_bytes += bytes;
    if (geometry) frame->geometry_frame_bytes += (bytes + 31) & ~VkDeviceSize(31);
    fresh.staged = staged;
    fresh.geometry = geometry;
    if (staged && !allocate_device_buffer(fresh))
    {
        free_page(fresh);
        return nullptr;
    }
    fresh.used = bytes;
    offset = 0;
    frame->pages.push_back(fresh);
    return &frame->pages.back();
}
void device::destroy_slot(frame_slot &slot)
{
    for (auto &p : slot.pages)
    {
        free_page(p);
    }
    if (slot.acquired) vkDestroySemaphore(native.device, slot.acquired, nullptr);
    if (slot.queries) vkDestroyQueryPool(native.device, slot.queries, nullptr);
    if (slot.pool) vkDestroyCommandPool(native.device, slot.pool, nullptr);
    slot = {};
}
void device::collect()
{
    const u64 done = completed();
    harvest(utility, done);
    for (auto *s : surfaces)
        for (auto &slot : s->slots) harvest(slot, done);
    for (usize i = 0; i < retired.size();)
        if (retired[i]->last_use <= done)
        {
            free_texture(retired[i]);
            retired.erase(retired.begin() + static_cast<std::ptrdiff_t>(i));
        }
        else
            ++i;
}
void device::harvest(frame_slot &slot, u64 done)
{
    if (!slot.queries || !slot.query_serial || slot.query_serial > done) return;
    u64 values[4]{};
    const VkResult r = vkGetQueryPoolResults(
        native.device, slot.queries, 0, 2, sizeof(values), values, sizeof(u64) * 2,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (r == VK_NOT_READY) return;
    if (!check(r, "vkGetQueryPoolResults") || !values[1] || !values[3]) return;
    const u64 mask = timestamp_bits >= 64 ? UINT64_MAX : (u64(1) << timestamp_bits) - 1;
    stats.completed_gpu_ms += static_cast<f64>((values[2] - values[0]) & mask) *
                              properties.limits.timestampPeriod / 1000000.0;
    ++stats.gpu_samples;
    slot.query_serial = 0;
}
} // namespace vk_detail
#endif
