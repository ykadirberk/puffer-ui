// Real storage-image compute painting must match the established graphics path.
#undef near
#undef far
#undef small
#include "test_util.h"

namespace
{
vk_detail::device *compute_device = nullptr;
vk_detail::rgba8_blend_lut *compute_blend = nullptr;
i32 compute_errors = 0;
void compute_diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, message);
    ++compute_errors;
}
color rgba(u32 c)
{
    return {static_cast<u8>(c), static_cast<u8>(c >> 8), static_cast<u8>(c >> 16),
            static_cast<u8>(c >> 24)};
}
paint_detail::command rectangle(i32 x, i32 y, i32 w, i32 h, u32 c)
{
    paint_detail::command result;
    result.operation = static_cast<u32>(paint_detail::opcode::RECT_COMPAT);
    result.flags = c;
    result.x0 = static_cast<f32>(x);
    result.y0 = static_cast<f32>(y);
    result.x1 = static_cast<f32>(x + w);
    result.y1 = static_cast<f32>(y + h);
    return result;
}
void native_draw(vk_detail::device &d, const paint_detail::command_tape &tape,
                 texture_handle target)
{
    d.set_target(target);
    for (const auto &c : tape.commands)
    {
        if (c.operation == static_cast<u32>(paint_detail::opcode::CLEAR))
            d.clear(rgba(c.flags));
        else
        {
            const color tint = rgba(c.flags);
            const vertex v[] = {{c.x0, c.y0, 0, 0, tint},
                                {c.x1, c.y0, 1, 0, tint},
                                {c.x1, c.y1, 1, 1, tint},
                                {c.x0, c.y1, 0, 1, tint}};
            const i32 indices[] = {0, 1, 2, 0, 2, 3};
            d.draw(nullptr, v, 4, indices, 6);
        }
    }
}
} // namespace

PUI_TEST(compute_painter_writes_exact_pixels_for_ordered_translucent_rect_programs)
{
    auto &d = *compute_device;
    constexpr u32 WIDTH = 137, HEIGHT = 91;
    paint_detail::command_tape tape;
    CHECK(tape.prepare(1001, 0, 1001 * sizeof(paint_detail::command)));
    auto clear = rectangle(0, 0, WIDTH, HEIGHT, 0x2c21160bu);
    clear.operation = static_cast<u32>(paint_detail::opcode::CLEAR);
    CHECK(tape.append(clear, {}));
    u32 random = 0x31415926;
    for (u32 i = 0; i < 1000; ++i)
    {
        auto next = [&]()
        {
            random = random * 1664525u + 1013904223u;
            return random;
        };
        const i32 x = static_cast<i32>(next() % 180) - 25, y = static_cast<i32>(next() % 130) - 20;
        const i32 w = 1 + static_cast<i32>(next() % 70), h = 1 + static_cast<i32>(next() % 50);
        CHECK(tape.append(rectangle(x, y, w, h, next()), {}));
    }
    auto reference = d.create_target(WIDTH, HEIGHT);
    auto *actual = d.make_texture({.width = WIDTH,
                                   .height = HEIGHT,
                                   .render_target = true,
                                   .extra_usage = VK_IMAGE_USAGE_STORAGE_BIT});
    CHECK(reference && actual);
    if (!reference || !actual) return;
    native_draw(d, tape, reference);
    std::vector<u8> expected(WIDTH * HEIGHT * 4), pixels(expected.size());
    CHECK(d.read_pixels(reference, 0, 0, WIDTH, HEIGHT, expected.data()));
    {
        vk_detail::gpu_bin_job bins;
        CHECK(bins.initialize(d, tape, WIDTH, HEIGHT, 16, 64, 1001 * 54));
        vk_detail::rgba8_painter_job painter;
        CHECK(painter.initialize(d, bins, tape, actual, *compute_blend));
        CHECK(bins.record() && painter.record());
        bins.barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        CHECK(d.read_pixels(actual, 0, 0, WIDTH, HEIGHT, pixels.data()));
        CHECK(pixels == expected);
        if (pixels != expected)
        {
            usize at = 0;
            while (at < pixels.size() && pixels[at] == expected[at]) ++at;
            std::printf("compute mismatch pixel(%zu,%zu) channel%zu actual%u expected%u\n",
                        (at / 4) % WIDTH, (at / 4) / WIDTH, at % 4, pixels[at], expected[at]);
        }
        if (!bins.buffers[4].coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = bins.buffers[4].memory;
            range.size = VK_WHOLE_SIZE;
            CHECK(d.vkInvalidateMappedMemoryRanges(d.native.device, 1, &range) == VK_SUCCESS);
        }
        const auto *control = reinterpret_cast<const u32 *>(bins.buffers[4].mapped);
        CHECK(control[3] == 0 && control[5] == 54); // real workgroups painted all tiles
    }
    d.destroy_target(reference);
    d.destroy_target(actual);
}
PUI_TEST(compute_painter_rejects_unqualified_fractional_program_without_scene_writes)
{
    auto &d = *compute_device;
    paint_detail::command_tape tape;
    CHECK(tape.prepare(2, 0, 128));
    auto clear = rectangle(0, 0, 16, 16, 0xff000000u);
    clear.operation = static_cast<u32>(paint_detail::opcode::CLEAR);
    CHECK(tape.append(clear, {}));
    auto c = rectangle(1, 1, 8, 8, 0x7f030201u);
    c.x0 = 1.25f;
    CHECK(tape.append(c, {}));
    auto *target = d.make_texture({.width = 16,
                                   .height = 16,
                                   .render_target = true,
                                   .extra_usage = VK_IMAGE_USAGE_STORAGE_BIT});
    CHECK(target);
    if (!target) return;
    d.set_target(target);
    d.clear({3, 4, 5, 6});
    {
        vk_detail::gpu_bin_job bins;
        CHECK(bins.initialize(d, tape, 16, 16, 16, 64, 2));
        vk_detail::rgba8_painter_job painter;
        CHECK(!painter.initialize(d, bins, tape, target, *compute_blend));
        std::vector<u8> pixels(16 * 16 * 4);
        CHECK(d.read_pixels(target, 0, 0, 16, 16, pixels.data()));
        for (usize i = 0; i < pixels.size(); ++i) CHECK(pixels[i] == 3 + i % 4);
    }
    d.destroy_target(target);
}
int main()
{
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = compute_diagnostic;
    auto *device = create_vulkan_device(options);
    compute_device = dynamic_cast<vk_detail::device *>(device);
    if (!compute_device) return 1;
    vk_detail::rgba8_blend_lut blend;
    compute_blend = &blend;
    if (!blend.initialize(*compute_device)) return 1;
    for (const auto &test : test_registry()) test.fn();
    blend.release();
    if (!vulkan_wait_idle(device) || compute_errors || compute_device->stats.validation_errors)
        ++g_failures;
    destroy_vulkan_device(device);
    if (!g_failures)
        std::printf("all actual compute painter regressions passed (%zu tests)\n",
                    test_registry().size());
    return g_failures ? 1 : 0;
}
