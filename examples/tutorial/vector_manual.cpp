// Chapter 11: public SVG/path resources, explicit SDL/Vulkan application lifetime.
#include <pufferui/vulkan.h>
#include <SDL3/SDL.h>
#include "../material_icons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace pui;

struct vector_application
{
    static constexpr color ICON_COLOR{116, 193, 255, 255};
    SDL_Window *native = nullptr;
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    vector_handle icon = nullptr, path = nullptr;
    bool initialized = false, running = true, okay = true;
    f32 opacity = 1, angle = 0;
    i32 turns = 0, painted = 0;
    rect turn_button{};
    ~vector_application() { shutdown(); }
    bool initialize(i32 width, i32 height, bool selftest)
    {
        initialized = SDL_Init(SDL_INIT_VIDEO);
        if (!initialized) return false;
        native = SDL_CreateWindow("PufferUI - explicit SVG and paths", width, height,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                      SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                      (selftest ? SDL_WINDOW_HIDDEN : SDL_WindowFlags{0}));
        if (!native) return false;
        device = create_sdl3_vulkan_device({.validation = selftest});
        if (!device) return false;
        surface = create_vulkan_surface(device, {.window = native});
        if (!surface) return false;
        ctx = create_context(device, surface);
        if (!ctx) return false;
        i32 x, y;
        SDL_GetWindowPosition(native, &x, &y);
        SDL_GetWindowSize(native, &width, &height);
        win = add_window(ctx, native, surface,
                         {static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width),
                          static_cast<f32>(height)});
        if (!win) return false;
        const f32 density = SDL_GetWindowPixelDensity(native),
                  scale = SDL_GetWindowDisplayScale(native);
        if (density > 0) set_window_scale(*win, scale, {.input_scale = scale / density});
        theme appearance = default_dark();
        appearance.font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        if (appearance.font == FONT_INVALID) return false;
        set_theme(ctx, appearance);
        set_clipboard(ctx, sdl3_system_clipboard());
        focus_window(ctx, *win);
        set_break_on_violation(ctx, false);
        SDL_StartTextInput(native);
        device->set_vsync(!selftest);
        return prepare_vectors();
    }
    bool prepare_vectors()
    {
        const auto svg = example_assets::load_svg_file(example_assets::EXPLORE_SVG);
        if (!svg)
        {
            std::fprintf(stderr, "%s: %s (byte %zu)\n", example_assets::EXPLORE_SVG,
                         svg.error.message, svg.error.offset);
            return false;
        }
        icon = svg.image;
        const path_command commands[] = {{path_verb::MOVE, {16, 96}},
                                         {path_verb::CUBIC, {16, 16}, {112, 16}, {112, 96}},
                                         {path_verb::QUADRATIC, {64, 56}, {16, 96}},
                                         {path_verb::CLOSE}};
        const auto source =
            create_path({.commands = commands,
                         .fill = {.kind = vector_paint_kind::SOLID, .tint = {116, 193, 255, 230}},
                         .stroke = {.kind = vector_paint_kind::SOLID, .tint = {255, 255, 255, 255}},
                         .stroke_options = {.width = 3, .join = path_line_join::ROUND},
                         .view_box = {0, 0, 128, 128}});
        if (!source) return false;
        path = source.image;
        ui u(ctx);
        const vector_prepare_opts preparation{.max_pixel_size = {256, 256},
                                              .max_instances = 8,
                                              .resource_budget_bytes = 16 * 1024 * 1024,
                                              .style = {.current_color = ICON_COLOR}};
        // Resource setup runs before the first begin_frame. Draws reuse it.
        return static_cast<bool>(u.prepare_vector(icon, preparation)) &&
               static_cast<bool>(u.prepare_vector(path, preparation));
    }
    void frame()
    {
        ui u(ctx);
        rect content = win->area.pad(16);
        u.text(content.cut_top(u.control_h() * 1.5f), "SVG and paths, explicitly owned",
               u.th().text);
        (void)content.cut_top(u.spacing());
        u.slider_float(content.cut_top(u.control_h()), "Opacity", opacity, .15f, 1,
                       "vector_opacity"_id, "%.2f");
        (void)content.cut_top(u.spacing());
        turn_button = content.cut_top(u.control_h());
        if (u.button(turn_button, "Turn the vectors", "turn_vectors"_id))
        {
            angle += .125f;
            ++turns;
        }
        (void)content.cut_top(u.spacing() * 2);
        const f32 width = std::max(0.0f, (content.w - u.spacing()) * .5f);
        row previews(content, u.spacing());
        for (i32 i = 0; i < 2; ++i)
        {
            rect bounds = previews.next(width);
            u.text_ellipsis(bounds.cut_top(u.control_h()), i ? "C++ path" : "Explore.svg",
                            u.th().text_dim, ALIGN_CENTER);
            const f32 size =
                std::max(0.0f, std::min(std::min(bounds.w, bounds.h), 256.0f / win->scale) -
                                   u.spacing() * 2);
            const vec2 center{bounds.x + bounds.w * .5f, bounds.y + bounds.h * .5f};
            const f32 c = std::cos(angle), s = std::sin(angle);
            const affine2 rotation{c,
                                   s,
                                   -s,
                                   c,
                                   center.x - c * center.x + s * center.y,
                                   center.y - s * center.x - c * center.y};
            const auto status = u.draw_vector(
                {center.x - size * .5f, center.y - size * .5f, size, size}, i ? path : icon,
                {.transform = rotation,
                 .style = {.current_color = ICON_COLOR, .opacity = opacity}});
            if (!status)
            {
                std::fprintf(stderr, "Vector: %s\n", status.error.message);
                okay = false;
                running = false;
            }
        }
        ++painted;
    }
    bool save(const char *filename)
    {
        i32 width, height;
        surface->output_size(width, height);
        if (width <= 0 || height <= 0) return false;
        std::vector<u8> rgba(static_cast<usize>(width) * height * 4);
        if (!device->read_pixels(surface->scene_target(), 0, 0, width, height, rgba.data()))
            return false;
        auto *image =
            SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, rgba.data(), width * 4);
        if (!image) return false;
        const bool saved = SDL_SaveBMP(image, filename);
        SDL_DestroySurface(image);
        return saved;
    }
    void shutdown()
    {
        if (ctx)
        {
            ui u(ctx);
            if (icon) (void)u.release_vector(icon);
            if (path) (void)u.release_vector(path);
            destroy_vector(icon);
            destroy_vector(path);
            destroy_context(ctx);
        }
        ctx = nullptr;
        win = nullptr;
        if (surface && device) device->destroy_surface(surface);
        surface = nullptr;
        if (device) destroy_vulkan_device(device);
        device = nullptr;
        if (native)
        {
            SDL_StopTextInput(native);
            SDL_DestroyWindow(native);
        }
        native = nullptr;
        if (initialized) SDL_Quit();
        initialized = false;
    }
};
static bool queue_vector_turn(vector_application &app, bool down)
{
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = SDL_GetWindowID(app.native);
    motion.motion.x = (app.turn_button.x + app.turn_button.w * .5f) / app.win->input_scale;
    motion.motion.y = (app.turn_button.y + app.turn_button.h * .5f) / app.win->input_scale;
    SDL_Event button{};
    button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.windowID = motion.motion.windowID;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = down;
    button.button.x = motion.motion.x;
    button.button.y = motion.motion.y;
    return SDL_PushEvent(&motion) && SDL_PushEvent(&button);
}
int main(int argc, char **argv)
{
    bool selftest = false, script = true;
    i32 frames = 8, width = 760, height = 520, resize_width = 0, resize_height = 0;
    const char *screenshot = nullptr;
    for (i32 i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--selftest"))
            selftest = true;
        else if (!std::strcmp(argv[i], "--no-script"))
            script = false;
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = std::max(6, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc)
            screenshot = argv[++i];
        else if ((!std::strcmp(argv[i], "--size") || !std::strcmp(argv[i], "--resize")) &&
                 i + 1 < argc)
        {
            const bool resize = std::strcmp(argv[i], "--resize") == 0;
            int w, h;
            if (SDL_sscanf(argv[++i], "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) return 1;
            if (resize)
            {
                resize_width = w;
                resize_height = h;
            }
            else
            {
                width = w;
                height = h;
            }
        }
        else if (!std::strcmp(argv[i], "--help"))
        {
            std::puts("SVG/path tutorial: --selftest --frames N --resize WxH --screenshot FILE "
                      "--no-script");
            return 0;
        }
        else
            return 1;
    }
    vector_application app;
    if (!app.initialize(width, height, selftest))
    {
        std::fprintf(stderr, "Vector tutorial initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    const f64 frequency = static_cast<f64>(SDL_GetPerformanceFrequency());
    u64 previous = SDL_GetPerformanceCounter();
    f64 now = 0;
    while (app.running && (!selftest || app.painted < frames))
    {
        if (selftest && script && (app.painted == 1 || app.painted == 2))
            app.okay = queue_vector_turn(app, app.painted == 1) && app.okay;
        if (selftest && app.painted == 3 && resize_width)
        {
            app.okay = SDL_SetWindowSize(app.native, resize_width, resize_height) && app.okay;
            SDL_Event event{};
            event.type = SDL_EVENT_WINDOW_RESIZED;
            event.window.windowID = SDL_GetWindowID(app.native);
            SDL_PushEvent(&event);
        }
        SDL_Event event;
        if (!selftest && !needs_redraw(app.ctx) && SDL_WaitEventTimeout(&event, 120))
            if (sdl3_route(app.ctx, &event)) app.running = false;
        while (SDL_PollEvent(&event))
            if (sdl3_route(app.ctx, &event)) app.running = false;
        if (!app.running || app.win->close_requested) break;
        if (SDL_GetWindowFlags(app.native) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }
        const u64 counter = SDL_GetPerformanceCounter();
        const f64 dt = selftest ? 1.0 / 60 : static_cast<f64>(counter - previous) / frequency;
        previous = counter;
        now += dt;
        begin_frame(app.ctx, *app.win, now, dt);
        app.frame();
        end_frame(app.ctx);
    }
    bool okay = app.okay && !violation_count(app.ctx) &&
                vulkan_last_result(app.device) == VK_SUCCESS &&
                !vulkan_statistics(app.device).validation_errors;
    if (screenshot) okay = app.save(screenshot) && okay;
    if (selftest) okay = app.painted == frames && (!script || app.turns == 1) && okay;
    app.shutdown();
    if (selftest)
        std::puts(okay ? "ok   explicit SVG/path tutorial (input, resize, prepared drawing)"
                       : "FAIL explicit SVG/path tutorial");
    return okay ? 0 : 1;
}
