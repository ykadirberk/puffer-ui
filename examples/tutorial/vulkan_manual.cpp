// Chapter 10: explicit SDL + Vulkan ownership, with no example bootstrap.
#include <pufferui/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace pui;

static void report_vulkan_error(i32 result, const char *operation, void *)
{
    std::fprintf(stderr, "Vulkan error %d: %s\n", result, operation);
}

struct sdl_vulkan_host final : vulkan_host
{
    SDL_Cursor *cursors[5]{};
    PFN_vkGetInstanceProcAddr instance_proc_addr() override
    {
        return reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    }
    std::span<const char *const> instance_extensions() override
    {
        Uint32 count = 0;
        const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
        return {names, count};
    }
    VkSurfaceKHR create_surface(VkInstance instance, void *window) override
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        SDL_Vulkan_CreateSurface(static_cast<SDL_Window *>(window), instance, nullptr, &surface);
        return surface;
    }
    void output_size(void *window, i32 &width, i32 &height) override
    {
        auto *native = static_cast<SDL_Window *>(window);
        width = height = 0;
        if (!(SDL_GetWindowFlags(native) & SDL_WINDOW_MINIMIZED))
            SDL_GetWindowSizeInPixels(native, &width, &height);
    }
    void set_cursor(cursor requested) override
    {
        const i32 index = static_cast<i32>(requested);
        if (index < 0 || index >= 5) return;
        if (!cursors[index])
        {
            const SDL_SystemCursor kinds[] = {
                SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_TEXT, SDL_SYSTEM_CURSOR_POINTER,
                SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE};
            cursors[index] = SDL_CreateSystemCursor(kinds[index]);
        }
        if (cursors[index]) SDL_SetCursor(cursors[index]);
    }
    void release_cursors()
    {
        for (auto *&value : cursors)
        {
            if (value) SDL_DestroyCursor(value);
            value = nullptr;
        }
    }
};

struct application
{
    // The host and native window must outlive their Vulkan surfaces and device.
    sdl_vulkan_host host;
    SDL_Window *native = nullptr;
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *win = nullptr;
    bool sdl_initialized = false, running = true, vsync = true;
    i32 count = 0;
    rect increment_button{};

    ~application() { shutdown(); }
    bool initialize(i32 width, i32 height, bool selftest)
    {
        sdl_initialized = SDL_Init(SDL_INIT_VIDEO);
        if (!sdl_initialized) return false;
        const SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                      SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                      (selftest ? SDL_WINDOW_HIDDEN : SDL_WindowFlags{0});
        native = SDL_CreateWindow("PufferUI - explicit Vulkan integration", width, height, flags);
        if (!native) return false; // SDL loaded the platform Vulkan loader for this window.

        vulkan_device_opts options;
        options.host = &host;
        options.validation = selftest;
        options.error_callback = report_vulkan_error;
        // Native triangle shapes plus HarfBuzz/Slug text in an enabled build.
        // No example bootstrap or command-line painter flags are required.
        device = create_vulkan_device(options);
        if (!device) return false;
        surface = create_vulkan_surface(device, {.window = native});
        if (!surface) return false;
        ctx = create_context(device, surface);
        if (!ctx) return false;
        i32 x = 0, y = 0;
        SDL_GetWindowPosition(native, &x, &y);
        SDL_GetWindowSize(native, &width, &height);
        win = add_window(ctx, native, surface,
                         {static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width),
                          static_cast<f32>(height)});
        if (!win) return false;
        const f32 density = SDL_GetWindowPixelDensity(native);
        const f32 scale = SDL_GetWindowDisplayScale(native);
        if (density > 0) set_window_scale(*win, scale, {.input_scale = scale / density});
        focus_window(ctx, *win);
        set_break_on_violation(ctx, false);
        set_clipboard(ctx, sdl3_system_clipboard());
        theme appearance = default_dark();
        appearance.font = load_font(ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        if (appearance.font == FONT_INVALID) return false;
        set_theme(ctx, appearance);
        SDL_StartTextInput(native);
        vsync = !selftest;
        device->set_vsync(vsync); // Set once; change it only when the checkbox changes.
        return true;
    }

    void build_ui()
    {
        ui u(ctx);
        rect page = win->area;
        u.draw_rect(page, u.th().bg);
        rect content = page.pad(16.0f);
        u.text(content.cut_top(u.control_h() * 1.5f), "Vulkan, explicitly wired", u.th().text);
        (void)content.cut_top(u.spacing());
        rect description = content.cut_top(u.control_h() * 2.5f);
        u.text_wrapped(description,
                       "SDL owns this window and its input. PufferUI owns the Vulkan device, "
                       "command stream, painting and exact reuse.",
                       u.th().text_dim);
        (void)content.cut_top(u.spacing());
        rect card = content.cut_top(u.control_h() * 4 + u.spacing() * 5);
        u.draw_rounded_rect(card, u.th().tokens.surface, 12);
        rect controls = card.pad(u.spacing() * 2);
        char label[64];
        std::snprintf(label, sizeof(label), "Application-owned count: %d", count);
        u.text(controls.cut_top(u.control_h()), label, u.th().text);
        (void)controls.cut_top(u.spacing());
        increment_button = controls.cut_top(u.control_h());
        if (u.button(increment_button, "Add one", "increment"_id)) ++count;
        (void)controls.cut_top(u.spacing());
        if (u.checkbox(controls.cut_top(u.control_h()), "VSync", vsync, "vsync"_id))
            device->set_vsync(vsync);
        (void)content.cut_top(u.spacing() * 2);
        u.text_wrapped(content,
                       "Default: native Vulkan + HarfBuzz/Slug\n"
                       "Explicit identities / renderer-owned storage / native presentation",
                       u.th().text_dim);
    }

