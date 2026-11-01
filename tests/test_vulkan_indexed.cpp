// Independent scalar split oracle and actual indexed-upload witnesses.
// Existing test bodies, primitive order and golden references stay unchanged.
#define main indexed_blur_fixture_main
#include "test_vulkan_blur.cpp"
#undef main

namespace indexed_test
{
struct span
{
    i32 end, low, high;
};
// Frozen pre-optimization greedy rule, independent of the block planner.
span scalar_span(const i32 *indices, i32 start, i32 count, VkDeviceSize stride,
                 VkDeviceSize capacity)
{
    i32 end = start, low = 0x7fffffff, high = 0;
    while (end < count)
    {
        const i32 lo =
            std::min(low, std::min(indices[end], std::min(indices[end + 1], indices[end + 2])));
        const i32 hi =
            std::max(high, std::max(indices[end], std::max(indices[end + 1], indices[end + 2])));
        if ((static_cast<VkDeviceSize>(hi) - lo + 1) * stride +
                static_cast<VkDeviceSize>(end - start + 3) * 4 >
            capacity)
            break;
        low = lo;
        high = hi;
        end += 3;
    }
    return {end, low, high};
}
bool planner()
{
    u32 random = 0x51a7e29;
    for (const i32 triangles : {1, 7, 8, 9, 15, 16, 17, 65, 1024})
        for (i32 pattern = 0; pattern < 7; ++pattern)
        {
            std::vector<i32> indices(static_cast<usize>(triangles) * 3);
            for (i32 i = 0; i < triangles * 3; ++i)
            {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                switch (pattern)
                {
                case 0:
                    indices[i] = 407 + i;
                    break;
                case 1:
                    indices[i] = 407 + (i % 3 ? i / 3 + i % 3 : 0);
                    break;
                case 2:
                    indices[i] = 407 + triangles * 3 - i;
                    break;
                case 3:
                    indices[i] = static_cast<i32>(random % 90000);
                    break;
                case 4:
                    indices[i] = i % 3 == 1 ? 0x7fffffff : i % 13;
                    break;
                case 5:
                    indices[i] = 0;
                    break;
                default:
                    indices[i] = -4096 + i % 127;
                    break;
                }
            }
            for (const VkDeviceSize stride : {20, 32})
                for (const VkDeviceSize capacity :
                     {VkDeviceSize(0), VkDeviceSize(3 * (stride + 4) - 1),
                      VkDeviceSize(3 * (stride + 4)), VkDeviceSize(3 * (stride + 4) + 1),
                      VkDeviceSize(511), VkDeviceSize(512), VkDeviceSize(513), VkDeviceSize(4096),
                      VkDeviceSize(1024 * 1024), VkDeviceSize(1) << 38})
                {
                    for (i32 start = 0; start < triangles * 3;)
                    {
                        const auto expected =
                            scalar_span(indices.data(), start, triangles * 3, stride, capacity);
                        const auto actual = vk_detail::plan_geometry_span(
                            indices.data(), start, triangles * 3, stride, capacity);
                        CHECK(actual.end == expected.end && actual.low == expected.low &&
                              actual.high == expected.high);
                        start = actual.end == start ? start + 3 : actual.end;
                    }
                }
        }
    // Count serial capacity decisions rather than asserting a noisy CPU time.
    // A large dense stream must not regress to one scalar check per triangle.
    std::vector<i32> dense(98304);
    for (usize i = 0; i < dense.size(); ++i) dense[i] = static_cast<i32>(i) + 407;
    vk_detail::span_work = {};
    const auto result =
        vk_detail::plan_geometry_span(dense.data(), 0, static_cast<i32>(dense.size()), 20, 1 << 28);
    CHECK(result.end == static_cast<i32>(dense.size()));
    CHECK(vk_detail::span_work.triangles * 2 < dense.size() / 3);
    dense[0] = 0;
    dense[1] = 0x7fffffff;
    dense[2] = 1;
    vk_detail::span_work = {};
    const auto sparse =
        vk_detail::plan_geometry_span(dense.data(), 0, static_cast<i32>(dense.size()), 20, 512);
    CHECK(sparse.end == 0 && vk_detail::span_work.blocks == 0 &&
          vk_detail::span_work.triangles == 1);
    return true;
}
bool offsets()
{
    struct sample
    {
        i32 base, count, expected;
    };
    for (const sample s :
         {sample{0, 12, 0}, sample{407, 12, -407}, sample{0x00fffff4, 12, -0x00fffff4},
          sample{0x00fffff4, 13, 0}, sample{0x01000000, 12, 0}, sample{-407, 12, 0},
          sample{std::numeric_limits<i32>::min(), 12, 0},
          sample{std::numeric_limits<i32>::max() - 11, 12, 0},
          sample{std::numeric_limits<i32>::max(), 12, 0}, sample{407, 0, 0}})
        CHECK(vk_detail::geometry_vertex_offset(s.base, s.count) == s.expected);
    return true;
}
struct frame_scope
{
    context *ctx;
    frame_scope(view &v, i32 frame) : ctx(v.ctx)
    {
        begin_frame(ctx, *v.win, frame / 60.0, 1.0 / 60.0);
    }
    ~frame_scope() { end_frame(ctx); }
};
struct upload_probe;
thread_local upload_probe *active = nullptr;
struct upload_probe
{
    vk_detail::device &dev;
    PFN_vkCmdBindIndexBuffer bind;
    PFN_vkCmdDrawIndexed draw;
    const i32 *indices;
    i32 count, base, expected_offset;
    VkBuffer bound = VK_NULL_HANDLE;
    VkDeviceSize at = 0;
    u32 calls = 0;
    bool correct = true;
    static VKAPI_ATTR void VKAPI_CALL bind_call(VkCommandBuffer command, VkBuffer buffer,
                                                VkDeviceSize offset, VkIndexType type)
    {
        auto &p = *active;
        p.bound = buffer;
        p.at = offset;
        p.correct = p.correct && type == VK_INDEX_TYPE_UINT32;
        p.bind(command, buffer, offset, type);
    }
    static VKAPI_ATTR void VKAPI_CALL draw_call(VkCommandBuffer command, u32 count, u32 instances,
                                                u32 first, i32 offset, u32 instance)
    {
        auto &p = *active;
        ++p.calls;
        p.correct = p.correct && count == static_cast<u32>(p.count) && instances == 1 &&
                    first == 0 && instance == 0 && offset == p.expected_offset;
        bool found = false;
        for (const auto &page : p.dev.frame->pages)
            if (p.bound == (page.staged ? page.device_buffer : page.buffer) && p.at <= page.used &&
                static_cast<u64>(p.count) * 4 <= page.used - p.at)
            {
                found = true;
                for (i32 i = 0; i < p.count; ++i)
                {
                    i32 value = 0;
                    std::memcpy(&value, page.mapped + p.at + static_cast<usize>(i) * 4, 4);
                    const i32 expected =
                        p.expected_offset || !p.base
                            ? p.indices[i]
                            : static_cast<i32>(static_cast<i64>(p.indices[i]) - p.base);
                    p.correct = p.correct && value == expected &&
                                static_cast<i64>(static_cast<u32>(value)) + offset ==
                                    static_cast<i64>(p.indices[i]) - p.base;
                }
                break;
            }
        p.correct = p.correct && found;
        p.draw(command, count, instances, first, offset, instance);
    }
    upload_probe(vk_detail::device &d, const i32 *source, i32 size, i32 index_base, i32 offset)
        : dev(d), bind(d.vkCmdBindIndexBuffer), draw(d.vkCmdDrawIndexed), indices(source),
          count(size), base(index_base), expected_offset(offset)
    {
        active = this;
        d.vkCmdBindIndexBuffer = bind_call;
        d.vkCmdDrawIndexed = draw_call;
    }
    ~upload_probe()
    {
        dev.vkCmdBindIndexBuffer = bind;
        dev.vkCmdDrawIndexed = draw;
        active = nullptr;
    }
};
bool pixels(vk_detail::device &d)
{
    view v;
    CHECK(v.initialize(&d, 64, 64));
    std::vector<u8> pixels[2];
    for (auto &p : pixels) p.resize(64 * 64 * 4);
    vertex vertices[12];
    for (i32 i = 0; i < 12; ++i)
    {
        const f32 x = 4.25f + i / 4 * 7, y = 3.375f + i / 4 * 9;
        vertices[i] = {x + (i % 4 == 1 || i % 4 == 2 ? 31 : 0),
                       y + (i % 4 >= 2 ? 29 : 0),
                       0,
                       0,
                       {static_cast<u8>(i * 17), static_cast<u8>(i * 29), static_cast<u8>(i * 41),
                        static_cast<u8>(51 + i * 13)}};
    }
    const i32 local[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11};
    for (const i32 base : {0, 407, 0x00fffff4, 0x00fffff5, 0x01000000, -407,
                           std::numeric_limits<i32>::min(), std::numeric_limits<i32>::max() - 11})
    {
        i32 indices[18];
        for (i32 i = 0; i < 18; ++i) indices[i] = base + local[i];
        const i32 expected = base > 0 && base <= 0x00fffff4 ? -base : 0;
        for (i32 variant = 0; variant < 2; ++variant)
        {
            {
                frame_scope frame(v, variant);
                const auto before = vulkan_statistics(&d);
                if (!variant)
                {
                    upload_probe probe(d, indices, 18, base, expected);
                    d.paint(d.white, vertices, 12, indices, 18, true, base);
                    CHECK(probe.calls == 1 && probe.correct);
                }
                else
                    d.draw(nullptr, vertices, 12, local, 18);
                CHECK(d.stats.draw_buffer_allocations == before.draw_buffer_allocations);
                CHECK(d.stats.upload_bytes - before.upload_bytes ==
                      12 * d.vertex_stride() + 18 * sizeof(i32));
            }
            CHECK(d.read_pixels(v.surface->scene_target(), 0, 0, 64, 64, pixels[variant].data()));
        }
        CHECK(pixels[0] == pixels[1]);
    }
    return true;
}
} // namespace indexed_test

int main(int argc, char **argv)
{
    bool staged = false, cpu_only = false;
    for (i32 i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--cpu"))
            cpu_only = true;
        else
            return 2;
    if (!indexed_test::planner() || !indexed_test::offsets()) return 1;
    if (cpu_only) return 0;
    for (const bool compact : {false, true})
        for (const u32 slots : {1u, 2u})
        {
            vulkan_device_opts options;
            options.validation = true;
            options.error_callback = diagnostic;
            options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
            options.compact_vertices = compact;
            options.frames_in_flight = slots;
            auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
            if (!d) return 1;
            if (!compact && slots == 1)
                std::fprintf(stderr, "indexed device=%s vendor=%u device=%u driver=%u\n",
                             d->properties.deviceName, d->properties.vendorID,
                             d->properties.deviceID, d->properties.driverVersion);
            const bool result = indexed_test::pixels(*d) && vulkan_wait_idle(d) &&
                                !vulkan_statistics(d).validation_errors;
            destroy_vulkan_device(d);
            if (!result || errors) return 1;
        }
    std::puts("indexed Vulkan: exact scalar splits, bounded work, native offsets and fallback "
              "pixels passed");
    return 0;
}
