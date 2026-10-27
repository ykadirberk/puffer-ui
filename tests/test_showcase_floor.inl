// Independent pre-culling arithmetic and stream oracle for the dotted floor.
PUI_TEST(showcase_floor_preserves_exact_stream_at_scroll_and_clip_edges)
{
    geometry_device device;
    context *ctx = create_context(&device, device.create_surface());
    for (rect clip : {rect{10, 10, 120, 70}, rect{130.25f, 0, 0.25f, 200},
                      rect{10, 9.7f, 120, 0.1f}, rect{}, rect{0, 0, 320, 240}})
        for (rect inner : {rect{-0.125f, -500.3125f, 1000, 5000}, rect{15.25f, 12.75f, 90, 280},
                           rect{131, 20, 0.25f, 5000}, rect{10, 10, 500, 1},
                           rect{320.125f, 0, 3.125f, 5000}, rect{-12.5f, -12.25f, 23, 23}})
        {
            u64 expected = 0;
            for (i32 pass = 0; pass < 2; ++pass)
            {
                device.hash = 1469598103934665603ull;
                begin_frame(ctx, 1, 1.0 / 60.0, {0, 0, 320, 240});
                {
                    ui u(ctx);
                    auto area = u.region(clip, "floor_clip"_id);
                    const color tint{153, 172, 219, 30};
                    if (pass == 0)
                    {
                        for (f32 y = inner.y + 12.0f; y < inner.bottom(); y += 24.0f)
                            for (f32 x = inner.x + 12.0f; x < inner.right(); x += 24.0f)
                                u.draw_rect({x, y, 1.5f, 1.5f}, tint);
                    }
                    else
                        lab_floor(u, inner, tint);
                }
                end_frame(ctx);
                if (pass == 0)
                    expected = device.hash;
                else
                    CHECK(device.hash == expected);
            }
        }
    CHECK(!violation_count(ctx));
    destroy_context(ctx);
}

PUI_TEST(showcase_floor_draw_path_keeps_prepared_storage)
{
    geometry_device device;
    context *ctx = create_context(&device, device.create_surface());
    for (i32 frame = 0; frame < 2; ++frame)
    {
        begin_frame(ctx, frame / 60.0, 1.0 / 60.0, {0, 0, 320, 240});
        {
            ui u(ctx);
            auto area = u.region({10, 10, 120, 70}, "floor_storage"_id);
            allocations = 0;
            track_allocations = frame != 0;
            lab_floor(u, {-500.25f, -10000.125f, 1000, 100000}, {153, 172, 219, 30});
            track_allocations = false;
            if (frame) CHECK(allocations == 0);
        }
        end_frame(ctx);
    }
    CHECK(!violation_count(ctx));
    destroy_context(ctx);
}

PUI_TEST(rectangle_early_culling_keeps_the_original_vertex_bounds_and_stream)
{
    geometry_device device;
    context *ctx = create_context(&device, device.create_surface());
    window *win = add_window(ctx, nullptr, &device.my_surface, {0, 0, 320, 240});
    for (f32 scale : {1.0f, 1.25f, 2.0f})
    {
        set_window_scale(*win, scale);
        for (rect clip :
             {rect{10, 10, 120, 70}, rect{0, 0, 320, 240}, rect{}, rect{130.25f, 0, 0.25f, 200}})
            for (rect r : {rect{9.75f, 20, 1.5f, 1.5f}, rect{8.5f, 20, 1.5f, 1.5f},
                           rect{130.125f, 0, 0.25f, 50}, rect{20, 20, 0, 12}, rect{20, 20, 12, 0},
                           rect{15, 15, 0.0000001f, 12}, rect{-1048576.125f, 10, 1048576.25f, 30},
                           rect{15.25f, -1048576.125f, 100, 1048576.25f}})
            {
                u64 expected = 0;
                for (i32 pass = 0; pass < 2; ++pass)
                {
                    device.hash = 1469598103934665603ull;
                    begin_frame(ctx, *win, 1, 1.0 / 60.0);
                    {
                        ui u(ctx);
                        auto area = u.region(clip, "rect_bounds_oracle"_id);
                        const color c{153, 172, 219, 137};
                        if (pass == 0)
                        {
                            const vertex v[] = {{r.x, r.y, 0, 0, c},
                                                {r.right(), r.y, 1, 0, c},
                                                {r.right(), r.bottom(), 1, 1, c},
                                                {r.x, r.bottom(), 0, 1, c}};
                            constexpr i32 indices[] = {0, 1, 2, 0, 2, 3};
                            u.draw_triangles(nullptr, v, 4, indices, 6);
                        }
                        else
                            u.draw_rect(r, c);
                    }
                    end_frame(ctx);
                    if (pass == 0)
                        expected = device.hash;
                    else
                        CHECK(device.hash == expected);
                }
            }
    }
    CHECK(!violation_count(ctx));
    destroy_context(ctx);
}
