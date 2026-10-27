// Invisible rectangles must stop before semantic capture/preparation, while
// touching and partially clipped rectangles retain the established stream.
namespace
{
struct rect_capture_probe final : paint_detail::primitive_sink
{
    i32 entered = 0, left = 0, meshes = 0;
    void enter(const paint_detail::primitive_input &) override { ++entered; }
    void leave() override { ++left; }
    void mesh(texture_handle, const vertex *, i32, const i32 *, i32) override { ++meshes; }
};
} // namespace

PUI_TEST(rectangles_reject_invisible_work_before_primitive_preparation)
{
    null_device device;
    context *ctx = create_context(&device, device.create_surface());
    rect_capture_probe probe;
    begin_frame(ctx, 0, 1.0 / 60.0, {0, 0, 320, 240});
    auto *previous = ctx->dl->painter;
    ctx->dl->painter = &probe;
    {
        ui u(ctx);
        auto clip = u.region({10, 10, 100, 80}, "rect_prepare_clip"_id);
        for (i32 i = 0; i < 10000; ++i)
            u.draw_rect({10, 500.0f + static_cast<f32>(i), 1.5f, 1.5f}, color::white());
        u.draw_rect({8.5f, 20, 1.5f, 1.5f}, color::white()); // touches the clip only
        u.draw_rect({20, 8.5f, 1.5f, 1.5f}, color::white());
        CHECK(probe.entered == 0 && probe.left == 0 && probe.meshes == 0);
        u.draw_rect({9.75f, 20, 1.5f, 1.5f}, color::white());
        u.draw_rect({20, 9.75f, 1.5f, 1.5f}, color::white());
        CHECK(probe.entered == 2 && probe.left == 2 && probe.meshes == 2);
    }
    ctx->dl->painter = previous;
    end_frame(ctx);
    CHECK(!violation_count(ctx));
    destroy_context(ctx);
}
