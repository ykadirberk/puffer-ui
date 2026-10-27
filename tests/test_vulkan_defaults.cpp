// Public defaults must work without the showcase or any example bootstrap.
#include <pufferui/vulkan.h>
#if defined(PUFFERUI_ENABLE_SDL3)
#include <SDL3/SDL.h>
#endif
#include <cstdio>
#include <cstring>
#include <vector>

using namespace pui;
constexpr vulkan_device_opts DEFAULTS{};
static_assert(DEFAULTS.painter == vulkan_painter_mode::INTEGRATED);
static_assert(DEFAULTS.tile_cache && DEFAULTS.whole_program_cache && !DEFAULTS.adaptive_compute);
static_assert(DEFAULTS.blur == vulkan_blur_mode::LEGACY);
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
static_assert(DEFAULTS.vector_text);
#else
static_assert(!DEFAULTS.vector_text);
#endif

#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line%d %s\n", __LINE__, #x);                                \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

struct scene
{
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    std::vector<u8> pixels;
    ~scene()
    {
        if (ctx) destroy_context(ctx);
        if (surface) device->destroy_surface(surface);
    }
    bool initialize(render_device *owner)
    {
        device = owner;
        surface = create_vulkan_surface(device, {.width = 240, .height = 120});
        REQUIRE(surface);
        ctx = create_context(device, surface);
        REQUIRE(ctx);
        win = add_window(ctx, nullptr, surface, {0, 0, 240, 120});
        REQUIRE(win);
        auto appearance = default_dark();
        appearance.font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        REQUIRE(appearance.font != FONT_INVALID);
        set_theme(ctx, appearance);
        pixels.resize(240 * 120 * 4);
        return true;
    }
    bool frame(u32 index)
    {
        set_current_context(ctx);
        begin_frame(ctx, *win, static_cast<f64>(index) / 60, 1.0 / 60);
        {
            ui u(ctx);
            u.draw_rect(win->area, {21, 31, 47, 255});
            u.draw_rounded_rect({9.25f, 8.75f, 218, 99}, {37, 97, 131, 179}, 13.5f);
            u.text({19.5f, 18.25f, 200, 28}, "Office ffi / Ag / 123", {235, 221, 187, 213});
            if (index >= 12)
                u.draw_rounded_rect({187.75f, 66.25f, 25, 25}, {189, 71, 111, 191}, 12.5f);
        }
        end_frame(ctx);
        REQUIRE(!violation_count(ctx));
        REQUIRE(vulkan_last_result(device) == VK_SUCCESS);
        REQUIRE(device->read_pixels(surface->scene_target(), 0, 0, 240, 120, pixels.data()));
        return true;
    }
};

static bool exercise(bool staged)
{
    // The no-argument overload and aggregate options must agree. The latter
    // also exercises staged uploads without selecting a painter or its caches.
    vulkan_device_opts options;
    options.validation = true;
    options.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    render_device *candidate = staged ? create_vulkan_device(options) : create_vulkan_device();
    REQUIRE(candidate);
    REQUIRE(vulkan_painter(candidate) == vulkan_painter_mode::INTEGRATED);
    const auto native = vulkan_handles(candidate);
    options.native = &native;
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.tile_cache = options.whole_program_cache = false;
    render_device *reference = create_vulkan_device(options);
    REQUIRE(reference);
    REQUIRE(vulkan_painter(reference) == vulkan_painter_mode::SDF_RASTER);
    bool okay = true;
    {
        scene scenes[2];
        REQUIRE(scenes[0].initialize(candidate) && scenes[1].initialize(reference));
        for (u32 frame = 0; frame < 24; ++frame)
        {
            REQUIRE(scenes[0].frame(frame) && scenes[1].frame(frame));
            REQUIRE(scenes[0].pixels == scenes[1].pixels);
        }
        REQUIRE(vulkan_wait_idle(candidate) && vulkan_wait_idle(reference));
        const auto stats = vulkan_statistics(candidate);
        REQUIRE(stats.sdf_shapes && stats.whole_program_hits);
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
        REQUIRE(stats.glyph_resident_batches && stats.glyph_upload_bytes);
#endif
        okay = !stats.validation_errors && !vulkan_statistics(reference).validation_errors;
    }
    destroy_vulkan_device(reference);
    // Diagnostic REFERENCE is still available with every default feature off.
    options.painter = vulkan_painter_mode::REFERENCE;
    options.vector_text = false;
    render_device *legacy = create_vulkan_device(options);
    REQUIRE(legacy && vulkan_painter(legacy) == vulkan_painter_mode::REFERENCE);
    destroy_vulkan_device(legacy);
    destroy_vulkan_device(candidate);
    return okay;
}

static bool sdl_factory()
{
#if defined(PUFFERUI_ENABLE_SDL3)
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *native =
        SDL_CreateWindow("Vulkan defaults", 240, 120, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    REQUIRE(native);
    render_device *device = create_sdl3_vulkan_device();
    REQUIRE(device && vulkan_painter(device) == vulkan_painter_mode::INTEGRATED);
    bool okay = false;
    {
        scene sample;
        okay = sample.initialize(device) && sample.frame(0);
    }
    destroy_vulkan_device(device);
    SDL_DestroyWindow(native);
    SDL_Quit();
    return okay;
#else
    return true;
#endif
}

int main(int argc, char **argv)
{
    if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "--staged"))) return 1;
    if (!exercise(argc == 2) || !sdl_factory()) return 1;
    std::puts("all Vulkan default tests passed (factories, overrides, 24 exact frames)");
    return 0;
}
