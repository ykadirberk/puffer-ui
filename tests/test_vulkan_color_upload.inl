// Pin actual uploaded float bytes against the original division, for all channels.
namespace
{
struct color_vertex_probe;
thread_local color_vertex_probe *color_vertices = nullptr;
struct color_vertex_probe
{
    vk_detail::device &device;
    PFN_vkCmdBindVertexBuffers previous;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    static VKAPI_ATTR void VKAPI_CALL observe(VkCommandBuffer command, u32 first, u32 count,
                                              const VkBuffer *buffers, const VkDeviceSize *offsets)
    {
        auto &probe = *color_vertices;
        if (!first && count)
        {
            probe.buffer = buffers[0];
            probe.offset = offsets[0];
        }
        probe.previous(command, first, count, buffers, offsets);
    }
    explicit color_vertex_probe(vk_detail::device &value)
        : device(value), previous(value.vkCmdBindVertexBuffers)
    {
        CHECK(!color_vertices);
        color_vertices = this;
        device.vkCmdBindVertexBuffers = observe;
    }
    ~color_vertex_probe()
    {
        device.vkCmdBindVertexBuffers = previous;
        color_vertices = nullptr;
    }
};
} // namespace

PUI_TEST(native_color_upload_matches_every_original_float_bit_in_direct_staged_and_deferred_draws)
{
    std::vector<vertex> vertices;
    std::vector<i32> indices;
    vertices.reserve(1024);
    indices.reserve(1536);
    for (i32 i = 0; i < 256; ++i)
    {
        const color tint{static_cast<u8>(i), static_cast<u8>(255 - i), static_cast<u8>(i * 73),
                         static_cast<u8>(i * 151)};
        const f32 x = static_cast<f32>(i % 16) * 4, y = static_cast<f32>(i / 16) * 4;
        const i32 base = static_cast<i32>(vertices.size());
        vertices.insert(vertices.end(), {{x, y, 0, 0, tint},
                                         {x + 4, y, 1, 0, tint},
                                         {x + 4, y + 4, 1, 1, tint},
                                         {x, y + 4, 0, 1, tint}});
        for (i32 index : {0, 1, 2, 0, 2, 3}) indices.push_back(base + index);
    }
    for (auto mode : {vulkan_painter_mode::REFERENCE, vulkan_painter_mode::SDF_RASTER,
                      vulkan_painter_mode::COMPUTE_SDF})
        for (bool staged : {false, true})
        {
            program_scene scene(mode, staged);
            if (!scene.win) return;
            scene.surface->make_current(*scene.base);
            scene.base->set_target(scene.surface->scene_target());
            scene.base->set_clip(nullptr);
            color_vertex_probe probe(*scene.native);
            scene.base->draw(nullptr, vertices.data(), static_cast<i32>(vertices.size()),
                             indices.data(), static_cast<i32>(indices.size()));
            CHECK(scene.native->flush_program());
            CHECK(probe.buffer && scene.native->frame);
            const vk_detail::page *storage = nullptr;
            for (const auto &page : scene.native->frame->pages)
                if (probe.buffer == (page.staged ? page.device_buffer : page.buffer))
                    storage = &page;
            CHECK(storage);
            if (!storage) continue;
            const usize stride = mode == vulkan_painter_mode::REFERENCE ? 32 : 48;
            const usize bytes = vertices.size() * stride + indices.size() * sizeof(i32);
            CHECK(probe.offset + bytes <= storage->used);
            if (probe.offset + bytes > storage->used) continue;
            // Volatile prevents reciprocal folding of the independent original
            // arithmetic; equality is bitwise, not a visual tolerance.
            const volatile f32 divisor = 255.0f;
            for (usize i = 0; i < vertices.size(); ++i)
            {
                const u8 *actual = storage->mapped + probe.offset + i * stride;
                const auto &tint = vertices[i].c;
                const f32 expected[] = {tint.r / divisor, tint.g / divisor, tint.b / divisor,
                                        tint.a / divisor};
                CHECK(!std::memcmp(actual, &vertices[i], 16));
                CHECK(!std::memcmp(actual + 16, expected, sizeof(expected)));
            }
            CHECK(!std::memcmp(storage->mapped + probe.offset + vertices.size() * stride,
                               indices.data(), indices.size() * sizeof(i32)));
            scene.surface->present();
            CHECK(vulkan_wait_idle(scene.base));
            CHECK(!vulkan_statistics(scene.base).validation_errors);
        }
}
