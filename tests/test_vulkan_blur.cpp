// Independent CPU convolution and actual native painter coverage. Existing
// Vulkan/cache fixtures and SDL golden files stay unchanged.
#define PUFFERUI_IMPLEMENTATION
#include <pufferui/vulkan.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
using namespace pui;
namespace
{
i32 errors = 0;
void diagnostic(i32 code, const char *text, void *)
{
    std::fprintf(stderr, "Vulkan blur %d: %s\n", code, text);
    ++errors;
}
#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            std::fprintf(stderr, "blur check failed at %d: %s\n", __LINE__, #condition);           \
            return false;                                                                          \
        }                                                                                          \
    } while (false)
bool plans()
{
    for (f32 radius : {0.5f, 1.0f, 2.5f, 7.999f, 8.0f, 14.999f, 15.0f, std::nextafter(15.0f, 16.0f),
                       16.0f, 32.0f, 64.0f, 512.0f})
    {
        const auto plan = vk_detail::plan_blur(radius);
        CHECK(plan.gaussian == (radius <= 15));
        if (plan.gaussian)
        {
            f32 sum = plan.taps.weights[0];
            const f64 sigma = std::max(0.5, static_cast<f64>(radius) / 3);
            f64 reference_sum = 1;
            for (i32 i = 1; i <= static_cast<i32>(std::ceil(radius)); ++i)
                reference_sum += 2 * std::exp(-i * i / (2 * sigma * sigma));
            CHECK(std::abs(plan.taps.weights[0] - 1 / reference_sum) < 0.000001);
            for (i32 i = 0; i < vk_detail::GAUSSIAN_PAIR_COUNT; ++i)
            {
                sum += 2 * plan.taps.weights[i + 1];
                if (plan.taps.weights[i + 1] > 0)
                    CHECK(plan.taps.offsets[i] >= 2 * i + 1 && plan.taps.offsets[i] <= 2 * i + 2);
            }
            CHECK(std::abs(sum - 1) < 0.000001f);
        }
        else
            CHECK(plan.levels >= 2 && plan.levels <= 6 && plan.offset > 0);
    }
    return true;
}
using rgba = std::array<f64, 4>;
struct cpu_image
{
    i32 width, height;
    std::vector<u8> pixels;
    cpu_image(i32 w, i32 h) : width(w), height(h), pixels(static_cast<usize>(w * h * 4)) {}
    rgba sample(f64 x, f64 y) const
    {
        x = std::clamp(x, 0.0, static_cast<f64>(width - 1));
        y = std::clamp(y, 0.0, static_cast<f64>(height - 1));
        const i32 x0 = static_cast<i32>(x), y0 = static_cast<i32>(y);
        const i32 x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
        const f64 dx = x - x0, dy = y - y0;
        rgba result{};
        for (i32 c = 0; c < 4; ++c)
            result[c] = (pixels[(y0 * width + x0) * 4 + c] * (1 - dx) +
                         pixels[(y0 * width + x1) * 4 + c] * dx) *
                            (1 - dy) +
                        (pixels[(y1 * width + x0) * 4 + c] * (1 - dx) +
                         pixels[(y1 * width + x1) * 4 + c] * dx) *
                            dy;
        return result;
    }
};
cpu_image convolution(const cpu_image &source, i32 width, i32 height, vk_detail::blur_kernel kernel,
                      f32 radius, bool horizontal)
{
    cpu_image result(width, height);
    for (i32 y = 0; y < height; ++y)
        for (i32 x = 0; x < width; ++x)
        {
            const f64 sx = (x + 0.5) * source.width / width - 0.5;
            const f64 sy = (y + 0.5) * source.height / height - 0.5;
            rgba sum{};
            f64 total = 0;
            const auto add = [&](f64 dx, f64 dy, f64 weight)
            {
                const auto pixel = source.sample(sx + dx, sy + dy);
                for (i32 c = 0; c < 4; ++c) sum[c] += pixel[c] * weight;
                total += weight;
            };
            if (kernel == vk_detail::blur_kernel::GAUSSIAN)
            {
                const f64 sigma = std::max(0.5, static_cast<f64>(radius) / 3);
                const i32 support = static_cast<i32>(std::ceil(radius));
                for (i32 i = -support; i <= support; ++i)
                    add(horizontal ? i : 0, horizontal ? 0 : i,
                        std::exp(-i * i / (2 * sigma * sigma)));
            }
            else
            {
                const f64 step = radius * 0.5;
                if (kernel == vk_detail::blur_kernel::KAWASE_DOWN)
                {
                    add(0, 0, 4);
                    for (i32 dy : {-1, 1})
                        for (i32 dx : {-1, 1}) add(dx * step, dy * step, 1);
                }
                else
                {
                    add(-2 * step, 0, 1);
                    add(2 * step, 0, 1);
                    add(0, -2 * step, 1);
                    add(0, 2 * step, 1);
                    for (i32 dy : {-1, 1})
                        for (i32 dx : {-1, 1}) add(dx * step, dy * step, 2);
                }
            }
            for (i32 c = 0; c < 4; ++c)
                result.pixels[(y * width + x) * 4 + c] =
                    static_cast<u8>(std::clamp(std::round(sum[c] / total), 0.0, 255.0));
        }
    return result;
}
bool cpu_oracles(vk_detail::device &d)
{
    cpu_image input(65, 49);
    for (i32 y = 0; y < input.height; ++y)
        for (i32 x = 0; x < input.width; ++x)
            for (i32 c = 0; c < 4; ++c)
                input.pixels[(y * input.width + x) * 4 + c] = static_cast<u8>(
                    c == 3 ? 73 + (x * 3 + y * 5) % 183 : (x * 53 + y * 71 + c * 97) % 256);
    auto *source = static_cast<vk_detail::texture *>(
        d.create_texture(input.width, input.height, input.pixels.data()));
    CHECK(source);
    // Destinations are deliberately larger than their valid regions. A second
    // pass must clamp to written texels rather than read unused/stale padding.
    auto *a = static_cast<vk_detail::texture *>(d.create_target(80, 64));
    auto *b = static_cast<vk_detail::texture *>(d.create_target(80, 64));
    CHECK(a && b);
    for (f32 radius : {0.5f, 2.5f, 8.0f, 8.01f, 15.0f, 17.0f, 48.0f})
    {
        const auto plan = vk_detail::plan_blur(radius);
        cpu_image expected = input;
        vk_detail::texture *sample = source;
        vk_detail::texture *target = a;
        const auto pass =
            [&](i32 w, i32 h, vk_detail::blur_kernel kernel, f32 spacing, bool horizontal)
        {
            vk_detail::blur_filter parameters = plan.taps;
            parameters.step[0] = kernel == vk_detail::blur_kernel::GAUSSIAN
                                     ? (horizontal ? 1.0f / sample->width : 0)
                                     : spacing * 0.5f / sample->width;
            parameters.step[1] = kernel == vk_detail::blur_kernel::GAUSSIAN
                                     ? (horizontal ? 0 : 1.0f / sample->height)
                                     : spacing * 0.5f / sample->height;
            const rect valid{0, 0, static_cast<f32>(expected.width),
                             static_cast<f32>(expected.height)};
            d.filter(sample, valid, target, {0, 0, static_cast<f32>(w), static_cast<f32>(h)},
                     kernel, parameters, valid);
            expected = convolution(expected, w, h, kernel, spacing, horizontal);
            sample = target;
            target = target == a ? b : a;
        };
        if (plan.gaussian)
        {
            pass(65, 49, vk_detail::blur_kernel::GAUSSIAN, radius, true);
            pass(65, 49, vk_detail::blur_kernel::GAUSSIAN, radius, false);
        }
        else
        {
            i32 widths[7]{65}, heights[7]{49};
            for (u32 i = 1; i <= plan.levels; ++i)
            {
                widths[i] = (widths[i - 1] + 1) / 2;
                heights[i] = (heights[i - 1] + 1) / 2;
                pass(widths[i], heights[i], vk_detail::blur_kernel::KAWASE_DOWN, plan.offset,
                     false);
            }
            for (u32 i = plan.levels; i > 0; --i)
                pass(widths[i - 1], heights[i - 1], vk_detail::blur_kernel::KAWASE_UP, plan.offset,
                     false);
        }
        std::vector<u8> actual(input.pixels.size());
        CHECK(d.read_pixels(sample, 0, 0, 65, 49, actual.data()));
        i32 largest = 0;
        for (usize i = 0; i < actual.size(); ++i)
            largest = std::max(largest, std::abs(static_cast<i32>(actual[i]) - expected.pixels[i]));
        if (largest > 2)
        {
            std::fprintf(stderr, "CPU blur mismatch radius %.4f, maximum byte error %d\n", radius,
                         largest);
            return false;
        }
    }
    d.destroy_target(a);
    d.destroy_target(b);
    d.destroy_texture(source);
    return true;
}
struct view
{
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    render_device *device = nullptr;
    bool initialize(render_device *d, i32 w = 129, i32 h = 97)
    {
        device = d;
        surface = create_vulkan_surface(d, {.width = w, .height = h});
        CHECK(surface);
        ctx = create_context(d, surface);
        win = add_window(ctx, nullptr, surface, {0, 0, static_cast<f32>(w), static_cast<f32>(h)});
        CHECK(win);
        return true;
    }
    ~view()
    {
        if (ctx) destroy_context(ctx);
        if (surface) device->destroy_surface(surface);
    }
};
bool public_gaussian(vk_detail::device &d)
{
    view v;
    CHECK(v.initialize(&d, 65, 49));
    cpu_image input(65, 49);
    for (i32 y = 0; y < 49; ++y)
        for (i32 x = 0; x < 65; ++x)
            for (i32 c = 0; c < 4; ++c)
                input.pixels[(y * 65 + x) * 4 + c] =
                    static_cast<u8>(c == 3 ? 255 : (x * 17 + y * 11 + c * 71) % 256);
    const auto image = d.create_texture(65, 49, input.pixels.data());
    CHECK(image);
    std::vector<u8> actual(input.pixels.size());
    for (i32 frame = 0; frame < 4; ++frame)
    {
        const f32 radius = frame % 2 ? 8 : 2.5f;
        begin_frame(v.ctx, *v.win, frame / 60.0, 1.0 / 60.0);
        d.blit(image, nullptr, v.surface->scene_target(), nullptr);
        {
            ui u(v.ctx);
            const rect area = frame < 2 ? rect{18, 12, 30, 20} : rect{0, 0, 65, 49};
            auto clip = u.region(area, "gaussian_oracle_clip"_id);
            // Blur extends beyond the active clip. Only clipped pixels may
            // change, but their samples still include the surrounding scene.
            u.blur({0, 0, 65, 49}, radius);
        }
        end_frame(v.ctx);
        CHECK(d.read_pixels(v.surface->scene_target(), 0, 0, 65, 49, actual.data()));
        const auto horizontal =
            convolution(input, 65, 49, vk_detail::blur_kernel::GAUSSIAN, radius, true);
        const auto expected =
            convolution(horizontal, 65, 49, vk_detail::blur_kernel::GAUSSIAN, radius, false);
        for (i32 y = 0; y < 49; ++y)
            for (i32 x = 0; x < 65; ++x)
                for (i32 c = 0; c < 4; ++c)
                {
                    const usize at = static_cast<usize>((y * 65 + x) * 4 + c);
                    const bool inside = frame >= 2 || (x >= 18 && x < 48 && y >= 12 && y < 32);
                    if (inside)
                        CHECK(std::abs(static_cast<i32>(actual[at]) - expected.pixels[at]) <= 2);
                    else
                        CHECK(actual[at] == input.pixels[at]);
                }
    }
    d.destroy_texture(image);
    return true;
}
bool painter(vk_detail::device &d)
{
    view views[2];
    CHECK(views[0].initialize(&d) && views[1].initialize(&d));
    const f32 radii[] = {0.75f, 4, 14.999f, 15, std::nextafter(15.0f, 16.0f), 15.01f, 16, 25, 64};
    std::vector<u8> pixels[2];
    for (auto &image : pixels) image.resize(129 * 97 * 4);
    for (i32 frame = 0; frame < 96; ++frame)
    {
        for (i32 variant = 0; variant < 2; ++variant)
        {
            auto &v = views[variant];
            vulkan_set_cache(&d, variant == 1);
            const f32 scale = frame >= 48 ? 1.5f : 1.0f;
            set_window_scale(*v.win, scale);
            begin_frame(v.ctx, *v.win, frame / 60.0, 1.0 / 60.0);
            {
                ui u(v.ctx);
                for (i32 y = 0; y < 97; y += 7)
                    for (i32 x = 0; x < 129; x += 9)
                        u.draw_rect({x / scale, y / scale, 9 / scale, 7 / scale},
                                    {static_cast<u8>(x * 31 + frame), static_cast<u8>(y * 19),
                                     static_cast<u8>((x + y) * 23), 255});
                v.ctx->painter->flush();
                u64 prefix_draws = 0;
                if (d.cache_active)
                    for (const auto &event : d.events)
                        if (event.type == vk_detail::cache_event::DRAW) ++prefix_draws;
                const auto before = vulkan_statistics(&d);
                const usize before_events = d.events.size();
                const f32 radius = radii[frame % 9];
                u.blur({0, 0, 129 / scale, 97 / scale}, radius / scale);
                const auto after = vulkan_statistics(&d);
                const auto plan = vk_detail::plan_blur(radius);
                const u64 draws = plan.gaussian ? 3 : 2 * plan.levels + 1;
                if (d.cache_active)
                {
                    CHECK(d.events.size() - before_events == draws);
                    CHECK(after.draws == before.draws);
                }
                else
                    CHECK(after.draws - before.draws == prefix_draws + draws);
                // A different, clipped, rounded, translucent blur immediately
                // reuses the same targets at a smaller valid extent.
                {
                    auto clip = u.region({6.5f, 5.25f, 61.5f, 43.25f}, "blur_clip"_id);
                    u.blur({-3.25f, 3.5f, 91.25f, 51.5f}, frame % 2 ? 8.0f : 18.0f,
                           corner_radii{7, 2, 9, 0}, 0.625f);
                }
                // Empty/outside clips, zero alpha, zero/NaN/infinite radii
                // must not submit passes or grow scratch resources.
                const auto idle = vulkan_statistics(&d);
                u.blur({0, 0, 40, 40}, 8, 0, 0);
                u.blur({0, 0, 40, 40}, 0);
                u.blur({0, 0, 40, 40}, std::numeric_limits<f32>::quiet_NaN());
                u.blur({0, 0, 40, 40}, std::numeric_limits<f32>::infinity());
                u.blur({2000, 2000, 30, 30}, 18);
                {
                    auto clip = u.region({0, 0, 0, 0}, "empty_blur_clip"_id);
                    u.blur({0, 0, 40, 40}, 18);
                }
                CHECK(vulkan_statistics(&d).draws == idle.draws);
                CHECK(vulkan_statistics(&d).live_textures == idle.live_textures);
                if (frame >= 9)
                {
                    auto *scratch = v.ctx->blur;
                    const auto a = scratch->half, b = scratch->quarter;
                    u.blur({17.25f, 9.5f, 33.75f, 21.25f}, 8);
                    u.blur({9.25f, 8.75f, 42.5f, 29.25f}, 24);
                    CHECK(a == scratch->half && b == scratch->quarter);
                    CHECK(vulkan_statistics(&d).live_textures == idle.live_textures);
                }
            }
            end_frame(v.ctx);
            CHECK(violation_count(v.ctx) == 0);
            CHECK(d.read_pixels(v.surface->scene_target(), 0, 0, 129, 97, pixels[variant].data()));
        }
        CHECK(pixels[0] == pixels[1]);
    }
    return true;
}
bool static_cache(vk_detail::device &d)
{
    view views[2];
    CHECK(views[0].initialize(&d) && views[1].initialize(&d));
    std::vector<u8> pixels[2];
    for (auto &image : pixels) image.resize(129 * 97 * 4);
    for (i32 frame = 0; frame < 12; ++frame)
    {
        for (i32 variant = 0; variant < 2; ++variant)
        {
            auto &v = views[variant];
            vulkan_set_cache(&d, variant == 1);
            const auto before = vulkan_statistics(&d);
            begin_frame(v.ctx, *v.win, frame / 60.0, 1.0 / 60.0);
            {
                ui u(v.ctx);
                u.draw_rect({0, 0, 63, 97}, {231, 59, 17, 255});
                u.draw_rect({63, 0, 66, 97}, {13, 117, 239, 255});
                // A parameter/kernel change must invalidate the cached image;
                // both stable Gaussian and stable Kawase must then hit again.
                u.blur({0, 0, 129, 97}, frame < 6 ? 15.0f : 15.01f);
                u.draw_rect({9, 7, 14, 11}, {29, 247, 113, 191});
            }
            end_frame(v.ctx);
            const auto after = vulkan_statistics(&d);
            if (variant == 1 && frame >= 2 && frame != 6)
            {
                CHECK(after.cache_hits == before.cache_hits + 1);
                CHECK(after.draws == before.draws);
            }
            if (variant == 1 && frame == 6) CHECK(after.cache_misses > before.cache_misses);
            CHECK(d.read_pixels(v.surface->scene_target(), 0, 0, 129, 97, pixels[variant].data()));
        }
        CHECK(pixels[0] == pixels[1]);
    }
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
    auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
    if (!d) return 1;
    const bool passed = plans() && cpu_oracles(*d) && public_gaussian(*d) && painter(*d) &&
                        static_cache(*d) && vulkan_wait_idle(d) &&
                        !vulkan_statistics(d).validation_errors;
    destroy_vulkan_device(d);
    if (!passed || errors) return 1;
    std::puts(
        "Vulkan blur: Gaussian <=15px, Dual Kawase >15px, CPU kernels, 108 exact cached/reference "
        "frames, DPI, clips, target reuse and idle cache hits passed");
    return 0;
}
