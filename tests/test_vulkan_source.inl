#include "vulkan_test_options.h"
// Native fragment vs compute source-float oracle. The production SDF module is
// unchanged; only this test's F32 attachment disables blending to expose sources.
#undef near
#undef far
#undef small
#include <cstdio>
using namespace pui;

namespace
{
constexpr u32 SIDE = 64, PIXELS = SIDE * SIDE, BYTES = PIXELS * 16;
PFN_vkCreateGraphicsPipelines original_pipelines;
u32 errors = 0;
void diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan diagnostic%d %s\n", code, message);
    ++errors;
}
VKAPI_ATTR VkResult VKAPI_CALL probe_pipelines(VkDevice device, VkPipelineCache cache, u32 count,
                                               const VkGraphicsPipelineCreateInfo *infos,
                                               const VkAllocationCallbacks *allocator,
                                               VkPipeline *out)
{
    if (count == 1)
    {
        auto *next = static_cast<const VkBaseInStructure *>(infos[0].pNext);
        while (next && next->sType != VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO)
            next = next->pNext;
        const auto *render = reinterpret_cast<const VkPipelineRenderingCreateInfo *>(next);
        if (render && render->colorAttachmentCount == 1 &&
            render->pColorAttachmentFormats[0] == VK_FORMAT_R32G32B32A32_SFLOAT)
        {
            auto info = infos[0];
            auto blend = *info.pColorBlendState;
            auto attachment = blend.pAttachments[0];
            attachment.blendEnable = VK_FALSE;
            blend.pAttachments = &attachment;
            info.pColorBlendState = &blend;
            return original_pipelines(device, cache, 1, &info, allocator, out);
        }
    }
    return original_pipelines(device, cache, count, infos, allocator, out);
}
struct fixture
{
    vk_detail::sdf_shape shape;
    color tint;
    const char *kind;
};
void barrier(vk_detail::device &d, VkBuffer buffer, VkPipelineStageFlags2 stage,
             VkAccessFlags2 access)
{
    VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    b.srcStageMask = stage;
    b.srcAccessMask = access;
    b.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    b.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = buffer;
    b.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &b;
    d.vkCmdPipelineBarrier2(d.frame->command, &dependency);
}
bool invalidate(vk_detail::device &d, vk_detail::page &page)
{
    if (page.coherent) return true;
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = page.memory;
    range.size = VK_WHOLE_SIZE;
    return d.vkInvalidateMappedMemoryRanges(d.native.device, 1, &range) == VK_SUCCESS;
}
} // namespace

