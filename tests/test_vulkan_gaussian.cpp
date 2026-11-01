// Independent full-image/filter-mask oracles for the native Gaussian composite.
// Reuse fixture/CPU helpers; the original test body runs in its own executable.
#define main gaussian_original_test_main
#include "test_vulkan_blur.cpp"
#undef main
#include <filesystem>
#include <fstream>

namespace gaussian_test
{
constexpr i32 WIDTH = 193, HEIGHT = 145;
struct options
{
    const char *capture_controls = nullptr, *capture_reference = nullptr;
    const char *capture_actual = nullptr, *goldens = nullptr, *compatibility = nullptr;
};
bool image_file(const char *directory, const char *prefix, i32 number, std::vector<u8> &pixels,
                bool write)
{
    char name[64];
    std::snprintf(name, sizeof(name), "%s-%02d.bmp", prefix, number);
    const auto path = std::filesystem::path(directory) / name;
    if (write)
    {
        CHECK(!std::filesystem::exists(path));
        std::ofstream file(path, std::ios::binary);
        const auto word = [&](u32 value, i32 bytes)
        {
            for (i32 i = 0; i < bytes; ++i) file.put(static_cast<char>(value >> (i * 8)));
        };
        word(0x4d42, 2);
        word(54 + WIDTH * HEIGHT * 4, 4);
        word(0, 4);
        word(54, 4);
        word(40, 4);
        word(WIDTH, 4);
        word(static_cast<u32>(-HEIGHT), 4);
        word(1, 2);
        word(32, 2);
        word(0, 4);
        word(WIDTH * HEIGHT * 4, 4);
        for (i32 i = 0; i < 4; ++i) word(0, 4);
        for (usize i = 0; i < pixels.size(); i += 4)
        {
            file.put(static_cast<char>(pixels[i + 2]));
            file.put(static_cast<char>(pixels[i + 1]));
            file.put(static_cast<char>(pixels[i]));
            file.put(static_cast<char>(pixels[i + 3]));
        }
        CHECK(file.good());
        return true;
    }
    std::ifstream file(path, std::ios::binary);
    const auto word = [&](i32 bytes)
    {
        u32 value = 0;
        for (i32 i = 0; i < bytes; ++i)
            value |= static_cast<u32>(static_cast<u8>(file.get())) << (i * 8);
        return value;
    };
    CHECK(word(2) == 0x4d42 && word(4) == 54 + WIDTH * HEIGHT * 4);
    CHECK(word(4) == 0 && word(4) == 54 && word(4) == 40);
    CHECK(word(4) == WIDTH && word(4) == static_cast<u32>(-HEIGHT));
    CHECK(word(2) == 1 && word(2) == 32 && word(4) == 0);
    file.seekg(54);
    pixels.resize(WIDTH * HEIGHT * 4);
    for (usize i = 0; i < pixels.size(); i += 4)
    {
        pixels[i + 2] = static_cast<u8>(file.get());
        pixels[i + 1] = static_cast<u8>(file.get());
        pixels[i] = static_cast<u8>(file.get());
        pixels[i + 3] = static_cast<u8>(file.get());
    }
    CHECK(file.good() && file.peek() == std::char_traits<char>::eof());
    return true;
}
bool equal(const std::vector<u8> &actual, const std::vector<u8> &expected, i32 case_id,
           const char *phase)
{
    if (actual == expected) return true;
    for (usize i = 0; i < actual.size(); ++i)
        if (actual[i] != expected[i])
        {
            std::fprintf(stderr, "%s case %d mismatch at (%zu,%zu) channel %zu: %u != %u\n", phase,
                         case_id, i / 4 % WIDTH, i / 4 / WIDTH, i % 4, actual[i], expected[i]);
            return false;
        }
    return false;
}
bool coefficients()
{
    for (f32 radius : {0.5f, 1.0f, 2.5f, 8.0f, 8.01f, 14.999f, 15.0f})
    {
        const auto plan = vk_detail::plan_blur(radius);
        CHECK(plan.gaussian);
        const f64 sigma = std::max(0.5, static_cast<f64>(radius) / 3);
        const i32 support = static_cast<i32>(std::ceil(radius));
        f64 weights[17]{1};
        f64 sum = 1;
        for (i32 i = 1; i <= support; ++i)
        {
            weights[i] = std::exp(-i * i / (2 * sigma * sigma));
            sum += 2 * weights[i];
        }
        CHECK(std::abs(plan.taps.weights[0] - 1 / sum) < 0.000001);
        for (i32 i = 0; i < static_cast<i32>(sizeof(plan.taps.offsets) / sizeof(f32)); ++i)
        {
            const f64 fraction = plan.taps.offsets[i] - (2 * i + 1);
            CHECK(std::abs(plan.taps.weights[i + 1] * (1 - fraction) - weights[2 * i + 1] / sum) <
                  0.000001);
            CHECK(std::abs(plan.taps.weights[i + 1] * fraction - weights[2 * i + 2] / sum) <
                  0.000001);
        }
    }
    CHECK(!vk_detail::plan_blur(std::nextafter(15.0f, 16.0f)).gaussian);
    return true;
}
void post_draw(context *ctx, texture_handle texture)
{
    const f32 s = ctx->render_scale;
    const rect r{(WIDTH - 27.0f) / s, (HEIGHT - 15.0f) / s, 23 / s, 11 / s};
    const color white{255, 255, 255, 255};
    const vertex v[] = {{r.x, r.y, 0, 0, white},
                        {r.right(), r.y, 1, 0, white},
                        {r.right(), r.bottom(), 1, 1, white},
                        {r.x, r.bottom(), 0, 1, white}};
    const i32 idx[] = {0, 1, 2, 0, 2, 3};
    detail::dl_prepare(ctx, texture, nullptr);
    detail::dl_add(ctx, v, 4, idx, 6);
    detail::dl_flush(ctx);
}
void composite(context *ctx, const paint_blur &p, texture_handle texture)
{
    const rect *clip = ctx->clip_depth ? &ctx->clip_stack[ctx->clip_depth - 1] : nullptr;
    detail::dl_prepare(ctx, texture, clip);
    auto &v = ctx->dl->scratch_v;
    auto &idx = ctx->dl->scratch_i;
    v.clear();
    idx.clear();
    const color tint{255, 255, 255, static_cast<u8>(p.alpha * 255)};
    detail::build_rounded_fan(
        p.area, detail::clamp_radii(p.area, p.radii), tint,
        [&](f32 x, f32 y, f32 &u, f32 &vv)
        {
            u = x * ctx->render_scale / WIDTH;
            vv = y * ctx->render_scale / HEIGHT;
        },
        v, idx);
    detail::dl_add(ctx, v.data(), static_cast<i32>(v.size()), idx.data(),
                   static_cast<i32>(idx.size()));
    detail::dl_flush(ctx);
}
bool full_filter(vk_detail::device &d, vk_detail::texture *source, vk_detail::texture *a,
                 vk_detail::texture *b, f32 radius)
{
    const auto plan = vk_detail::plan_blur(radius);
    CHECK(plan.gaussian);
    auto p = plan.taps;
    const rect area{0, 0, WIDTH, HEIGHT};
    p.step[0] = 1.0f / WIDTH;
    d.filter(source, area, a, area, vk_detail::blur_kernel::GAUSSIAN, p, area);
    p.step[0] = 0;
    p.step[1] = 1.0f / HEIGHT;
    d.filter(a, area, b, area, vk_detail::blur_kernel::GAUSSIAN, p, area);
    return true;
}
struct fixture
{
    f32 scale = 1, radius = 1, opacity = 1;
    bool transparent = false;
    rect area{};
};
bool run(vk_detail::device &d, const options &args)
{
    view actual[2], reference;
    CHECK(actual[0].initialize(&d, WIDTH, HEIGHT) && actual[1].initialize(&d, WIDTH, HEIGHT));
    CHECK(reference.initialize(&d, WIDTH, HEIGHT));
    const u8 sample[] = {37, 83, 131, 117, 241, 19, 73, 63, 17, 229, 53, 191, 109, 47, 233, 151};
    const auto post = d.create_texture(2, 2, sample);
    CHECK(post);
    if (args.capture_controls || args.compatibility)
    {
        const auto font = load_font(actual[0].ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        CHECK(font != FONT_INVALID);
        std::vector<u8> pixels(WIDTH * HEIGHT * 4), expected;
        i32 id = 0;
        for (f32 scale : {1.0f, 1.5f, 2.0f, 3.0f})
            for (i32 mode = 0; mode < 2; ++mode, ++id)
            {
                auto &v = actual[0];
                set_window_scale(*v.win, scale);
                for (i32 frame = 0; frame < 4; ++frame)
                {
                    vulkan_set_cache(&d, false);
                    begin_frame(v.ctx, *v.win, frame / 60.0, 1.0 / 60.0);
                    {
                        ui u(v.ctx);
                        for (i32 y = 0; y < HEIGHT; y += 9)
                            for (i32 x = 0; x < WIDTH; x += 11)
                                u.draw_rect({x / scale, y / scale, 11 / scale, 9 / scale},
                                            {static_cast<u8>(x * 7), static_cast<u8>(y * 13),
                                             static_cast<u8>(x + y * 3), 255});
                        u.blur({8 / scale, 7 / scale, 145 / scale, 104 / scale},
                               (mode ? 18.0f : 4.0f) / scale, mode ? 5.0f : 0.0f, 0.625f);
                        auto style = u.text_style(12 / scale, font);
                        u.text({4 / scale, 122 / scale, 160 / scale, 19 / scale}, "Gaussian AV 123",
                               {237, 211, 179, 211});
                        post_draw(v.ctx, post);
                    }
                    end_frame(v.ctx);
                    CHECK(d.read_pixels(v.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                        pixels.data()));
                    if (args.compatibility)
                    {
                        CHECK(image_file(args.compatibility, "control", id, expected, false));
                        CHECK(equal(pixels, expected, id, "compatibility"));
                    }
                }
                if (args.capture_controls)
                    CHECK(image_file(args.capture_controls, "control", id, pixels, true));
            }
        d.destroy_texture(post);
        std::printf("Gaussian compatibility: %d immutable control images matched/captured\n", id);
        return true;
    }
    CHECK(coefficients());
    auto *a = static_cast<vk_detail::texture *>(d.create_target(WIDTH, HEIGHT));
    auto *b = static_cast<vk_detail::texture *>(d.create_target(WIDTH, HEIGHT));
    CHECK(a && b);
    std::vector<fixture> cases;
    for (f32 scale : {1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f})
    {
        cases.push_back({scale, 0.5f, 1, false, {10, 8, 30, 24}});
        cases.push_back({scale, 15 / scale, 1, false, {6, 5, 26, 20}});
        for (f32 radius : {8.0f, 15.0f})
            for (f32 opacity : {1.0f, 0.625f})
                cases.push_back(
                    {scale, radius / scale, opacity, true, {0, 0, WIDTH / scale, HEIGHT / scale}});
    }
    std::vector<u8> input(WIDTH * HEIGHT * 4), expected(input.size()), pixels(input.size()), golden;
    i32 pairs = 0;
    u64 hits = 0;
    for (i32 id = 0; id < static_cast<i32>(cases.size()); ++id)
    {
        const auto &f = cases[id];
        const f32 radius = f.radius * f.scale;
        const i32 cut = static_cast<i32>(std::ceil(f.area.bottom() * f.scale) + std::ceil(radius));
        for (i32 y = 0; y < HEIGHT; ++y)
            for (i32 x = 0; x < WIDTH; ++x)
            {
                const usize at = static_cast<usize>((y * WIDTH + x) * 4);
                for (i32 c = 0; c < 4; ++c)
                    input[at + c] = static_cast<u8>(
                        f.transparent ? (x < WIDTH / 2 && (c == 2 || c == 3) ? 255 : 0)
                                      : (c == 3 || y >= cut ? 255 : 0));
            }
        auto *source =
            static_cast<vk_detail::texture *>(d.create_texture(WIDTH, HEIGHT, input.data()));
        CHECK(source);
        vulkan_set_cache(&d, false);
        CHECK(full_filter(d, source, a, b, radius));
        texture_handle straight = b;
        if (f.transparent)
        {
            CHECK(d.read_pixels(b, 0, 0, WIDTH, HEIGHT, pixels.data()));
            // Pure blue remains pure blue at every nonzero coverage. Prepare
            // an independent straight-alpha texture for ordinary SRC_ALPHA
            // drawing; never use the native blur composite for the oracle.
            for (usize at = 0; at < pixels.size(); at += 4)
            {
                CHECK(pixels[at] == 0 && pixels[at + 1] == 0 && pixels[at + 2] == pixels[at + 3]);
                pixels[at + 2] = 255;
            }
            straight = d.create_texture(WIDTH, HEIGHT, pixels.data());
            CHECK(straight);
        }
        const paint_blur blur{f.area, f.radius, corner_radii::all(5), f.opacity};
        auto render = [&](view &v, bool oracle, bool cache, i32 frame)
        {
            vulkan_set_cache(&d, cache);
            set_window_scale(*v.win, f.scale);
            begin_frame(v.ctx, *v.win, frame / 60.0, 1.0 / 60.0);
            if (f.transparent)
            {
                d.clear({0, 0, 0, 0});
                ui u(v.ctx);
                u.draw_rect({0, 0, (WIDTH / 2) / f.scale, HEIGHT / f.scale}, {0, 0, 255, 255});
                v.ctx->painter->flush();
            }
            else
                d.blit(source, nullptr, v.surface->scene_target(), nullptr);
            if (oracle)
                composite(v.ctx, blur, straight);
            else
            {
                ui u(v.ctx);
                u.blur(blur.area, blur.radius, blur.radii, blur.alpha);
            }
            post_draw(v.ctx, post);
            end_frame(v.ctx);
            return violation_count(v.ctx) == 0 && vulkan_last_result(&d) == VK_SUCCESS;
        };
        CHECK(render(reference, true, false, id));
        CHECK(
            d.read_pixels(reference.surface->scene_target(), 0, 0, WIDTH, HEIGHT, expected.data()));
        if (args.capture_reference)
            CHECK(image_file(args.capture_reference, "reference", id, expected, true));
        else
        {
            if (args.goldens)
            {
                CHECK(image_file(args.goldens, "reference", id, golden, false));
                CHECK(equal(expected, golden, id, "independent frozen reference"));
            }
            for (i32 variant = 0; variant < 2; ++variant)
                for (i32 repeat = 0; repeat < 4; ++repeat)
                {
                    const auto before = vulkan_statistics(&d);
                    CHECK(render(actual[variant], false, variant == 1, id * 4 + repeat));
                    CHECK(d.read_pixels(actual[variant].surface->scene_target(), 0, 0, WIDTH,
                                        HEIGHT, pixels.data()));
                    CHECK(equal(pixels, expected, id, "native/full-image reference"));
                    if (args.goldens) CHECK(equal(pixels, golden, id, "native/frozen golden"));
                    if (variant == 1 && repeat >= 2)
                    {
                        CHECK(vulkan_statistics(&d).cache_hits > before.cache_hits);
                        CHECK(vulkan_statistics(&d).draws == before.draws);
                        ++hits;
                    }
                    if (args.capture_actual && variant == 0 && repeat == 0)
                        CHECK(image_file(args.capture_actual, "actual", id, pixels, true));
                    ++pairs;
                }
        }
        if (f.transparent) d.destroy_texture(straight);
        d.destroy_texture(source);
    }
    d.destroy_target(a);
    d.destroy_target(b);
    d.destroy_texture(post);
    std::printf(
        "Gaussian fixes: cases=%zu exact_pairs=%d idle_hits=%llu DPI=1,1.25,1.5,2,3,4 limit=15\n",
        cases.size(), pairs, static_cast<unsigned long long>(hits));
    return true;
}
} // namespace gaussian_test
int main(int argc, char **argv)
{
    gaussian_test::options args;
    bool staged = false;
    for (i32 i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--staged"))
            staged = true;
        else if (i + 1 < argc && !std::strcmp(argv[i], "--capture-controls"))
            args.capture_controls = argv[++i];
        else if (i + 1 < argc && !std::strcmp(argv[i], "--capture-reference"))
            args.capture_reference = argv[++i];
        else if (i + 1 < argc && !std::strcmp(argv[i], "--capture-actual"))
            args.capture_actual = argv[++i];
        else if (i + 1 < argc && !std::strcmp(argv[i], "--goldens"))
            args.goldens = argv[++i];
        else if (i + 1 < argc && !std::strcmp(argv[i], "--compatibility"))
            args.compatibility = argv[++i];
        else
            return 2;
    }
    for (const char *directory :
         {args.capture_controls, args.capture_reference, args.capture_actual})
        if (directory && !std::filesystem::create_directory(directory)) return 2;
    vulkan_device_opts options;
    options.validation = true;
    options.error_callback = diagnostic;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    auto *d = static_cast<vk_detail::device *>(create_vulkan_device(options));
    if (!d) return 1;
    const bool result = gaussian_test::run(*d, args) && vulkan_wait_idle(d) &&
                        !vulkan_statistics(d).validation_errors;
    destroy_vulkan_device(d);
    return result && !errors ? 0 : 1;
}
