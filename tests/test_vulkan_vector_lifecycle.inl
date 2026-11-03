// Shared immutable sources, precise retirement and bounded 5000-instance draws.
static void vector_native_lifecycle(vulkan_upload_mode mode)
{
    using namespace vk_detail;
    auto *base =
        create_vulkan_device({.validation = true, .vector_text = false, .upload_mode = mode});
    CHECK(base);
    if (!base) return;
    auto &dev = *static_cast<device *>(base);
    auto *s1 = create_vulkan_surface(base, {.width = 128, .height = 128});
    auto *s2 = create_vulkan_surface(base, {.width = 128, .height = 128});
    CHECK(s1 && s2);
    if (!s1 || !s2)
    {
        destroy_vulkan_device(base);
        return;
    }
    auto *c1 = create_context(base, s1);
    auto *c2 = create_context(base, s2);
    auto *w1 = add_window(c1, nullptr, s1, {0, 0, 128, 128});
    auto *w2 = add_window(c2, nullptr, s2, {0, 0, 128, 128});
    ui u1(c1), u2(c2);
    CHECK(dev.paths == nullptr);
    auto image = load_svg(
        "<svg viewBox='0 0 64 64'><rect x='8' y='8' width='48' height='48' fill='red'/></svg>");
    CHECK(image);
    if (!image)
    {
        destroy_context(c1);
        destroy_context(c2);
        base->destroy_surface(s1);
        base->destroy_surface(s2);
        destroy_vulkan_device(base);
        return;
    }
    CHECK(u1.prepare_vector(image.image, {.max_pixel_size = {128, 128}, .max_instances = 5000}));
    CHECK(u2.prepare_vector(image.image, {.max_pixel_size = {64, 64}, .max_instances = 8}));
    auto *p1 = static_cast<native_painter *>(c1->painter),
         *p2 = static_cast<native_painter *>(c2->painter);
    auto *asset = p1->path_bindings->get(image.image);
    CHECK(asset == p2->path_bindings->get(image.image) && asset->references == 2 &&
          image.image->references.load() == 2);
    CHECK(dev.paths->assets.size() == 1);
    auto unknown = create_path({.view_box = {0, 0, 16, 16}});
    CHECK(unknown);
    begin_frame(c1, *w1, 0, 1.0 / 60);
    base->clear({0, 0, 0, 0});
    CHECK(u1.draw_vector({0, 0, 16, 16}, unknown.image).error.code ==
          vector_error_code::NOT_PREPARED);
    {
        auto clip = u1.region({0, 0, 0, 0}, "vector_zero_clip"_id);
        CHECK(u1.draw_vector({0, 0, 16, 16}, unknown.image));
    }
    const auto buffer_allocations = dev.stats.draw_buffer_allocations;
    vector_allocation_count = 0;
    vector_count_allocations = true;
    for (i32 i = 0; i < 5000; ++i)
        CHECK(u1.draw_vector(
            {static_cast<f32>(i % 16 * 8), static_cast<f32>(i / 16 % 16 * 8), 8, 8}, image.image));
    c1->painter->flush();
    vector_count_allocations = false;
    CHECK(vector_allocation_count == 0 && dev.stats.draw_buffer_allocations == buffer_allocations);
    end_frame(c1);
    const u64 recorded = asset->last_use;
    CHECK(recorded > 0 && dev.paths->pending.empty());
    auto borrowed = image.image;
    destroy_vector(image.image);
    CHECK(borrowed->references.load() == 1);
    // Rebinding the renderer releases this context's source lease while the
    // other context keeps the immutable asset alive and drawable.
    set_device(c1, base, s1);
    CHECK(static_cast<native_painter *>(c1->painter)->path_bindings == nullptr);
    CHECK(asset->references == 1 && borrowed->references.load() == 1);
    destroy_context(c1);
    CHECK(asset->references == 1);
    begin_frame(c2, *w2, 1.0 / 60, 1.0 / 60);
    base->clear({0, 0, 0, 0});
    CHECK(u2.draw_vector({0, 0, 64, 64}, borrowed));
    end_frame(c2);
    CHECK(u2.release_vector(borrowed));
    CHECK(asset->references == 0);
    dev.paths->collect(asset->last_use - 1);
    CHECK(dev.paths->assets.size() == 1);
    CHECK(dev.wait(dev.next_serial - 1));
    dev.collect();
    CHECK(dev.paths->assets.empty());
    destroy_vector(unknown.image);
    CHECK(!violation_count(c2) && dev.stats.validation_errors == 0);
    destroy_context(c2);
    base->destroy_surface(s1);
    base->destroy_surface(s2);
    destroy_vulkan_device(base);
}
static void vector_native_lifecycle_regressions()
{
    vector_native_lifecycle(vulkan_upload_mode::DIRECT);
    vector_native_lifecycle(vulkan_upload_mode::STAGED);
    if (!g_failures)
        std::puts("all native vector lifetime regressions passed (shared contexts, retirement, "
                  "10000 allocation-free instances)");
}
