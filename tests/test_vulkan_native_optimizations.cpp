// Independent legacy-float/uncached oracles; no existing fixture is altered.
#define PUFFERUI_IMPLEMENTATION
#include <pufferui/vulkan.h>
#include <bit>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace pui;
namespace
{
i32 errors = 0;
void diagnostic(i32 code, const char *text, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, text);
    ++errors;
}
#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            std::fprintf(stderr, "native optimization check failed at %d: %s\n", __LINE__,         \
                         #condition);                                                              \
            return false;                                                                          \
        }                                                                                          \
    } while (false)
struct scene
{
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    font_handle font = FONT_INVALID;
    i32 width = 321, height = 193;
    bool initialize(vulkan_device_opts options, i32 h = 193)
    {
        height = h;
        device = create_vulkan_device(options);
        CHECK(device);
        surface = create_vulkan_surface(device, {.width = width, .height = height});
        CHECK(surface);
        ctx = create_context(device, surface);
        win = add_window(ctx, nullptr, surface,
                         {0, 0, static_cast<f32>(width), static_cast<f32>(height)});
        font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        CHECK(font != FONT_INVALID);
        return true;
    }
    bool pixels(std::vector<u8> &result)
    {
        result.resize(static_cast<usize>(width) * height * 4);
        return device->read_pixels(surface->scene_target(), 0, 0, width, height, result.data());
    }
    ~scene()
    {
        if (ctx) destroy_context(ctx);
        if (surface) device->destroy_surface(surface);
        if (device) destroy_vulkan_device(device);
    }
};
bool same_layout(const text_store::text_layout &a, const text_store::text_layout &b)
{
    const auto bits = [](f32 value) { return std::bit_cast<u32>(value); };
    if (bits(a.width) != bits(b.width) || bits(a.min_xoff) != bits(b.min_xoff) ||
        a.forward != b.forward || a.gs.size() != b.gs.size())
        return false;
    for (usize i = 0; i < a.gs.size(); ++i)
    {
        const auto &x = a.gs[i], &y = b.gs[i];
        if (x.vector != y.vector || x.glyph_index != y.glyph_index || x.cluster != y.cluster ||
            bits(x.offset.x) != bits(y.offset.x) || bits(x.offset.y) != bits(y.offset.y) ||
            bits(x.g.advance) != bits(y.g.advance) || bits(x.g.xoff) != bits(y.g.xoff) ||
            bits(x.g.yoff) != bits(y.g.yoff) || bits(x.g.w) != bits(y.g.w) ||
            bits(x.g.h) != bits(y.g.h))
            return false;
    }
    return true;
}
bool shaping(scene &s)
{
    std::vector<std::string> labels;
    std::vector<u32> widths;
    labels.reserve(5000);
    widths.reserve(5000);
    auto &font = *s.ctx->ts->fonts[s.font].harfbuzz;
    const u64 cold = font.shape_calls;
    for (i32 i = 0; i < 5000; ++i) labels.push_back("#" + std::to_string(i));
    for (const auto &label : labels)
        widths.push_back(
            std::bit_cast<u32>(detail::get_layout(s.ctx, s.font, 13.375f, label)->width));
    const u64 warm = font.shape_calls;
    CHECK(warm - cold == 5000);
    for (i32 repeat = 0; repeat < 3; ++repeat)
        for (usize i = 0; i < labels.size(); ++i)
            CHECK(std::bit_cast<u32>(
                      detail::get_layout(s.ctx, s.font, 13.375f, labels[i])->width) == widths[i]);
    CHECK(font.shape_calls == warm);
    for (f32 size : {12.0f, 17.125f, 30.1f, 13.375f})
        for (const auto &label : labels)
            CHECK(detail::get_layout(s.ctx, s.font, size, label)->width > 0);
    CHECK(font.shape_calls == warm); // font-unit runs survive exact-size cache churn
    CHECK(font.runs->cache.bytes <= 4 * 1024 * 1024);
    CHECK(s.ctx->ts->vector_layouts.bytes <= 32 * 1024 * 1024);
    for (const char *text : {"office ffi AV", "a\xcc\x81", "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"})
        for (f32 size : {9.375f, 13.375f, 27.1f, 72.25f})
        {
            text_store::text_layout cached;
            CHECK(detail::harfbuzz_layout(s.ctx, s.font, size, text, cached));
            const u64 before = font.shape_calls;
            text_store::text_layout hit;
            CHECK(detail::harfbuzz_layout(s.ctx, s.font, size, text, hit));
            CHECK(font.shape_calls == before && same_layout(cached, hit));
            font.runs->cache.clear();
            text_store::text_layout scalar;
            CHECK(detail::harfbuzz_layout(s.ctx, s.font, size, text, scalar));
            CHECK(font.shape_calls == before + 1 && same_layout(cached, scalar));
        }
    const auto bold = load_font(s.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    CHECK(bold != FONT_INVALID);
    const f32 a = detail::get_layout(s.ctx, s.font, 21.25f, "separate font")->width;
    const f32 b = detail::get_layout(s.ctx, bold, 21.25f, "separate font")->width;
    CHECK(a != b);
    set_text_shaper(s.ctx, nullptr);
    CHECK(detail::get_layout(s.ctx, s.font, 21.25f, "separate font")->width == a);
    return true;
}
bool packed(vulkan_device_opts options)
{
    options.vector_text = false;
    scene compact, legacy;
    CHECK(compact.initialize(options, 321));
    const auto native = vulkan_handles(compact.device);
    options.native = &native;
    options.compact_vertices = false;
    CHECK(legacy.initialize(options, 321));
    std::vector<vertex> vertices;
    std::vector<i32> indices;
    for (i32 i = 0; i < 256; ++i)
    {
        const i32 base = static_cast<i32>(vertices.size());
        for (i32 corner = 0; corner < 4; ++corner)
            vertices.push_back(
                {static_cast<f32>((i % 16) * 20 + (corner == 1 || corner == 2 ? 19 : 0)) + .25f,
                 static_cast<f32>((i / 16) * 20 + (corner >= 2 ? 19 : 0)) + .75f,
                 corner == 1 || corner == 2 ? .9f : .1f,
                 corner >= 2 ? .9f : .1f,
                 {static_cast<u8>(i), static_cast<u8>(i * 29 + corner * 17),
                  static_cast<u8>(255 - i), static_cast<u8>(i * 41 + corner * 53)}});
        for (i32 n : {0, 1, 2, 0, 2, 3}) indices.push_back(base + n);
    }
    for (u32 value = 0; value < 256; ++value)
    {
        const u32 top = value ? 31u - static_cast<u32>(std::countl_zero(value)) : 0;
        const u32 bits = !value ? 0
                         : value == 255
                             ? 0x3f800000u
                             : ((top + 119) << 23) |
                                   (((value * 0x01010101u + (1u << top)) >> (top + 1)) & 0x7fffffu);
        CHECK(bits == std::bit_cast<u32>(vk_detail::BYTE_CHANNELS.values[value]));
    }
    std::vector<u8> pixels[2];
    for (i32 f = 0; f < 12; ++f)
    {
        u64 uploaded[2]{};
        for (i32 variant = 0; variant < 2; ++variant)
        {
            auto &s = variant ? legacy : compact;
            begin_frame(s.ctx, *s.win, f / 60.0, 1.0 / 60.0);
            CHECK(s.device->prepare_geometry({vertices.size(), indices.size()}));
            s.device->clear({17, 29, 53, 137});
            const rect clip{1.25f + f % 3, 2.75f, 317.25f, 315.75f};
            s.device->set_clip(&clip);
            const auto before = vulkan_statistics(s.device);
            s.device->draw(nullptr, vertices.data(), static_cast<i32>(vertices.size()),
                           indices.data(), static_cast<i32>(indices.size()));
            end_frame(s.ctx);
            uploaded[variant] = vulkan_statistics(s.device).upload_bytes - before.upload_bytes;
            CHECK(s.pixels(pixels[variant]));
            CHECK(!violation_count(s.ctx));
        }
        CHECK(pixels[0] == pixels[1]);
        CHECK(uploaded[0] == vertices.size() * 20 + indices.size() * 4);
        CHECK(uploaded[1] == vertices.size() * 32 + indices.size() * 4);
    }
    return true;
}
struct capture_surface : render_surface
{
    scene &owner;
    render_surface *inner;
    vk_detail::page storage;
    i32 count = 0;
    static constexpr i32 FRAMES = 192;
    bool okay = true;
    explicit capture_surface(scene &s) : owner(s), inner(s.surface)
    {
        auto *d = static_cast<vk_detail::device *>(owner.device);
        okay =
            d->allocate_buffer(storage, frame_bytes() * FRAMES, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        owner.win->surface = this;
    }
    VkDeviceSize frame_bytes() const
    {
        return static_cast<VkDeviceSize>(owner.width) * owner.height * 4;
    }
    void make_current(render_device &device) override { inner->make_current(device); }
    texture_handle default_target() override { return inner->default_target(); }
    texture_handle scene_target() override { return inner->scene_target(); }
    void output_size(i32 &w, i32 &h) override { inner->output_size(w, h); }
    void resize() override { inner->resize(); }
    void present() override
    {
        auto *d = static_cast<vk_detail::device *>(owner.device);
        if (okay && count < FRAMES)
        {
            // Capture after end-frame overlays/text, before normal presentation
            // submits. CPU readback never serializes the individual frames.
            d->flush_text();
            auto *image = static_cast<vk_detail::texture *>(inner->scene_target());
            d->transition(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy{};
            copy.bufferOffset = frame_bytes() * count;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {static_cast<u32>(owner.width), static_cast<u32>(owner.height), 1};
            d->vkCmdCopyImageToBuffer(d->frame->command, image->image, image->layout,
                                      storage.buffer, 1, &copy);
            if (++count == FRAMES)
            {
                VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = storage.buffer;
                barrier.size = VK_WHOLE_SIZE;
                VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dependency.bufferMemoryBarrierCount = 1;
                dependency.pBufferMemoryBarriers = &barrier;
                d->vkCmdPipelineBarrier2(d->frame->command, &dependency);
            }
        }
        inner->present();
    }
    bool finish()
    {
        CHECK(okay && count == FRAMES && vulkan_wait_idle(owner.device));
        auto *d = static_cast<vk_detail::device *>(owner.device);
        if (!storage.coherent)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = storage.memory;
            range.size = VK_WHOLE_SIZE;
            CHECK(d->check(d->vkInvalidateMappedMemoryRanges(d->native.device, 1, &range),
                           "invalidate pipelined golden"));
        }
        return true;
    }
    ~capture_surface() override
    {
        owner.win->surface = inner;
        (void)vulkan_wait_idle(owner.device);
        static_cast<vk_detail::device *>(owner.device)->free_page(storage);
    }
};
bool text_frame(scene &s, i32 frame)
{
    const f32 density = frame < 48 ? 1 : frame < 96 ? 1.5f : frame < 144 ? 2 : 1;
    set_window_scale(*s.win, density);
    auto style = default_dark();
    style.font = s.font;
    style.text_size = frame / 12 % 3 == 0 ? 13.375f : frame / 12 % 3 == 1 ? 21.25f : 32.125f;
    style.text_gamma = frame / 24 % 4 == 0   ? .8f
                       : frame / 24 % 4 == 1 ? 1.0001f
                       : frame / 24 % 4 == 2 ? .9999f
                                             : 1.2f;
    set_theme(s.ctx, style);
    const f32 shift = frame % 12 < 6 ? 0 : static_cast<f32>(frame % 6) * .125f;
    begin_frame(s.ctx, *s.win, frame / 60.0, 1.0 / 60.0);
    {
        ui u(s.ctx);
        u.draw_rect({0, 0, 321, 193}, {23, 37, 51, 179});
        {
            auto clip = u.region({3.25f, 2.75f, 300.5f, 178.25f}, "clip"_id);
            u.text({7.25f + shift, 4.75f, 300, 37}, "office ffi a\xcc\x81",
                   {253, 187, 73, static_cast<u8>(70 + frame % 180)});
            u.text({11.75f + shift, 8.25f, 300, 37}, "office ffi a\xcc\x81", {43, 173, 241, 91});
            u.draw_rect({13.25f, 37.5f, 196.5f, 9.25f}, {79, 113, 151, 127});
            u.text({9.5f, 39.25f, 290, 43}, "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85  AV ffi",
                   {241, 73, 169, 163});
            {
                auto disabled = u.disabled();
                u.text({8.25f, 85.75f, 280, 35}, "Disabled overlap", {213, 239, 251, 199});
            }
            {
                auto font = u.text_style(192, s.font);
                u.text({235, 8, 65, 170}, "Q", {255, 255, 255, 137});
            }
        }
        u.blur({4.5f, 120.25f, 218.25f, 32.5f}, 8);
        u.text({8.25f, 154.5f, 270, 35}, "native after blur", {173, 227, 239, 211});
    }
    end_frame(s.ctx);
    return !violation_count(s.ctx) && vulkan_last_result(s.device) == VK_SUCCESS;
}
bool aligned_positions(scene &cached, scene &reference)
{
    i32 ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&cached.ctx->ts->fonts[cached.font].info, &ascent, &descent, &gap);
    const f32 size = static_cast<f32>(ascent - descent) / 32;
    std::vector<u8> actual, expected;
    u64 last_hits = 0;
    for (i32 frame = 0; frame < 16; ++frame)
    {
        const auto before = vulkan_statistics(cached.device);
        for (auto *s : {&cached, &reference})
        {
            set_window_scale(*s->win, 1);
            auto style = default_dark();
            style.font = s->font;
            style.text_size = size; // exact font-unit scale 1/32
            set_theme(s->ctx, style);
            begin_frame(s->ctx, *s->win, frame / 60.0 + 4, 1.0 / 60.0);
            {
                ui u(s->ctx);
                u.draw_rect({0, 0, 321, 193}, {17, 31, 53, 191});
                for (i32 i = 0; i < 8; ++i)
                    u.text({256.0f + i, 32, 64, size}, "A", {211, 97, 151, 137});
            }
            end_frame(s->ctx);
        }
        last_hits =
            vulkan_statistics(cached.device).glyph_coverage_hits - before.glyph_coverage_hits;
    }
    CHECK(cached.pixels(actual) && reference.pixels(expected) && actual == expected);
    if (static_cast<vk_detail::device *>(cached.device)->text->coverage) CHECK(last_hits == 8);
    return true;
}
bool coverage(vulkan_device_opts options)
{
    options.glyph_coverage_budget_bytes = 256 * 256 * 4;
    scene cached, reference;
    CHECK(cached.initialize(options));
    const auto native = vulkan_handles(cached.device);
    options.native = &native;
    options.glyph_coverage_budget_bytes = 0;
    options.compact_vertices = false;
    CHECK(reference.initialize(options));
    capture_surface actual(cached), expected(reference);
    CHECK(actual.okay && expected.okay);
    for (i32 f = 0; f < 192; ++f)
    {
        CHECK(text_frame(cached, f));
        CHECK(text_frame(reference, f));
    }
    CHECK(actual.finish() && expected.finish());
    for (i32 f = 0; f < 192; ++f)
        if (std::memcmp(actual.storage.mapped + actual.frame_bytes() * f,
                        expected.storage.mapped + expected.frame_bytes() * f,
                        static_cast<usize>(actual.frame_bytes())) != 0)
        {
            std::fprintf(stderr, "coverage/legacy RGBA mismatch at frame %d\n", f);
            return false;
        }
    CHECK(aligned_positions(cached, reference));
    const auto stats = vulkan_statistics(cached.device);
    auto *d = static_cast<vk_detail::device *>(cached.device);
    if (d->text->coverage)
    {
        CHECK(stats.glyph_coverage_hits && stats.glyph_coverage_fills);
        CHECK(stats.glyph_coverage_evictions && stats.glyph_coverage_bytes <= 256 * 256 * 4);
        auto &cache = *d->text->coverage;
        const auto epoch = cache.epoch;
        ++d->paint_depth;
        CHECK(d->prepare_coverage() && cache.epoch == epoch);
        --d->paint_depth;
        d->text->pending.push_back({});
        CHECK(d->prepare_coverage() && cache.epoch == epoch && d->text->pending.size() == 1);
        d->text->pending.clear();
        auto &region = cache.regions[0];
        region.retiring = true;
        region.last_use = UINT64_MAX;
        const auto generation = region.generation;
        const auto used = region.x;
        CHECK(d->prepare_coverage());
        CHECK(region.retiring && region.generation == generation && region.x == used);
        region.last_use = d->known_completed;
        cache.pressure = true; // pending pressure must not immediately re-retire the freed region
        CHECK(d->prepare_coverage());
        CHECK(!region.retiring && region.generation != generation && !region.x && !region.y);
    }
    CHECK(!vulkan_statistics(reference.device).glyph_coverage_bytes);
    CHECK(shaping(reference));
    return true;
}
} // namespace
int main(int argc, char **argv)
{
    const bool staged = argc == 2 && std::strcmp(argv[1], "--staged") == 0;
    if (argc > 2 || (argc == 2 && !staged)) return 2;
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    if (!packed(options) || !coverage(options) || errors) return 1;
    std::puts("native optimizations: exact 20/32-byte colors, 192 coverage frames, retirement and "
              "5000-label size reuse passed");
    return 0;
}