#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line%d: %s\n", __LINE__, #x);                               \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    bool staged = false, negative = false, byte_tints = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--staged") == 0)
            staged = true;
        else if (std::strcmp(argv[i], "--negative-control") == 0)
            negative = true;
        else if (std::strcmp(argv[i], "--byte-tints") == 0)
            byte_tints = true;
        else
            return 1;
    }
    vulkan_device_opts options = test_detail::reference_options();
    options.painter = vulkan_painter_mode::SDF_RASTER;
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    options.vector_text = true;
#endif
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    options.validation = true;
    options.error_callback = diagnostic;
    auto *base = create_vulkan_device(options);
    REQUIRE(base);
    auto &d = *static_cast<vk_detail::device *>(base);
    VkFormatProperties format{};
    d.vkGetPhysicalDeviceFormatProperties(d.native.physical_device, VK_FORMAT_R32G32B32A32_SFLOAT,
                                          &format);
    constexpr auto needed =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    if ((format.optimalTilingFeatures & needed) != needed)
    {
        destroy_vulkan_device(base);
        std::puts("native source oracle unavailable: F32 target format; 0 comparisons");
        return errors ? 1 : 0;
    }
    auto *surface = static_cast<vk_detail::surface *>(
        create_vulkan_surface(base, {.width = SIDE, .height = SIDE}));
    REQUIRE(surface);
    auto *ctx = create_context(base, surface);
    REQUIRE(ctx);
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    const auto font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    REQUIRE(font != FONT_INVALID);
#endif
    std::vector<fixture> cases;
    for (u32 i = 0; i < 256; ++i)
    {
        fixture f{};
        f.kind = "rounded";
        f.shape.bounds = {3.25f + (i % 4) * 0.1875f, 5.75f, 44.5f, 37.25f};
        f.shape.radii = {3.25f, 7.0f, 12.0f, 1.75f};
        f.shape.feather = 0.5f + (i % 5) * 0.375f;
        f.tint = {static_cast<u8>(i), static_cast<u8>(i * 37), static_cast<u8>(i * 101),
                  static_cast<u8>(i * 53)};
        cases.push_back(f);
    }
    for (u32 i = 0; i < 36; ++i)
    {
        fixture f{};
        f.kind = "gradient";
        f.tint = {127, 93, 211, 173};
        f.shape.bounds = {2.125f + (i % 6) * 0.125f, 4.375f, 49.75f, 41.125f};
        f.shape.radii = {7, 3, 11, 5};
        f.shape.feather = 0.75f;
        f.shape.reserved[0] = 2;
        f.shape.corners[0] = 0x174fb3d1u;
        f.shape.corners[1] = 0xe5913478u;
        f.shape.corners[2] = 0x9d50c31bu;
        f.shape.corners[3] = 0x735b9ae7u;
        cases.push_back(f);
    }
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    auto *native_font = detail::harfbuzz_font(ctx, font);
    REQUIRE(native_font);
    const float em = static_cast<float>(hb_face_get_upem(hb_font_get_face(native_font)));
    for (u32 cp : {65u, 103u, 81u, 0x03a9u, 0x015fu})
    {
        hb_codepoint_t glyph = 0;
        REQUIRE(hb_font_get_nominal_glyph(native_font, cp, &glyph));
        auto *outline = detail::harfbuzz_outline(ctx, font, glyph);
        REQUIRE(outline && !outline->data.empty());
        const auto location = static_cast<u32>(d.slug_data.size() / 8);
        d.slug_data.insert(d.slug_data.end(), outline->data.begin(), outline->data.end());
        while (d.slug_data.size() % 8) d.slug_data.push_back(0);
        for (float dpi : {1.0f, 1.25f, 2.0f})
            for (float gamma : {0.1f, 1.0f, 1.8f})
                for (u32 phase = 0; phase < 6; ++phase)
                {
                    fixture f{};
                    f.kind = "glyph";
                    f.tint = {163, 117, 211, 193};
                    f.shape.bounds = {0, 0, SIDE, SIDE};
                    f.shape.radii = {7.25f + phase * 0.125f, 44.75f - phase * 0.0625f,
                                     24.0f / em * dpi, 0};
                    std::memcpy(&f.shape.radii.bl, &location, 4);
                    f.shape.reserved[0] = 1;
                    f.shape.reserved[1] = gamma;
                    cases.push_back(f);
                }
    }
