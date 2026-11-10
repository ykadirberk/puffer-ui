#include <pufferui/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace pui;

namespace
{
constexpr i32 WIDTH = 160, HEIGHT = 112;
constexpr rect SHAPE{4.25f, 3.5f, 21.25f, 18.25f};
constexpr corner_radii RADII{7, 2, 5, 0};

i32 red_oracle(i32 x, i32 y, f32 scale)
{
    const double px = (x + .5) / scale, py = (y + .5) / scale;
    const bool left = px < SHAPE.center_x(), top = py < SHAPE.center_y();
    const double dx = left ? px - SHAPE.x : SHAPE.right() - px;
    const double dy = top ? py - SHAPE.y : SHAPE.bottom() - py;
    const double radius = top ? (left ? RADII.tl : RADII.tr) : (left ? RADII.bl : RADII.br);
    const double distance = dx < radius && dy < radius
                                ? std::hypot(radius - dx, radius - dy) - radius
                                : -std::min(dx, dy);
    return static_cast<i32>(std::lround(255 * std::clamp(.5 - distance * scale / 1.25, 0., 1.)));
}

bool check_coverage(const std::vector<u8> &pixels, f32 scale)
{
    const auto channel = [&](i32 x, i32 y, i32 c) { return pixels[(y * WIDTH + x) * 4 + c]; };
    i32 feather = 0;
    for (i32 y = 0; y < HEIGHT; ++y)
        for (i32 x = 0; x < static_cast<i32>(27 * scale); ++x)
        {
            const i32 expected = red_oracle(x, y, scale);
            const i32 actual = channel(x, y, 0);
            if (std::abs(actual - expected) > 1 || channel(x, y, 1) || channel(x, y, 2))
            {
                std::fprintf(stderr, "rounded coverage scale %.2f (%d,%d): %d != %d\n", scale, x, y,
                             actual, expected);
                return false;
            }
            feather += actual > 0 && actual < 255;
        }
    const i32 x = static_cast<i32>(32 * scale);
    const i32 gradient_y = static_cast<i32>(9 * scale);
    const i32 foreground_y = static_cast<i32>(15 * scale);
    const i32 clipped_x = static_cast<i32>(35 * scale);
    // 50% green over a 50% green underlay yields 75% green in the interior.
    return feather > 10 && std::abs(channel(x, gradient_y, 1) - 192) <= 1 &&
           channel(x, foreground_y, 2) == 255 && channel(x, foreground_y, 1) == 0 &&
           channel(clipped_x, gradient_y, 1) == 0;
}

bool check_square_gradient(render_device *device, render_surface *surface, context *ctx,
                           window &win, std::vector<u8> &reference, std::vector<u8> &actual)
{
    const rect area{15.25f, 13.75f, 31.5f, 22.25f};
    const color tl{15, 33, 201, 65}, tr{87, 159, 20, 163};
    const color br{150, 19, 102, 228}, bl{71, 221, 69, 34};
    const color mid =
        theme_lerp_color(theme_lerp_color(tl, tr, .5f), theme_lerp_color(bl, br, .5f), .5f);
    const vertex corners[] = {{area.x, area.y, 0, 0, tl},
                              {area.right(), area.y, 0, 0, tr},
                              {area.right(), area.bottom(), 0, 0, br},
                              {area.x, area.bottom(), 0, 0, bl}};
    // The original fan submitted seven coincident vertices for each square corner.
    vertex fan[29]{{area.center_x(), area.center_y(), 0, 0, mid}};
    i32 indices[84];
    for (i32 i = 0; i < 28; ++i)
    {
        fan[i + 1] = corners[i / 7];
        indices[3 * i] = 0;
        indices[3 * i + 1] = i + 1;
        indices[3 * i + 2] = (i + 1) % 28 + 1;
    }
    const auto draw = [&](bool compact, f32 radius, std::vector<u8> &pixels)
    {
        begin_frame(ctx, win, 1, 1.0 / 60.0);
        const auto before = vulkan_statistics(device);
        {
            ui u(ctx);
            auto clip = u.region({19, 14, 24, 18}, "square_gradient_clip"_id);
            if (compact)
                u.draw_rounded_gradient({area, radius, tl, tr, br, bl});
            else
            {
                u.draw_rounded_rect(area, mid, 0);
                u.draw_triangles(nullptr, fan, 29, indices, 84);
            }
        }
        end_frame(ctx);
        const auto after = vulkan_statistics(device);
        return after.requested_vertices - before.requested_vertices == (compact ? 9u : 33u) &&
               after.requested_indices - before.requested_indices == (compact ? 18u : 90u) &&
               device->read_pixels(surface->scene_target(), 0, 0, WIDTH, HEIGHT, pixels.data());
    };
    vulkan_set_cache(device, false);
    for (f32 scale : {1.0f, 2.0f, 4.0f})
    {
        set_window_scale(win, scale);
        if (!draw(false, 0, reference) || !draw(true, 0, actual) || actual != reference ||
            !draw(true, -2, actual) || actual != reference)
        {
            std::fprintf(stderr, "square gradient geometry or pixels differ at %.1fx\n", scale);
            return false;
        }
    }
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    const bool staged = argc == 2 && std::strcmp(argv[1], "--staged") == 0;
    if (argc != 1 && !staged) return 2;
    vulkan_device_opts opts{.validation = true,
                            .upload_mode =
                                staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT,
                            .analytic_rounded_rects = true};
    render_device *device = create_vulkan_device(opts);
    if (!device) return 1;
    render_surface *surface = create_vulkan_surface(device, {.width = WIDTH, .height = HEIGHT});
    context *ctx = surface ? create_context(device, surface) : nullptr;
    window *win = ctx ? add_window(ctx, nullptr, surface, {0, 0, WIDTH, HEIGHT}) : nullptr;
    bool okay = win != nullptr;
    if (win)
    {
        theme t = default_dark();
        t.bg = color::black();
        set_theme(ctx, t);
        std::vector<u8> reference(WIDTH * HEIGHT * 4), cached(reference.size());
        for (f32 scale : {1.0f, 1.5f, 2.0f, 4.0f})
        {
            set_window_scale(*win, scale);
            const auto draw = [&](bool cache, std::vector<u8> &pixels)
            {
                vulkan_set_cache(device, cache);
                const auto before = vulkan_statistics(device);
                begin_frame(ctx, *win, 0, 1.0 / 60.0);
                {
                    ui u(ctx);
                    u.draw_rounded_rect(SHAPE, {255, 0, 0, 255}, RADII);
                    auto clipped = u.region({28, 1, 6, 24}, "round_clip"_id);
                    const color green{0, 255, 0, 128};
                    u.draw_rounded_gradient(
                        {{30.25f, 4, 6.5f, 15}, 2.5f, green, green, green, green});
                    u.draw_rect({31, 14, 3, 3}, {0, 0, 255, 255});
                }
                end_frame(ctx);
                const auto after = vulkan_statistics(device);
                return after.rounded_instances - before.rounded_instances == 2 &&
                       device->read_pixels(surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                           pixels.data());
            };
            okay &= draw(false, reference) && check_coverage(reference, scale) &&
                    draw(true, cached) && reference == cached;
        }

        // An empty clip produces no paint. More than one batch of visible shapes
        // stays bounded and preserves the following solid paint's order.
        set_window_scale(*win, 1);
        const auto before = vulkan_statistics(device);
        begin_frame(ctx, *win, .5, 1.0 / 60.0);
        {
            ui u(ctx);
            {
                auto empty = u.region({0, 0, 0, 0}, "empty_round_clip"_id);
                u.draw_rounded_rect(SHAPE, {255, 0, 0, 255}, RADII);
            }
            for (i32 i = 0; i < 600; ++i) u.draw_rounded_rect({4, 4, 6, 6}, {255, 0, 0, 255}, 2);
            u.draw_rect({4, 4, 6, 6}, {0, 0, 255, 255});
        }
        end_frame(ctx);
        const auto after = vulkan_statistics(device);
        okay &= after.rounded_instances - before.rounded_instances == 600 &&
                after.rounded_draws - before.rounded_draws == 3 &&
                device->read_pixels(surface->scene_target(), 0, 0, WIDTH, HEIGHT, cached.data());
        const auto center = (7 * WIDTH + 7) * 4;
        okay &= cached[center] == 0 && cached[center + 2] == 255 && violation_count(ctx) == 0;
        okay &= check_square_gradient(device, surface, ctx, *win, reference, cached);
    }
    okay &= vulkan_wait_idle(device) && vulkan_last_result(device) == VK_SUCCESS &&
            vulkan_statistics(device).validation_errors == 0;
    if (ctx) destroy_context(ctx);
    destroy_vulkan_device(device);
    if (!okay) return 1;
    std::puts(
        "Vulkan rounded coverage, gradient alpha, clipping, batch bounds and ordering passed");
    return 0;
}
