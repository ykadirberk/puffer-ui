// Real GPU function-cache oracle: native float colors, exact destination bytes,
// duplicate requests, bounded exhaustion and native state after private draws.
#undef near
#undef far
#undef small
#include <cstdio>
using namespace pui;
namespace
{
constexpr u32 ROWS = 256, WIDTH = 256, PIXELS = ROWS * WIDTH, BYTES = PIXELS * 4;
u32 errors = 0;
PFN_vkCmdBindVertexBuffers original_vertices;
PFN_vkCreateGraphicsPipelines original_pipelines;
VkBuffer vertices;
VkDeviceSize vertex_offset;
vk_detail::native_function_cache *negative_cache = nullptr;
void diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan%d %s\n", code, message);
    ++errors;
}
VKAPI_ATTR void VKAPI_CALL capture_vertices(VkCommandBuffer command, u32 first, u32 count,
                                            const VkBuffer *buffers, const VkDeviceSize *offsets)
{
    if (!first && count)
    {
        vertices = buffers[0];
        vertex_offset = offsets[0];
    }
    original_vertices(command, first, count, buffers, offsets);
}
VKAPI_ATTR VkResult VKAPI_CALL wrong_blend(VkDevice device, VkPipelineCache cache, u32 count,
                                           const VkGraphicsPipelineCreateInfo *infos,
                                           const VkAllocationCallbacks *allocator, VkPipeline *out)
{
    if (negative_cache && count == 1 && infos[0].layout == negative_cache->layout)
    {
        auto info = infos[0];
        auto blend = *info.pColorBlendState;
        auto attachment = blend.pAttachments[0];
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.pAttachments = &attachment;
        info.pColorBlendState = &blend;
        return original_pipelines(device, cache, 1, &info, allocator, out);
    }
    return original_pipelines(device, cache, count, infos, allocator, out);
}
bool invalidate(vk_detail::device &d, vk_detail::page &p)
{
    if (p.coherent) return true;
    VkMappedMemoryRange m{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    m.memory = p.memory;
    m.size = VK_WHOLE_SIZE;
    return d.check(d.vkInvalidateMappedMemoryRanges(d.native.device, 1, &m), "function oracle map");
}
struct probe
{
    vk_detail::device &d;
    vk_detail::page input, output, control;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    explicit probe(vk_detail::device &owner) : d(owner) {}
    ~probe()
    {
        vulkan_wait_idle(&d);
        if (pipeline) d.vkDestroyPipeline(d.native.device, pipeline, nullptr);
        if (layout) d.vkDestroyPipelineLayout(d.native.device, layout, nullptr);
        if (pool) d.vkDestroyDescriptorPool(d.native.device, pool, nullptr);
        if (set_layout) d.vkDestroyDescriptorSetLayout(d.native.device, set_layout, nullptr);
        d.free_page(control);
        d.free_page(output);
        d.free_page(input);
    }
    bool initialize(vk_detail::native_function_cache &cache, const std::vector<u32> &sources)
    {
        if (!d.allocate_buffer(input, sources.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
            !d.allocate_buffer(output, BYTES * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
            !d.allocate_buffer(control, 128, VK_BUFFER_USAGE_TRANSFER_DST_BIT))
            return false;
        std::memcpy(input.mapped, sources.data(), sources.size() * 4);
        input.used = sources.size() * 4;
        if (!d.flush_page(input)) return false;
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dc.bindingCount = 2;
        dc.pBindings = bindings;
        if (!d.check(d.vkCreateDescriptorSetLayout(d.native.device, &dc, nullptr, &set_layout),
                     "function oracle descriptors"))
            return false;
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets = 1;
        pc.poolSizeCount = 1;
        pc.pPoolSizes = &size;
        if (!d.check(d.vkCreateDescriptorPool(d.native.device, &pc, nullptr, &pool),
                     "function oracle pool"))
            return false;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &set_layout;
        if (!d.check(d.vkAllocateDescriptorSets(d.native.device, &ai, &set), "function oracle set"))
            return false;
        const VkDescriptorBufferInfo buffers[] = {{input.buffer, 0, input.capacity},
                                                  {output.buffer, 0, output.capacity}};
        VkWriteDescriptorSet writes[2]{};
        for (u32 i = 0; i < 2; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &buffers[i];
        }
        d.vkUpdateDescriptorSets(d.native.device, 2, writes, 0, nullptr);
        const VkDescriptorSetLayout layouts[] = {cache.descriptor_layout, set_layout};
        VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lc.setLayoutCount = 2;
        lc.pSetLayouts = layouts;
        if (!d.check(d.vkCreatePipelineLayout(d.native.device, &lc, nullptr, &layout),
                     "function oracle layout"))
            return false;
        pipeline = vk_detail::compute_pipeline(d, layout, vulkan_function_probe,
                                               sizeof(vulkan_function_probe));
        return pipeline != VK_NULL_HANDLE;
    }
    bool run(vk_detail::native_function_cache &cache)
    {
        if (!d.commands()) return false;
        d.end_rendering();
        const VkDescriptorSet sets[] = {cache.descriptor, set};
        d.vkCmdBindPipeline(d.frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        d.vkCmdBindDescriptorSets(d.frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2,
                                  sets, 0, nullptr);
        d.vkCmdDispatch(d.frame->command, WIDTH / 64, ROWS, 1);
        return d.result == VK_SUCCESS;
    }
    bool read(vk_detail::native_function_cache &cache)
    {
        cache.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                      VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        const VkBufferCopy copy{0, 0, 128};
        d.vkCmdCopyBuffer(d.frame->command, cache.storage.buffers[0].buffer, control.buffer, 1,
                          &copy);
        cache.barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        const u64 serial = d.submit();
        return serial && d.wait(serial) && invalidate(d, output) && invalidate(d, control);
    }
};
} // namespace
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line%d %s\n", __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    bool staged = false, negative = false;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--negative-control"))
            negative = true;
        else
            return 1;
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    auto *base = create_vulkan_device(options);
    REQUIRE(base);
    auto &d = *static_cast<vk_detail::device *>(base);
    std::vector<u32> sources(ROWS * 4);
    std::vector<u8> destination(BYTES), expected(BYTES);
    for (u32 r = 0; r < ROWS; ++r)
    {
        f32 color[] = {f32((r * 73) % 257) / 256.0f, f32((r * 101) % 257) / 256.0f,
                       f32((r * 157) % 257) / 256.0f, f32(r) / 255.0f};
        const f32 special[] = {0, 1, 0.1f, 0.1234567f, 0.5f, 1e-6f, 1.0f - 1e-6f, 1e-38f};
        if (r % 12 < 8) color[3] = special[r % 12];
        std::memcpy(sources.data() + r * 4, color, 16);
        for (u32 x = 0; x < WIDTH; ++x)
        {
            const auto at = (r * WIDTH + x) * 4;
            destination[at] = static_cast<u8>(x);
            destination[at + 1] = static_cast<u8>(255 - x);
            destination[at + 2] = static_cast<u8>(x * 37 + 13);
            destination[at + 3] = static_cast<u8>(x ^ 128);
        }
    }
    auto reference = d.create_target(WIDTH, ROWS);
    REQUIRE(reference);
    original_vertices = d.vkCmdBindVertexBuffers;
    d.vkCmdBindVertexBuffers = capture_vertices;
    d.update_texture(reference, 0, 0, WIDTH, ROWS, destination.data());
    d.set_target(reference);
    d.set_clip(nullptr);
    constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
    for (u32 r = 0; r < ROWS; ++r)
    {
        const color white{255, 255, 255, 255};
        const vertex quad[] = {{0, f32(r), 0, 0, white},
                               {WIDTH, f32(r), 1, 0, white},
                               {WIDTH, f32(r + 1), 1, 1, white},
                               {0, f32(r + 1), 0, 1, white}};
        d.paint(d.white, quad, 4, indices, 6, true);
        vk_detail::page *found = nullptr;
        for (auto &page : d.frame->pages)
            if (page.buffer == vertices || page.device_buffer == vertices) found = &page;
        REQUIRE(found);
        for (u32 corner = 0; corner < 4; ++corner)
            std::memcpy(found->mapped + vertex_offset + corner * 32 + 16, sources.data() + r * 4,
                        16);
    }
    d.vkCmdBindVertexBuffers = original_vertices;
    REQUIRE(d.read_pixels(reference, 0, 0, WIDTH, ROWS, expected.data()));
    const auto old_metadata = d.stats.gpu_metadata_allocated_bytes,
               old_images = d.stats.image_bytes;
    {
        vk_detail::native_function_cache rejected;
        REQUIRE(!rejected.initialize(d, 7));
        d.paint_depth = 1;
        REQUIRE(!rejected.initialize(d));
        d.paint_depth = 0;
        REQUIRE(!rejected.initialize(d, 4096, 256, 1));
        rejected.release();
        REQUIRE(d.result == VK_SUCCESS && d.stats.gpu_metadata_allocated_bytes == old_metadata &&
                d.stats.image_bytes == old_images);
    }
    u64 compared = 0, differences = 0, exhausted_hits = 0, overflow_requests = 0;
    {
        vk_detail::native_function_cache cache;
        if (negative)
        {
            negative_cache = &cache;
            original_pipelines = d.vkCreateGraphicsPipelines;
            d.vkCreateGraphicsPipelines = wrong_blend;
        }
        REQUIRE(cache.initialize(d));
        if (negative)
        {
            d.vkCreateGraphicsPipelines = original_pipelines;
            negative_cache = nullptr;
        }
        REQUIRE(!cache.storage.buffers[0].mapped &&
                cache.atlas->allocation <= options.cache_budget_bytes);
        probe oracle(d);
        REQUIRE(oracle.initialize(cache, sources));
        REQUIRE(cache.record_begin() && oracle.run(cache) && oracle.read(cache));
        const auto *values = reinterpret_cast<const u32 *>(oracle.output.mapped);
        for (u32 pixel = 0; pixel < PIXELS; ++pixel)
            REQUIRE(!values[PIXELS + pixel] && values[pixel] == 0xdeadc0deu);
        const auto allocations = d.stats.draw_memory_allocations;
        const auto resources = d.stats.draw_resource_creations;
        const auto metadata = d.stats.gpu_metadata_allocations;
        d.paint_depth = 1;
        for (u32 cycle = 0; cycle < 8; ++cycle)
            REQUIRE(cache.record_begin() && oracle.run(cache) && cache.record_prepare());
        REQUIRE(cache.record_begin() && oracle.run(cache));
        d.paint_depth = 0;
        REQUIRE(oracle.read(cache));
        REQUIRE(d.stats.draw_memory_allocations == allocations &&
                d.stats.draw_resource_creations == resources &&
                d.stats.gpu_metadata_allocations == metadata);
        const auto *control = reinterpret_cast<const u32 *>(oracle.control.mapped);
        REQUIRE(control[0] == 0 && control[5] == 0 && control[17] == 0 && control[21] == 0);
        for (u32 pixel = 0; pixel < PIXELS; ++pixel)
        {
            REQUIRE(values[PIXELS + pixel] == 1);
            for (u32 c = 0; c < 4; ++c)
            {
                differences += ((values[pixel] >> (c * 8)) & 255) != expected[pixel * 4 + c];
                ++compared;
            }
        }
        REQUIRE(negative ? differences > 0 : differences == 0);
        // GPU epoch wrap must preserve exact keys and ready rows.
        REQUIRE(d.commands());
        d.vkCmdFillBuffer(d.frame->command, cache.storage.buffers[0].buffer, 4 * 4, 4, UINT32_MAX);
        REQUIRE(cache.record_begin() && oracle.run(cache) && oracle.read(cache));
        control = reinterpret_cast<const u32 *>(oracle.control.mapped);
        REQUIRE(control[4] == 0 && control[0] == 0);
        for (u32 pixel = 0; pixel < PIXELS; ++pixel) REQUIRE(values[PIXELS + pixel] == 1);
        // A tiny request list admits one row and safely rejects all excess.
        vk_detail::native_function_cache overflow;
        REQUIRE(overflow.initialize(d, 4096, 1));
        REQUIRE(overflow.record_begin() && oracle.run(overflow) && overflow.record_prepare() &&
                oracle.read(overflow));
        control = reinterpret_cast<const u32 *>(oracle.control.mapped);
        REQUIRE(control[0] == 1 && control[1] > 0 && control[5] == 1 && control[17] == 1 &&
                control[21] == 1);
        overflow_requests = control[1];
        overflow.release();
        // All 256 keys compete in a single eight-way set: no wrong-key alias.
        vk_detail::native_function_cache exhausted;
        REQUIRE(exhausted.initialize(d, 8, 256));
        REQUIRE(exhausted.record_begin() && oracle.run(exhausted) && exhausted.record_prepare());
        REQUIRE(exhausted.record_begin() && oracle.run(exhausted) && oracle.read(exhausted));
        for (u32 pixel = 0; pixel < PIXELS; ++pixel)
            if (values[PIXELS + pixel])
            {
                ++exhausted_hits;
                for (u32 c = 0; c < 4; ++c)
                    REQUIRE(((values[pixel] >> (c * 8)) & 255) == expected[pixel * 4 + c]);
            }
            else
                REQUIRE(values[pixel] == 0xdeadc0deu);
        if (!exhausted_hits || exhausted_hits > 8 * WIDTH)
            std::fprintf(stderr, "exhausted_hits=%llu request_count=%u calibration_count=%u\n",
                         exhausted_hits, reinterpret_cast<const u32 *>(oracle.control.mapped)[0],
                         reinterpret_cast<const u32 *>(oracle.control.mapped)[5]);
        REQUIRE(exhausted_hits > 0 && exhausted_hits <= 8 * WIDTH);
        // Release a freshly recorded, unsubmitted calibration with its owner alive.
        REQUIRE(exhausted.record_prepare());
        exhausted.release();
    }
    REQUIRE(d.stats.gpu_metadata_allocated_bytes == old_metadata);
    d.set_target(reference);
    d.set_clip(nullptr);
    d.clear({17, 21, 31, 255});
    const color final_color{22, 33, 44, 255};
    const vertex final_quad[] = {{0, 0, 0, 0, final_color},
                                 {WIDTH, 0, 1, 0, final_color},
                                 {WIDTH, ROWS, 1, 1, final_color},
                                 {0, ROWS, 0, 1, final_color}};
    d.paint(d.white, final_quad, 4, indices, 6, true);
    REQUIRE(d.read_pixels(reference, 0, 0, WIDTH, ROWS, destination.data()));
    for (u32 pixel = 0; pixel < PIXELS; ++pixel)
        REQUIRE(!std::memcmp(destination.data() + pixel * 4, &final_color, 4));
    d.destroy_target(reference);
    REQUIRE(vulkan_wait_idle(base));
    destroy_vulkan_device(base);
    std::fprintf(
        stderr,
        "function_cache compared_bytes=%llu differences=%llu negative=%u exhausted_hit_pixels=%llu "
        "overflow_requests=%llu draw_allocations=0 validation_errors=%u\n",
        compared, differences, negative, exhausted_hits, overflow_requests, errors);
    return errors ? 1 : 0;
}