#endif
    original_pipelines = d.vkCreateGraphicsPipelines;
    d.vkCreateGraphicsPipelines = probe_pipelines;
    auto *target = d.make_texture({.width = SIDE,
                                   .height = SIDE,
                                   .render_target = true,
                                   .format = VK_FORMAT_R32G32B32A32_SFLOAT});
    REQUIRE(target);
    vk_detail::page source, output, readback;
    const auto alignment =
        std::max<VkDeviceSize>(256, d.properties.limits.minStorageBufferOffsetAlignment);
    const VkDeviceSize glyph_at = alignment;
    const VkDeviceSize glyph_bytes = std::max<usize>(8, d.slug_data.size());
    REQUIRE(d.allocate_buffer(source, glyph_at + glyph_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    REQUIRE(d.allocate_buffer(output, BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    REQUIRE(d.allocate_buffer(readback, BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    std::memset(source.mapped, 0, static_cast<usize>(glyph_at + glyph_bytes));
    std::memcpy(source.mapped + glyph_at, d.slug_data.data(), d.slug_data.size());
    source.used = glyph_at + glyph_bytes;
    struct parameters
    {
        float tint[4];
        u32 width = SIDE, height = SIDE;
        u32 alpha_flip = 0;
        u32 packed_tint = 0, use_byte_tints = 0;
    } p{};
    p.alpha_flip = negative ? 1u : 0u;
    p.use_byte_tints = byte_tints ? 1u : 0u;
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(p)};
    const VkDescriptorSetLayout layouts[] = {d.descriptor_layout, d.sdf_layout};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 2;
    layout_info.pSetLayouts = layouts;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &range;
    VkPipelineLayout layout;
    REQUIRE(d.vkCreatePipelineLayout(d.native.device, &layout_info, nullptr, &layout) ==
            VK_SUCCESS);
    const auto pipeline =
        vk_detail::compute_pipeline(d, layout, vulkan_source_probe, sizeof(vulkan_source_probe));
    REQUIRE(pipeline);
    REQUIRE(vulkan_wait_idle(base));
    u64 differences = 0, compared = 0, case_count = 0;
    u64 channel_differences[4]{};
    for (const auto &f : cases)
    {
        surface->make_current(d);
        REQUIRE(d.result == VK_SUCCESS);
        d.set_target(target);
        d.set_clip(nullptr);
        d.sdf_shapes.clear();
        d.sdf_spans.clear();
        d.sdf_shapes.push_back(f.shape);
        d.sdf_spans.push_back({0, 0});
        const vertex vertices[] = {{0, 0, 0, 0, f.tint},
                                   {SIDE, 0, 1, 0, f.tint},
                                   {SIDE, SIDE, 1, 1, f.tint},
                                   {0, SIDE, 0, 1, f.tint}};
        constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
        d.paint(d.white, vertices, 4, indices, 6, true);
        REQUIRE(d.result == VK_SUCCESS);
        d.end_rendering();
        d.transition(target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {SIDE, SIDE, 1};
        d.vkCmdCopyImageToBuffer(d.frame->command, target->image, target->layout, readback.buffer,
                                 1, &copy);
        barrier(d, readback.buffer, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);
        std::memcpy(source.mapped, &f.shape, sizeof(f.shape));
        REQUIRE(d.flush_page(source));
        const VkDescriptorBufferInfo buffers[] = {{source.buffer, 0, sizeof(f.shape)},
                                                  {source.buffer, glyph_at, glyph_bytes},
                                                  {output.buffer, 0, BYTES},
                                                  {source.buffer, 0, 4}};
        const auto set = d.sdf_descriptor(buffers);
        REQUIRE(set);
        p.tint[0] = f.tint.r / 255.0f;
        p.tint[1] = f.tint.g / 255.0f;
        p.tint[2] = f.tint.b / 255.0f;
        p.tint[3] = f.tint.a / 255.0f;
        std::memcpy(&p.packed_tint, &f.tint, 4);
        d.vkCmdBindPipeline(d.frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        d.vkCmdBindDescriptorSets(d.frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1,
                                  &set, 0, nullptr);
        d.vkCmdPushConstants(d.frame->command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(p),
                             &p);
        d.vkCmdDispatch(d.frame->command, SIDE / 8, SIDE / 8, 1);
        barrier(d, output.buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT);
        const auto serial = d.submit();
        REQUIRE(serial && d.wait(serial));
        REQUIRE(invalidate(d, readback) && invalidate(d, output));
        const auto *a = reinterpret_cast<const u32 *>(readback.mapped);
        const auto *b = reinterpret_cast<const u32 *>(output.mapped);
        u64 changed = 0;
        for (u32 i = 0; i < PIXELS * 4; ++i)
            if (a[i] != b[i])
            {
                ++changed;
                ++channel_differences[i % 4];
                if (!negative && changed < 3)
                    std::fprintf(stderr,
                                 "difference case=%llu kind=%s pixel=%u channel=%u native=%08x "
                                 "compute=%08x\n",
                                 static_cast<unsigned long long>(case_count), f.kind, i / 4, i % 4,
                                 a[i], b[i]);
            }
        differences += changed;
        compared += PIXELS * 4;
        ++case_count;
    }
    REQUIRE(vulkan_wait_idle(base));
    d.vkDestroyPipeline(d.native.device, pipeline, nullptr);
    d.vkDestroyPipelineLayout(d.native.device, layout, nullptr);
    d.free_page(readback);
    d.free_page(output);
    d.free_page(source);
    d.destroy_texture(target);
    destroy_context(ctx);
    d.destroy_surface(surface);
    destroy_vulkan_device(base);
    std::fprintf(stderr,
                 "cases=%llu compared_float_components=%llu differences=%llu "
                 "channels=%llu/%llu/%llu/%llu validation_errors=%u\n",
                 static_cast<unsigned long long>(case_count),
                 static_cast<unsigned long long>(compared),
                 static_cast<unsigned long long>(differences),
                 static_cast<unsigned long long>(channel_differences[0]),
                 static_cast<unsigned long long>(channel_differences[1]),
                 static_cast<unsigned long long>(channel_differences[2]),
                 static_cast<unsigned long long>(channel_differences[3]), errors);
    if (negative)
    {
        REQUIRE(!channel_differences[0] && !channel_differences[1] && !channel_differences[2] &&
                channel_differences[3] == case_count * PIXELS && !errors);
        std::puts("native source oracle rejected every deliberate alpha bit flip");
        return 0;
    }
    REQUIRE(!differences && !errors);
    std::puts("all native/compute source-float components matched exactly");
    return 0;
}
