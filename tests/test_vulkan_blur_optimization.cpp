// Independent regressions for validated chunking, bounded upload continuation,
// blur preparation and final-pass work. Reuse the unchanged fixture helpers.
#define main blur_contract_original_main
#include "test_vulkan_blur.cpp"
#undef main

namespace blur_optimization
{
constexpr i32 TEST_VERTEX_COUNT = 90000;
struct safe_view : view
{
    ~safe_view()
    {
        if (ctx && ctx->current_window) end_frame(ctx);
    }
};
struct api_probe;
thread_local api_probe *active = nullptr;
struct api_probe
{
    vk_detail::device &dev;
    PFN_vkWaitSemaphores wait;
    PFN_vkCreateImage image;
    PFN_vkCmdDrawIndexed draw;
    PFN_vkCmdBeginRendering render;
    u32 waits = 0, images = 0, filters = 0, loads = 0, discards = 0;
    VkRect2D last_filter_scissor{};
    static VKAPI_ATTR VkResult VKAPI_CALL wait_call(VkDevice d, const VkSemaphoreWaitInfo *i,
                                                    u64 timeout)
    {
        ++active->waits;
        return active->wait(d, i, timeout);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL image_call(VkDevice d, const VkImageCreateInfo *i,
                                                     const VkAllocationCallbacks *a, VkImage *out)
    {
        ++active->images;
        return active->image(d, i, a, out);
    }
    static VKAPI_ATTR void VKAPI_CALL draw_call(VkCommandBuffer c, u32 indices, u32 instances,
                                                u32 first, i32 offset, u32 instance)
    {
        auto &p = *active;
        for (const auto &pipeline : p.dev.pipelines)
            for (i32 kernel = 0; kernel < 3; ++kernel)
                if (p.dev.frame->graphics.pipeline == pipeline.filters[kernel])
                {
                    ++p.filters;
                    p.last_filter_scissor = p.dev.frame->graphics.scissor;
                }
        p.draw(c, indices, instances, first, offset, instance);
    }
    static VKAPI_ATTR void VKAPI_CALL render_call(VkCommandBuffer c, const VkRenderingInfo *i)
    {
        if (i->colorAttachmentCount)
        {
            if (i->pColorAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) ++active->loads;
            if (i->pColorAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
                ++active->discards;
        }
        active->render(c, i);
    }
    explicit api_probe(vk_detail::device &d)
        : dev(d), wait(d.vkWaitSemaphores), image(d.vkCreateImage), draw(d.vkCmdDrawIndexed),
          render(d.vkCmdBeginRendering)
    {
        active = this;
        d.vkWaitSemaphores = wait_call;
        d.vkCreateImage = image_call;
        d.vkCmdDrawIndexed = draw_call;
        d.vkCmdBeginRendering = render_call;
    }
    ~api_probe()
    {
        dev.vkWaitSemaphores = wait;
        dev.vkCreateImage = image;
        dev.vkCmdDrawIndexed = draw;
        dev.vkCmdBeginRendering = render;
        active = nullptr;
    }
};

bool chunked_pixels(vk_detail::device &d)
{
    constexpr i32 COUNT = TEST_VERTEX_COUNT, BASE = 407;
    std::vector<vertex> vertices(COUNT);
    std::vector<i32> indices;
    indices.reserve(COUNT + 192 * 3);
    for (i32 i = 0; i < COUNT; i += 3)
    {
        const f32 x = static_cast<f32>((i / 3 * 7) % 59) + 0.25f;
        const f32 y = static_cast<f32>((i / 3 * 11) % 59) + 0.375f;
        const color tint{static_cast<u8>(i * 13), static_cast<u8>(i * 29), static_cast<u8>(i * 47),
                         static_cast<u8>(83 + i % 161)};
        vertices[i] = {x, y, 0, 0, tint};
        vertices[i + 1] = {x + 2.75f, y, 0, 0, tint};
        vertices[i + 2] = {x, y + 3.125f, 0, 0, tint};
        indices.insert(indices.end(), {BASE + i, BASE + i + 1, BASE + i + 2});
    }
    // Each sparse triangle exceeds a prepared chunk's source-vertex interval.
    for (i32 i = 0; i < 192; ++i)
        indices.insert(indices.end(), {BASE + i, BASE + COUNT - 1 - i, BASE + COUNT / 2});
    safe_view v;
    CHECK(v.initialize(&d, 64, 64));
    std::vector<u8> actual(64 * 64 * 4), expected(actual.size());
    for (i32 variant = 0; variant < 2; ++variant)
    {
        begin_frame(v.ctx, *v.win, variant / 60.0, 1.0 / 60.0);
        if (!variant)
            for (auto &slot : static_cast<vk_detail::surface *>(v.surface)->slots)
                CHECK(d.prepare_slot_geometry(slot, 128 * 1024));
        const auto before = vulkan_statistics(&d);
        if (!variant)
            d.paint(d.white, vertices.data(), COUNT, indices.data(),
                    static_cast<i32>(indices.size()), true, BASE);
        else
            for (usize i = 0; i < indices.size();)
            {
                // Independent small batches never enter the chunking path.
                // Primitive order and original vertex bits remain identical.
                vertex batch[384];
                i32 local[384];
                const i32 count = static_cast<i32>(std::min<usize>(384, indices.size() - i));
                for (i32 j = 0; j < count; ++j)
                {
                    batch[j] = vertices[indices[i + j] - BASE];
                    local[j] = j;
                }
                d.draw(nullptr, batch, count, local, count);
                i += count;
            }
        CHECK(vulkan_statistics(&d).draw_buffer_allocations == before.draw_buffer_allocations);
        end_frame(v.ctx);
        CHECK(d.read_pixels(v.surface->scene_target(), 0, 0, 64, 64,
                            variant ? expected.data() : actual.data()));
        CHECK(!violation_count(v.ctx));
    }
    CHECK(actual == expected);
    return true;
}

struct guard_result
{
    i32 count = 0;
    bool correct = true;
};
void expected_guard(i32, const char *operation, void *user)
{
    auto &result = *static_cast<guard_result *>(user);
    ++result.count;
    result.correct = result.correct && std::strcmp(operation, "Vulkan index out of bounds") == 0;
}
bool late_invalid(const vulkan_device_opts &original)
{
    for (const i32 bad : {-1, TEST_VERTEX_COUNT})
    {
        guard_result result;
        auto options = original;
        options.error_callback = expected_guard;
        options.user = &result;
        auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
        CHECK(d);
        bool clean = false;
        {
            safe_view v;
            CHECK(v.initialize(d, 64, 64));
            begin_frame(v.ctx, *v.win, 0, 1.0 / 60.0);
            std::vector<vertex> vertices(TEST_VERTEX_COUNT, {8, 8, 0, 0, {255, 0, 0, 255}});
            std::vector<i32> indices(TEST_VERTEX_COUNT);
            for (i32 i = 0; i < TEST_VERTEX_COUNT; ++i) indices[i] = i;
            indices.back() = bad;
            const auto before = vulkan_statistics(d);
            d->draw(nullptr, vertices.data(), TEST_VERTEX_COUNT, indices.data(), TEST_VERTEX_COUNT);
            const auto after = vulkan_statistics(d);
            clean = after.draws == before.draws && after.upload_bytes == before.upload_bytes &&
                    result.count == 1 && result.correct && !after.validation_errors;
            end_frame(v.ctx);
        }
        destroy_vulkan_device(d);
        CHECK(clean);
    }
    return true;
}

bool continuation(vk_detail::device &d)
{
    safe_view v;
    CHECK(v.initialize(&d, 64, 64));
    begin_frame(v.ctx, *v.win, 0, 1.0 / 60.0);
    auto *s = static_cast<vk_detail::surface *>(v.surface);
    constexpr VkDeviceSize CAPACITY = 1024 * 1024;
    for (auto &slot : s->slots) CHECK(d.prepare_slot_geometry(slot, CAPACITY));
    auto *owner = d.frame;
    const auto before = vulkan_statistics(&d);
    {
        api_probe probe(d);
        VkDeviceSize offset = 0;
        CHECK(d.upload(CAPACITY - 32, offset, true));
        CHECK(d.upload(CAPACITY - 32, offset, true));
        if (s->slots.size() > 1)
        {
            CHECK(probe.waits == 0);
            CHECK(d.frame != owner && d.frame->geometry_owner == owner);
        }
        else
        {
            CHECK(probe.waits == 1);
            CHECK(d.frame == owner && !d.frame->geometry_owner);
        }
        CHECK(owner->group_serial == owner->serial && owner->serial != 0);
        CHECK(owner->geometry_frame_bytes == 2 * (CAPACITY - 32));
        CHECK(d.stats.upload_allocated_bytes == before.upload_allocated_bytes);
        CHECK(d.stats.buffer_allocations == before.buffer_allocations);
    }
    end_frame(v.ctx);
    CHECK(owner->group_serial >= owner->serial);
    CHECK(vulkan_wait_idle(&d));
    // Revisit both logical slots after continuation: acquire ownership and
    // geometry accounting must not leak into the following window frames.
    for (i32 frame = 1; frame <= 6; ++frame)
    {
        begin_frame(v.ctx, *v.win, frame / 60.0, 1.0 / 60.0);
        auto *logical = &s->slots[s->slot];
        CHECK((d.frame->geometry_owner ? d.frame->geometry_owner : d.frame) == logical);
        CHECK(!logical->group_serial && !logical->geometry_frame_bytes);
        if (s->slots.size() > 1 && frame == 1) CHECK(d.frame == owner);
        end_frame(v.ctx);
    }
    return true;
}

bool prepared_blur(vk_detail::device &d)
{
    for (const bool hint : {false, true})
    {
        safe_view v;
        CHECK(v.initialize(&d, 257, 193));
        begin_frame(v.ctx, *v.win, 0, 1.0 / 60.0);
        if (hint) CHECK(vulkan_prepare_blur(v.ctx, {.gaussian_area = {0, 0, 40, 28}}));
        api_probe probe(d);
        {
            ui u(v.ctx);
            for (i32 x = 0; x < 257; x += 7)
                u.draw_rect({static_cast<f32>(x), 0, 7, 193},
                            {static_cast<u8>(x * 31), 120, static_cast<u8>(x * 19), 255});
            u.blur({0, 0, 257, 193}, 22);
            const u32 allocated = probe.images;
            CHECK(allocated == (hint ? 0u : 2u));
            const auto *a = static_cast<vk_detail::texture *>(v.ctx->blur->half);
            CHECK(a->width == 129 && a->height == 97);
            const auto before = vulkan_statistics(&d);
            probe.filters = 0;
            u.blur({90, 60, 40, 28}, 8, corner_radii::all(7));
            CHECK(probe.images == allocated);
            CHECK(vulkan_statistics(&d).image_bytes == before.image_bytes);
            CHECK(v.ctx->blur->half == a);
            CHECK(probe.filters == 2);
            // The full final-pass ROI is 88x76. The consumer needs only its
            // 40x28 mask, AA edge and bilinear apron; no timings are asserted.
            CHECK(probe.last_filter_scissor.extent.width <= 44);
            CHECK(probe.last_filter_scissor.extent.height <= 32);
            CHECK(probe.last_filter_scissor.offset.x > 0);
            CHECK(probe.last_filter_scissor.offset.y > 0);
        }
        end_frame(v.ctx);
        CHECK(!violation_count(v.ctx));
    }
    CHECK(!vulkan_prepare_blur(nullptr));
    return true;
}
bool prepare_tiny_arena(vk_detail::device &d, VkDeviceSize bytes)
{
    auto *owner = d.frame;
    for (usize i = 0; i < owner->pages.size();)
        if (owner->pages[i].geometry)
        {
            CHECK(!owner->pages[i].used);
            d.free_page(owner->pages[i]);
            owner->pages.erase(owner->pages.begin() + static_cast<std::ptrdiff_t>(i));
        }
        else
            ++i;
    vk_detail::page page;
    CHECK(d.allocate_buffer(page, bytes,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    page.geometry = true;
    page.staged = d.options.upload_mode == vulkan_upload_mode::STAGED;
    if (page.staged) CHECK(d.allocate_device_buffer(page));
    owner->pages.push_back(page);
    owner->geometry_chunk_bytes = bytes;
    return true;
}
bool bounded_chunk(vk_detail::device &d)
{
    safe_view v;
    CHECK(v.initialize(&d, 64, 64));
    begin_frame(v.ctx, *v.win, 0, 1.0 / 60.0);
    auto *owner = d.frame;
    CHECK(prepare_tiny_arena(d, 128));
    const auto allocated = d.stats.upload_allocated_bytes;
    {
        api_probe probe(d);
        VkDeviceSize offset = 0;
        CHECK(d.upload(96, offset, true));
        CHECK(d.upload(96, offset, true));
        CHECK(probe.waits == 1 && d.frame == owner);
        CHECK(d.frame->geometry_chunk_bytes == 128);
        CHECK(d.stats.upload_allocated_bytes == allocated);
    }
    end_frame(v.ctx);
    return true;
}
bool split_filter(vk_detail::device &d)
{
    safe_view v;
    CHECK(v.initialize(&d, 64, 64));
    std::vector<u8> input(64 * 64 * 4), actual(input.size()), expected(input.size());
    for (usize i = 0; i < input.size(); ++i)
        input[i] = static_cast<u8>(i % 4 == 3 ? 255 : 31 + (i * 17) % 211);
    auto source = d.create_texture(64, 64, input.data());
    auto reference = d.create_target(64, 64);
    auto target = d.create_target(64, 64);
    CHECK(source && reference && target);
    auto parameters = vk_detail::plan_blur(3).taps;
    parameters.step[0] = 1.0f / 64;
    const rect area{0, 0, 64, 64};
    for (i32 variant = 0; variant < 2; ++variant)
    {
        begin_frame(v.ctx, *v.win, variant / 60.0, 1.0 / 60.0);
        if (variant) CHECK(prepare_tiny_arena(d, 3 * (d.vertex_stride() + sizeof(i32))));
        {
            api_probe probe(d);
            d.filter(static_cast<vk_detail::texture *>(source), area,
                     static_cast<vk_detail::texture *>(variant ? target : reference), area,
                     vk_detail::blur_kernel::GAUSSIAN, parameters, area);
            CHECK(probe.discards == 1);
            if (variant) CHECK(probe.loads >= 1); // second triangle resumes with LOAD
            CHECK(!d.discard_target);
            d.set_target(v.surface->scene_target());
            const vertex triangle[] = {{2, 2, 0, 0, color::white()},
                                       {9, 2, 0, 0, color::white()},
                                       {2, 9, 0, 0, color::white()}};
            const i32 indices[] = {0, 1, 2};
            d.draw(nullptr, triangle, 3, indices, 3);
            CHECK(probe.discards == 1 && probe.loads >= (variant ? 2u : 1u));
        }
        end_frame(v.ctx);
        CHECK(d.read_pixels(variant ? target : reference, 0, 0, 64, 64,
                            variant ? actual.data() : expected.data()));
    }
    CHECK(actual == expected);
    d.destroy_texture(source);
    d.destroy_target(reference);
    d.destroy_target(target);
    return true;
}
} // namespace blur_optimization

int main(int argc, char **argv)
{
    const bool staged = argc == 2 && std::strcmp(argv[1], "--staged") == 0;
    if (argc > 2 || (argc == 2 && !staged)) return 2;
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    for (const bool compact : {true, false})
        for (const u32 slots : {1u, 2u})
        {
            options.compact_vertices = compact;
            options.frames_in_flight = slots;
            auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
            if (!d) return 1;
            const bool passed =
                blur_optimization::chunked_pixels(*d) && blur_optimization::continuation(*d) &&
                blur_optimization::bounded_chunk(*d) && blur_optimization::split_filter(*d) &&
                blur_optimization::prepared_blur(*d) && vulkan_wait_idle(d) &&
                !vulkan_statistics(d).validation_errors;
            destroy_vulkan_device(d);
            if (!passed || errors) return 1;
        }
    if (!blur_optimization::late_invalid(options)) return 1;
    std::puts("Vulkan blur optimizations: dense/sparse exact pixels, late invalid indices, "
              "bounded continuation, scratch preparation and final-pass scissor passed");
    return 0;
}
