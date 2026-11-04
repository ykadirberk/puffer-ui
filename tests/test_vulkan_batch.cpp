// Independent lossless isolated-scene and consolidated-upload regressions.
// Capture immutable references with the same source and frozen base headers.
#define PUFFERUI_IMPLEMENTATION
#include <pufferui/vulkan.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>
using namespace pui;
namespace
{
i32 failures = 0;
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                   \
            ++failures;                                                                            \
            return false;                                                                          \
        }                                                                                          \
    } while (false)
constexpr std::string_view COMPLEX =
    R"svg(<svg viewBox="0 0 64 64"><defs><linearGradient id="g" x2="1" y2="1"><stop stop-color="currentColor"/><stop offset="1" stop-color="#7562db"/></linearGradient><clipPath id="c"><circle cx="32" cy="32" r="28"/></clipPath></defs><circle cx="32" cy="32" r="28" fill="url(#g)"/><g clip-path="url(#c)" opacity=".7"><path d="M0 44Q16 20 32 44T64 44V64H0Z" fill="white"/><circle cx="32" cy="32" r="12" fill="#192838" fill-opacity=".5"/></g></svg>)svg";
constexpr color PALETTE[] = {{82, 191, 255, 255}, {248, 128, 132, 255}};
struct environment
{
    render_device *base = nullptr;
    vk_detail::device *dev = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    vector_handle image = nullptr;
    vector_handle simple = nullptr;
    ~environment()
    {
        destroy_vector(image);
        destroy_vector(simple);
        if (ctx) destroy_context(ctx);
        if (surface) base->destroy_surface(surface);
        destroy_vulkan_device(base);
    }
    bool open(bool staged, bool cache = false, usize hint = 96, u64 budget = 256 * 1024 * 1024,
              bool prepare_paths = true)
    {
        base = create_vulkan_device(
            {.validation = true,
             .tile_cache = cache,
             .vector_text = false,
             .upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT,
             .upload_budget_bytes = budget});
        REQUIRE(base);
        dev = static_cast<vk_detail::device *>(base);
        surface = create_vulkan_surface(base, {.width = 512, .height = 512});
        REQUIRE(surface);
        ctx = create_context(base, surface);
        win = add_window(ctx, nullptr, surface, {0, 0, 512, 512});
        if (!prepare_paths) return vulkan_wait_idle(base);
        auto source = load_svg(COMPLEX);
        REQUIRE(source);
        image = source.image;
        REQUIRE(ui(ctx).prepare_vector(image, {.max_pixel_size = {64, 64},
                                               .max_instances = hint,
                                               .resource_budget_bytes = 32 * 1024 * 1024}));
        auto solid = load_svg("<svg viewBox='0 0 64 64'><rect x='4' y='4' width='56' height='56' "
                              "fill='#36c8'/></svg>");
        REQUIRE(solid);
        simple = solid.image;
        REQUIRE(
            ui(ctx).prepare_vector(simple, {.max_pixel_size = {64, 64}, .max_instances = hint}));
        REQUIRE(vulkan_wait_idle(base));
        return true;
    }
    bool clean() const
    {
        REQUIRE(vulkan_wait_idle(base));
        REQUIRE(!violation_count(ctx) && !dev->stats.validation_errors &&
                vulkan_last_result(base) == VK_SUCCESS);
        return true;
    }
};
bool bench_scene(environment &env, usize frame)
{
    ui u(env.ctx);
    const f32 angle = .12f * std::sin(static_cast<f32>(frame) * .03f), c = std::cos(angle),
              s = std::sin(angle);
    for (usize i = 0; i < 96; ++i)
    {
        const rect area{static_cast<f32>(i % 12) * 42 + 2, static_cast<f32>(i / 12) * 42 + 2, 38,
                        38};
        const vec2 center{area.x + 19, area.y + 19};
        REQUIRE(
            u.draw_vector(area, env.image,
                          {.transform = {c, s, -s, c, center.x - c * center.x + s * center.y,
                                         center.y - s * center.x - c * center.y},
                           .style = {.current_color = PALETTE[(frame / 30) % 2], .opacity = .8f}}));
    }
    return true;
}
bool golden(bool staged, bool cache, const std::filesystem::path &directory, bool capture)
{
    if (capture)
    {
        REQUIRE(!std::filesystem::exists(directory));
        REQUIRE(std::filesystem::create_directories(directory));
    }
    environment env;
    REQUIRE(env.open(staged, cache));
    std::vector<u8> actual(512 * 512 * 4), expected(actual.size());
    for (usize frame = 0; frame < 180; ++frame)
    {
        begin_frame(env.ctx, *env.win, frame / 60.0, 1.0 / 60);
        REQUIRE(bench_scene(env, frame));
        end_frame(env.ctx);
        REQUIRE(env.base->read_pixels(env.surface->scene_target(), 0, 0, 512, 512, actual.data()));
        char filename[32];
        std::snprintf(filename, sizeof(filename), "frame-%03zu.rgba", frame);
        const auto file = directory / filename;
        if (capture)
        {
            std::ofstream output(file, std::ios::binary);
            output.write(reinterpret_cast<const char *>(actual.data()), actual.size());
            REQUIRE(output.good());
        }
        else
        {
            std::ifstream input(file, std::ios::binary);
            input.read(reinterpret_cast<char *>(expected.data()), expected.size());
            REQUIRE(input && input.peek() == std::char_traits<char>::eof());
            if (actual != expected)
            {
                usize first = 0;
                while (actual[first] == expected[first]) ++first;
                std::fprintf(stderr,
                             "vector frame %zu mismatch at pixel (%zu,%zu) channel %zu: %u/%u\n",
                             frame, first / 4 % 512, first / 4 / 512, first % 4, actual[first],
                             expected[first]);
                REQUIRE(actual == expected);
            }
        }
    }
    REQUIRE(env.clean());
    std::printf("180 exact composed Slug frames passed (%s, cache=%d, capture=%d)\n",
                staged ? "staged" : "direct", cache, capture);
    return true;
}
#if !defined(PUFFERUI_BATCH_BASELINE)
struct command_capture
{
    PFN_vkCmdCopyBuffer copy = nullptr;
    PFN_vkQueueSubmit2 submit = nullptr;
    PFN_vkCmdBeginRendering rendering = nullptr;
    u64 copies = 0, submissions = 0, renders = 0, transferred = 0;
    bool valid = true, staged = false;
    vk_detail::device *dev = nullptr;
};
thread_local command_capture *commands = nullptr;
void VKAPI_CALL on_copy(VkCommandBuffer command, VkBuffer source, VkBuffer destination, u32 count,
                        const VkBufferCopy *regions)
{
    auto &c = *commands;
    ++c.copies;
    auto &slot = *c.dev->frame;
    c.valid = c.valid && command == slot.uploads && count == 1;
    bool found = false;
    for (const auto &page : slot.pages)
        if (page.buffer == source && page.device_buffer == destination)
        {
            found = true;
            c.valid = c.valid && page.staged && page.used && page.used <= page.capacity &&
                      regions[0].srcOffset == 0 && regions[0].dstOffset == 0 &&
                      regions[0].size == page.used;
        }
    c.valid = c.valid && found;
    c.transferred += regions[0].size;
    c.copy(command, source, destination, count, regions);
}
VkResult VKAPI_CALL on_submit(VkQueue queue, u32 count, const VkSubmitInfo2 *info, VkFence fence)
{
    auto &c = *commands;
    ++c.submissions;
    const auto &slot = *c.dev->frame;
    c.valid = c.valid && count == 1 && info[0].commandBufferInfoCount == (c.staged ? 2u : 1u) &&
              info[0].pCommandBufferInfos[c.staged ? 1 : 0].commandBuffer == slot.command &&
              (!c.staged || info[0].pCommandBufferInfos[0].commandBuffer == slot.uploads);
    return c.submit(queue, count, info, fence);
}
void VKAPI_CALL on_render(VkCommandBuffer command, const VkRenderingInfo *info)
{
    ++commands->renders;
    commands->rendering(command, info);
}
bool upload_regressions(bool staged, bool streaming)
{
    environment env;
    REQUIRE(env.open(staged, false, 1, streaming ? 512 * 1024 : 256 * 1024 * 1024, false));
    auto &dev = *env.dev;
    if (streaming)
    {
        // Force a small compatible arena without weakening the public hint's
        // minimum reservation. This must stream through completed slots rather
        // than grow storage or overwrite an in-flight prefix.
        for (auto &slot : static_cast<vk_detail::surface *>(env.surface)->slots)
            REQUIRE(dev.prepare_slot_geometry(slot, 4096));
    }
    else
        REQUIRE(vulkan_prepare_surface_geometry(env.surface, {.vertices = 4096, .indices = 6144}));
    command_capture capture{dev.vkCmdCopyBuffer, dev.vkQueueSubmit2, dev.vkCmdBeginRendering};
    capture.dev = &dev;
    capture.staged = staged;
    commands = &capture;
    dev.vkCmdCopyBuffer = on_copy;
    dev.vkQueueSubmit2 = on_submit;
    dev.vkCmdBeginRendering = on_render;
    begin_frame(env.ctx, *env.win, 0, 1.0 / 60);
    const vertex quad[] = {{0, 0, 0, 0, {17, 42, 93, 128}},
                           {32, 0, 1, 0, {17, 42, 93, 128}},
                           {32, 32, 1, 1, {17, 42, 93, 128}},
                           {0, 32, 0, 1, {17, 42, 93, 128}}};
    const i32 indices[] = {0, 1, 2, 0, 2, 3};
    const usize count = streaming ? 12000 : 96;
    for (usize i = 0; i < count; ++i) env.base->draw(nullptr, quad, 4, indices, 6);
    const u64 before_end = capture.copies;
    end_frame(env.ctx);
    dev.vkCmdCopyBuffer = capture.copy;
    dev.vkQueueSubmit2 = capture.submit;
    dev.vkCmdBeginRendering = capture.rendering;
    commands = nullptr;
    REQUIRE(capture.valid);
    REQUIRE(!staged || capture.copies == capture.submissions);
    REQUIRE(staged || !capture.copies);
    REQUIRE(!streaming || capture.submissions > 2);
    REQUIRE(streaming || (before_end == 0 && capture.submissions == 1 && capture.renders == 1));
    REQUIRE(env.clean());
    return true;
}
bool batch_pixels(bool staged)
{
    const auto empty = vk_detail::vector_group_roi({100.25f, 100.25f, 2, 2}, {0, 0, 256, 256}, {},
                                                   {{0, 0}, {8, 8}});
    REQUIRE(empty.w == 0 && empty.h == 0);
    environment batched, immediate;
    REQUIRE(batched.open(staged));
    REQUIRE(immediate.open(staged, false, 1));
    REQUIRE(batched.dev->paths->layer_columns > 1 && immediate.dev->paths->layer_columns == 1);
    std::vector<u8> a(512 * 512 * 4), b(a.size());
    for (usize frame = 0; frame < 48; ++frame)
    {
        for (auto *env : {&batched, &immediate})
        {
            ui u(env->ctx);
            const f32 scale = static_cast<f32>(1u << (frame % 3));
            set_window_scale(*env->win, scale);
            begin_frame(env->ctx, *env->win, frame / 60.0, 1.0 / 60);
            for (usize i = 0; i < 35; ++i)
            {
                // Overlap/tile reuse, nested clip changes, reflection, group
                // skips, transparent/opaque fallbacks and mesh/vector ordering.
                const rect area{static_cast<f32>(24 + i % 5 * 9) / scale,
                                static_cast<f32>(20 + i % 7 * 11) / scale, 38 / scale, 38 / scale};
                auto clip = u.region(
                    {static_cast<f32>(i / 13 % 2) * 32 / scale, 0, 96 / scale, 128 / scale},
                    id_child("batch_clip"_id, i));
                const f32 opacity = i % 7 == 0 ? 1.0f : i % 11 == 0 ? 0.0f : .8f;
                REQUIRE(u.draw_vector(
                    area, env->image,
                    {.transform = frame % 2 ? affine2{-1, 0, .125f, 1, 120 / scale, 0} : affine2{},
                     .style = {.current_color = PALETTE[(frame / 4 + i) % 2],
                               .opacity = opacity}}));
                if (i == 4 || i == 17 || i == 34) REQUIRE(u.draw_vector(area, env->simple));
                if (i % 9 == 0)
                    u.draw_rect({20 / scale, 20 / scale, 12 / scale, 8 / scale}, {12, 44, 99, 77});
            }
            end_frame(env->ctx);
            auto &pixels = env == &batched ? a : b;
            REQUIRE(env->base->read_pixels(env->surface->scene_target(), 0, 0, 512, 512,
                                           pixels.data()));
        }
        REQUIRE(a == b);
    }
    REQUIRE(batched.clean() && immediate.clean());
    // A full batch must collapse the original 480 draws without changing upload
    // bytes or creating new buffers on the draw path.
    const auto initial = vulkan_statistics(batched.base);
    set_window_scale(*batched.win, 1);
    begin_frame(batched.ctx, *batched.win, 1, 1.0 / 60);
    REQUIRE(bench_scene(batched, 0));
    end_frame(batched.ctx);
    const auto final = vulkan_statistics(batched.base);
    REQUIRE(final.draws - initial.draws == 30);
    REQUIRE(final.draw_buffer_allocations == initial.draw_buffer_allocations);
    REQUIRE(final.upload_bytes - initial.upload_bytes ==
            96 * 5 * sizeof(vk_detail::vector_instance));
    REQUIRE(batched.clean());
    // The tile reservation must honor actual Vulkan image alignment and fall
    // back to fewer tiles when a larger prepared extent consumes the budget.
    REQUIRE(ui(batched.ctx)
                .prepare_vector(batched.image, {.max_pixel_size = {256, 256},
                                                .max_instances = 96,
                                                .resource_budget_bytes = 16 * 1024 * 1024}));
    REQUIRE(batched.dev->paths->layer_bytes <= 16 * 1024 * 1024);
    REQUIRE(batched.clean());
    return true;
}
#endif
} // namespace
int main(int argc, char **argv)
{
    bool staged = false, cache = false, capture = false;
    const char *directory = nullptr;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (!std::strcmp(argv[i], "--cache"))
            cache = true;
        else if ((!std::strcmp(argv[i], "--capture-reference") ||
                  !std::strcmp(argv[i], "--reference")) &&
                 i + 1 < argc)
        {
            capture = !std::strcmp(argv[i], "--capture-reference");
            directory = argv[++i];
        }
        else
            return 2;
    if (directory) return golden(staged, cache, directory, capture) ? 0 : 1;
#if !defined(PUFFERUI_BATCH_BASELINE)
    if (!upload_regressions(staged, false) || !upload_regressions(staged, true) ||
        !batch_pixels(staged))
        return 1;
    std::puts("all native batch regressions passed (copies, streaming, order, clips, DPI and draw "
              "bounds)");
    return failures ? 1 : 0;
#else
    return 2;
#endif
}