    bool save_screenshot(const char *path)
    {
        i32 width = 0, height = 0;
        surface->output_size(width, height);
        if (width <= 0 || height <= 0) return false;
        std::vector<u8> rgba(static_cast<usize>(width) * height * 4);
        if (!device->read_pixels(surface->scene_target(), 0, 0, width, height, rgba.data()))
            return false;
        SDL_Surface *image =
            SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, rgba.data(), width * 4);
        if (!image) return false;
        const bool saved = SDL_SaveBMP(image, path);
        SDL_DestroySurface(image);
        return saved;
    }

    void shutdown()
    {
        if (native) SDL_StopTextInput(native);
        if (ctx) destroy_context(ctx); // Fonts, painters, windows and their UI state go first.
        ctx = nullptr;
        win = nullptr;
        if (surface) device->destroy_surface(surface);
        surface = nullptr;
        if (device) destroy_vulkan_device(device); // Waits for its owned GPU work.
        device = nullptr;
        host.release_cursors();
        if (native) SDL_DestroyWindow(native);
        native = nullptr;
        if (sdl_initialized) SDL_Quit();
        sdl_initialized = false;
    }
};

// Selftest input uses the same SDL queue and routing as interactive input.
static bool queue_click(application &app, bool down)
{
    const f32 inverse_scale = 1.0f / app.win->input_scale;
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = SDL_GetWindowID(app.native);
    motion.motion.x = (app.increment_button.x + app.increment_button.w * 0.5f) * inverse_scale;
    motion.motion.y = (app.increment_button.y + app.increment_button.h * 0.5f) * inverse_scale;
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
    i32 width = 720, height = 480, frames = 8, resize_width = 0, resize_height = 0;
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
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc)
        {
            if (SDL_sscanf(argv[++i], "%dx%d", &width, &height) != 2 || width <= 0 || height <= 0)
                return 1;
        }
        else if (!std::strcmp(argv[i], "--resize") && i + 1 < argc)
        {
            if (SDL_sscanf(argv[++i], "%dx%d", &resize_width, &resize_height) != 2 ||
                resize_width <= 0 || resize_height <= 0)
                return 1;
        }
        else
        {
            std::fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
            return 1;
        }
    }
    application app;
    if (!app.initialize(width, height, selftest))
    {
        std::fprintf(stderr, "Initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    const f64 frequency = static_cast<f64>(SDL_GetPerformanceFrequency());
    u64 previous = SDL_GetPerformanceCounter();
    f64 now = 0;
    i32 drawn = 0;
    bool okay = true;
    while (app.running && (!selftest || drawn < frames))
    {
        if (selftest && script && (drawn == 1 || drawn == 2))
            okay = queue_click(app, drawn == 1) && okay;
        if (selftest && drawn == 3 && resize_width)
        {
            okay = SDL_SetWindowSize(app.native, resize_width, resize_height) && okay;
            // A queued resize also makes the hidden-window path deterministic.
            SDL_Event resized{};
            resized.type = SDL_EVENT_WINDOW_RESIZED;
            resized.window.windowID = SDL_GetWindowID(app.native);
            okay = SDL_PushEvent(&resized) && okay;
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
        const f64 dt = selftest ? 1.0 / 60.0 : static_cast<f64>(counter - previous) / frequency;
        previous = counter;
        now += dt;
        begin_frame(app.ctx, *app.win, now, dt);
        {
            app.build_ui(); // All UI scopes are destroyed before end_frame.
        }
        end_frame(app.ctx); // Submits and presents the native Vulkan surface.
        ++drawn;
    }
    if (screenshot) okay = app.save_screenshot(screenshot) && okay;
    okay = !violation_count(app.ctx) && vulkan_last_result(app.device) == VK_SUCCESS &&
           !vulkan_statistics(app.device).validation_errors && okay;
    if (selftest)
    {
        okay =
            drawn == frames && (!script || app.count == 1) && is_vulkan_device(app.device) && okay;
        if (resize_width)
            okay = app.win->client.w == resize_width && app.win->client.h == resize_height && okay;
    }
    app.shutdown();
    if (selftest)
        std::puts(okay ? "ok   explicit Vulkan tutorial (input, resize, defaults)"
                       : "FAIL explicit Vulkan tutorial");
    return okay ? 0 : 1;
}
