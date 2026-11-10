#include "test_util.h"
#include "../examples/showcase_icons.h"

PUI_TEST(showcase_svg_icons_prepare_once_and_reuse_color_and_rotation)
{
    using example_assets::showcase_icon_set;
    using enum example_assets::icon_kind;
    constexpr color blue{82, 191, 255, 231}, rose{248, 128, 132, 153};
    constexpr f32 extent = 22.0f;

    vertex_log_device device;
    context *ctx = create_context(&device, device.create_surface());
    set_violation_handler(ctx, capture_violation, nullptr);
    g_violation_events = 0;
    ui u(ctx);
    showcase_icon_set icons;
    CHECK(!icons.draw(u, ICON_GEAR, {30, 30}, extent, blue));

    const i32 base_textures = device.textures;
    icons.prepare(u);
    CHECK(device.textures == base_textures + ICON_COUNT);
    for (const auto image : icons.images)
    {
        const rect box = vector_view_box(image);
        CHECK(box.x == 0 && box.y == 0 && box.w == 24 && box.h == 24);
    }
    icons.prepare(u);
    CHECK(device.textures == base_textures + ICON_COUNT);

    tf_frame(ctx, 0,
             [&](ui &frame)
             {
                 for (i32 kind = 0; kind < ICON_COUNT; ++kind)
                 {
                     const vec2 center{30.0f + 48.0f * (kind % 5), 30.0f + 48.0f * (kind / 5)};
                     CHECK(icons.draw(frame, kind, center, extent, blue));
                 }
             });
    CHECK(device.log.size() == ICON_COUNT * 4);
    for (const vertex &v : device.log)
        CHECK(v.c.r == blue.r && v.c.g == blue.g && v.c.b == blue.b && v.c.a == blue.a);

    device.log.clear();
    tf_frame(ctx, .016,
             [&](ui &frame)
             {
                 CHECK(!icons.draw(frame, -1, {30, 30}, extent, rose));
                 CHECK(!icons.draw(frame, ICON_COUNT, {30, 30}, extent, rose));
                 CHECK(icons.draw(frame, ICON_CHEVRON, {30, 30}, extent, rose, 1.57079632679f));
             });
    CHECK(device.log.size() == 4);
    if (device.log.size() == 4)
    {
        const vertex &first = device.log.front();
        CHECK(std::fabs(first.x - 41) < .001f && std::fabs(first.y - 19) < .001f);
        CHECK(first.c.r == rose.r && first.c.g == rose.g && first.c.b == rose.b &&
              first.c.a == rose.a);
    }
    CHECK(device.textures == base_textures + ICON_COUNT);
    icons.release(u);
    CHECK(!icons.draw(u, ICON_CHEVRON, {30, 30}, extent, rose));
    for (const auto image : icons.images) CHECK(!image);
    CHECK(violation_count(ctx) == 0 && g_violation_events == 0);
    destroy_context(ctx);
}
