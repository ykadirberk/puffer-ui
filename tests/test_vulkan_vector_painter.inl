// Actual semantic UI -> native Slug paths, including isolated SVG composition.
// No bitmap font or text renderer participates in these independent scenes.
static std::vector<std::vector<u8>> vector_painter_pixels(vulkan_upload_mode mode, bool cache)
{
    using namespace vk_detail;
    std::vector<std::vector<u8>> captures;
    auto *base = create_vulkan_device(
        {.validation = true, .tile_cache = cache, .vector_text = false, .upload_mode = mode});
    CHECK(base);
    if (!base) return captures;
    auto &dev = *static_cast<device *>(base);
    auto *surface = create_vulkan_surface(base, {.width = 64, .height = 64});
    CHECK(surface);
    if (!surface)
    {
        destroy_vulkan_device(base);
        return captures;
    }
    auto *ctx = create_context(base, surface);
    auto *win = add_window(ctx, nullptr, surface, {0, 0, 64, 64});
    ui u(ctx);
    const std::string_view svg[] = {
        R"(<svg viewBox="0 0 64 64"><rect x="8" y="8" width="48" height="48" fill="red"/></svg>)",
        R"(<svg viewBox="0 0 64 64"><g opacity=".5"><rect x="8" y="8" width="32" height="48" fill="red"/><rect x="24" y="8" width="32" height="48" fill="red"/></g></svg>)",
        R"svg(<svg viewBox="0 0 64 64"><defs><clipPath id="c"><rect x="8" y="8" width="24" height="48"/><rect x="32" y="8" width="24" height="48"/></clipPath></defs><g clip-path="url(#c)"><rect width="64" height="64" fill="blue"/></g></svg>)svg",
        R"svg(<svg viewBox="0 0 64 64"><defs><clipPath id="c"><path d="M8 8H56V56H8ZM24 24H40V40H24Z" clip-rule="evenodd"/></clipPath></defs><rect width="64" height="64" fill="green" clip-path="url(#c)"/></svg>)svg",
        R"(<svg viewBox="0 0 64 64"><path d="M8 32H56" fill="none" stroke="red" stroke-width="8" stroke-dasharray="8 16" stroke-linecap="round"/></svg>)",
        R"(<svg viewBox="0 0 64 64"><rect x="8" y="8" width="32" height="48" fill="red"/><rect x="24" y="8" width="32" height="48" fill="red"/></svg>)",
        R"(<svg viewBox="0 0 64 64"><rect x="8" y="8" width="48" height="48" fill="red"/></svg>)"};
    std::vector<vector_handle> images;
    for (auto input : svg)
    {
        auto result = load_svg(input);
        CHECK(result);
        if (!result) continue;
        CHECK(u.prepare_vector(result.image));
        images.push_back(result.image);
    }
    CHECK(images.size() == std::size(svg));
    if (images.size() == std::size(svg))
    {
        auto *painter = static_cast<native_painter *>(ctx->painter);
        CHECK(painter->path_bindings && painter->path_bindings->entries.size() == images.size());
        CHECK(painter->vector_images == nullptr && dev.paths && dev.text == nullptr);
        auto *original = painter->path_bindings->get(images[0]);
        CHECK(!u.prepare_vector(images[0], {.max_pixel_size = {8192, 64}}));
        CHECK(painter->path_bindings->get(images[0]) == original);
        for (usize dpi = 0; dpi < 3; ++dpi)
        {
            const f32 scale = static_cast<f32>(1u << dpi);
            set_window_scale(*win, scale);
            auto appearance = default_dark();
            appearance.text_gamma = dpi == 0 ? .75f : dpi == 1 ? 1.0f : 2.2f;
            set_theme(ctx, appearance); // Path coverage is independent of text gamma.
            for (usize scene = 0; scene < images.size(); ++scene)
            {
                const int prior_failures = g_failures;
                begin_frame(ctx, *win, (dpi * images.size() + scene) / 60.0, 1.0 / 60);
                base->clear({0, 0, 0, 0});
                if (scene == 6) u.draw_rect({0, 0, 64 / scale, 64 / scale}, {10, 20, 30, 255});
                CHECK(u.prepare_vector(images[scene]).error.code ==
                      vector_error_code::WRONG_CONTEXT);
                vector_draw_opts options;
                if (scene == 5) options.style.opacity = .5f;
                if (scene == 6)
                {
                    auto clip = u.region({0, 0, 32 / scale, 64 / scale}, "mixed_vector_clip"_id);
                    CHECK(u.draw_vector({0, 0, 64 / scale, 64 / scale}, images[scene], options));
                }
                else
                    CHECK(u.draw_vector({0, 0, 64 / scale, 64 / scale}, images[scene], options));
                if (scene == 6)
                    u.draw_rect({28 / scale, 28 / scale, 8 / scale, 8 / scale}, {0, 255, 0, 255});
                end_frame(ctx);
                std::vector<u8> pixels(64 * 64 * 4);
                CHECK(base->read_pixels(surface->scene_target(), 0, 0, 64, 64, pixels.data()));
                const usize center = (32 * 64 + 32) * 4, single = (32 * 64 + 12) * 4;
                if (scene == 1 || scene == 5)
                {
                    CHECK(pixels[center] == 128 && pixels[center + 3] == 128);
                    CHECK(pixels[single] == 128 && pixels[single + 3] == 128);
                }
                else if (scene == 3)
                {
                    CHECK(pixels[center + 3] == 0);
                    CHECK(pixels[single + 3] == 255);
                }
                else if (scene == 6)
                {
                    CHECK(pixels[0] == 10 && pixels[1] == 20 && pixels[2] == 30);
                    CHECK(pixels[center] == 0 && pixels[center + 1] == 255);
                }
                else
                    CHECK(pixels[center + 3] == 255);
                if (scene == 0) CHECK(pixels[center] == 255 && pixels[center + 1] == 0);
                if (scene == 2) CHECK(pixels[center + 2] == 255);
                if (scene == 4) CHECK(pixels[(32 * 64 + 24) * 4 + 3] == 0);
                CHECK(violation_count(ctx) == 0 && dev.stats.validation_errors == 0 &&
                      dev.text == nullptr);
                if (g_failures != prior_failures)
                    std::fprintf(stderr,
                                 "native vector frame: mode=%u cache=%u dpi=%zu scene=%zu "
                                 "center=(%u,%u,%u,%u) single_alpha=%u render_scale=%g\n",
                                 static_cast<unsigned>(mode), static_cast<unsigned>(cache), dpi,
                                 scene, pixels[center], pixels[center + 1], pixels[center + 2],
                                 pixels[center + 3], pixels[single + 3],
                                 static_cast<double>(ctx->render_scale));
                captures.push_back(std::move(pixels));
            }
        }
        for (usize i = 0; i < images.size(); ++i)
            CHECK(captures[i] == captures[i + images.size()] &&
                  captures[i] == captures[i + images.size() * 2]);
    }
    for (auto &image : images) destroy_vector(image);
    destroy_context(ctx);
    base->destroy_surface(surface);
    dev.collect();
    CHECK(!dev.paths || dev.paths->assets.empty());
    CHECK(!dev.paths || dev.paths->layers.empty());
    CHECK(dev.stats.validation_errors == 0);
    destroy_vulkan_device(base);
    return captures;
}
static bool vector_painter_fixture(const std::vector<std::vector<u8>> &frames,
                                   const char *directory, bool capture)
{
    const std::filesystem::path root(directory);
    if (capture && !std::filesystem::create_directory(root)) return false;
    for (usize i = 0; i < frames.size(); ++i)
    {
        char name[40];
        std::snprintf(name, sizeof(name), "frame-%03zu.rgba", i);
        if (capture)
        {
            std::ofstream file(root / name, std::ios::binary);
            file.write(reinterpret_cast<const char *>(frames[i].data()),
                       static_cast<std::streamsize>(frames[i].size()));
            if (!file) return false;
        }
        else
        {
            std::ifstream file(root / name, std::ios::binary);
            std::vector<u8> expected(frames[i].size());
            file.read(reinterpret_cast<char *>(expected.data()),
                      static_cast<std::streamsize>(expected.size()));
            if (!file || file.peek() != std::char_traits<char>::eof() || expected != frames[i])
            {
                std::fprintf(stderr, "vector golden differs: %s\n", name);
                return false;
            }
        }
    }
    return true;
}
static void vector_painter_gpu_regressions(const char *directory = nullptr, bool capture = false)
{
    const auto direct = vector_painter_pixels(vulkan_upload_mode::DIRECT, false);
    const auto staged = vector_painter_pixels(vulkan_upload_mode::STAGED, false);
    const auto cached = vector_painter_pixels(vulkan_upload_mode::DIRECT, true);
    const auto cached_staged = vector_painter_pixels(vulkan_upload_mode::STAGED, true);
    CHECK(direct.size() == 21 && direct == staged && direct == cached && direct == cached_staged);
    if (directory) CHECK(vector_painter_fixture(direct, directory, capture));
    if (!g_failures)
        std::puts("all native vector painter regressions passed (84 exact direct/staged/cache/DPI "
                  "frames)");
}
