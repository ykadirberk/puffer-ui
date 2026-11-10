#include "test_util.h"
#include "../examples/material_icons.h"

namespace
{
struct icon_texture_capture : null_device
{
    static constexpr i32 SIZE = 48;
    std::vector<u8> pixels;
    texture_handle create_texture(i32 w, i32 h, const u8 *rgba) override
    {
        if (rgba && w == SIZE && h == SIZE) pixels.assign(rgba, rgba + w * h * 4);
        return null_device::create_texture(w, h, rgba);
    }
};
} // namespace

PUI_TEST(bundled_material_icons_prepare_two_visible_colors_with_identical_coverage)
{
    constexpr color sky{82, 191, 255, 255}, coral{248, 128, 132, 255};
    constexpr f32 extent = static_cast<f32>(icon_texture_capture::SIZE);
    icon_texture_capture device;
    context *ctx = create_context(&device, device.create_surface());
    set_violation_handler(ctx, capture_violation, nullptr);
    g_violation_events = 0;
    ui u(ctx);
    for (const auto &icon : example_assets::MATERIAL_ICONS)
    {
        auto result = example_assets::load_svg_file(icon.filename);
        CHECK(result);
        if (!result) continue;
        const rect box = vector_view_box(result.image);
        CHECK(box.x == 0 && box.y == 0 && box.w == 24 && box.h == 24);

        const auto prepare = [&](color current)
        {
            device.pixels.clear();
            CHECK(u.prepare_vector(result.image, {.max_pixel_size = {extent, extent},
                                                  .max_instances = 2,
                                                  .style = {.current_color = current}}));
        };
        prepare(sky);
        const auto first = device.pixels;
        prepare(coral);
        const auto &second = device.pixels;
        constexpr usize bytes = icon_texture_capture::SIZE * icon_texture_capture::SIZE * 4;
        CHECK(first.size() == bytes && second.size() == bytes);
        if (first.size() == bytes && second.size() == bytes)
        {
            CHECK(first[3] == 0 && second[3] == 0);
            usize opaque = 0;
            bool same_coverage = true, expected_colors = true;
            for (usize i = 0; i < bytes; i += 4)
            {
                same_coverage &= first[i + 3] == second[i + 3];
                if (first[i + 3] == 255)
                {
                    ++opaque;
                    expected_colors &= first[i] == sky.r && first[i + 1] == sky.g &&
                                       first[i + 2] == sky.b && second[i] == coral.r &&
                                       second[i + 1] == coral.g && second[i + 2] == coral.b;
                }
            }
            CHECK(opaque > 0 && same_coverage && expected_colors);
        }

        const i32 textures = device.textures;
        tf_frame(ctx, 0,
                 [&](ui &frame)
                 {
                     CHECK(frame.draw_vector({4, 4, 16, 16}, result.image,
                                             {.style = {.current_color = sky}}));
                     CHECK(frame.draw_vector({32, 4, extent, extent}, result.image,
                                             {.style = {.current_color = coral}}));
                 });
        CHECK(device.draw_calls == 2);
        CHECK(device.textures == textures);
        CHECK(u.release_vector(result.image));
        destroy_vector(result.image);
    }
    CHECK(violation_count(ctx) == 0 && g_violation_events == 0);
    destroy_context(ctx);
}

PUI_TEST(material_svg_file_loader_reports_missing_source)
{
    const auto missing =
        example_assets::load_svg_file(PUFFERUI_ASSET_DIR "/icons/material/missing.svg");
    CHECK(!missing && missing.error.code == vector_error_code::INVALID_SVG);
}
