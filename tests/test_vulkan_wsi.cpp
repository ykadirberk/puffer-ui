#include <pufferui/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
using namespace pui;
static i32 errors = 0;
static void diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, message);
    ++errors;
}
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s / SDL: %s\n", __LINE__, #x, SDL_GetError());         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
// A real SDL surface with deterministic zero framebuffer size. This exercises
// minimization without depending on asynchronous OS window-manager timing.
struct zero_size_host : vulkan_host
{
    bool zero = false;
    PFN_vkGetInstanceProcAddr instance_proc_addr() override
    {
        return reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    }
    std::span<const char *const> instance_extensions() override
    {
        Uint32 n = 0;
        auto names = SDL_Vulkan_GetInstanceExtensions(&n);
        return {names, n};
    }
    VkSurfaceKHR create_surface(VkInstance instance, void *window) override
    {
        VkSurfaceKHR result = VK_NULL_HANDLE;
        SDL_Vulkan_CreateSurface(static_cast<SDL_Window *>(window), instance, nullptr, &result);
        return result;
    }
    void output_size(void *window, i32 &w, i32 &h) override
    {
        if (zero)
            w = h = 0;
        else
            SDL_GetWindowSizeInPixels(static_cast<SDL_Window *>(window), &w, &h);
    }
};
static int zero_size_frames()
{
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window =
        SDL_CreateWindow("Vulkan zero extent", 96, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    zero_size_host host;
    vulkan_device_opts opts;
    opts.host = &host;
    opts.validation = true;
    opts.error_callback = diagnostic;
    render_device *d = create_vulkan_device(opts);
    REQUIRE(d);
    render_surface *offscreen = create_vulkan_surface(d, {.width = 8, .height = 8});
    REQUIRE(offscreen);
    render_surface *s = create_vulkan_surface(d, {.window = window});
    REQUIRE(s);
    // Offscreen references must not take the native window's pacer role.
    REQUIRE(vulkan_present_mode(s) == VK_PRESENT_MODE_FIFO_KHR);
    SDL_Window *second =
        SDL_CreateWindow("Vulkan next pacer", 96, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    REQUIRE(second);
    render_surface *next = create_vulkan_surface(d, {.window = second});
    REQUIRE(next);
    d->destroy_surface(s);
    SDL_DestroyWindow(window);
    window = second;
    s = next;
    s->make_current(*d);
    REQUIRE(vulkan_present_mode(s) == VK_PRESENT_MODE_FIFO_KHR);
    d->clear({1, 2, 3, 255});
    s->present();
    d->set_vsync(false);
    for (i32 frame = 0; frame < 12; ++frame)
    {
        host.zero = frame >= 4 && frame < 7;
        const u64 submitted = vulkan_statistics(d).submissions;
        s->make_current(*d);
        d->clear({7, 8, 9, 255});
        s->present();
        i32 w = -1, h = -1;
        s->output_size(w, h);
        if (host.zero)
            REQUIRE(w == 0 && h == 0 && vulkan_statistics(d).submissions == submitted);
        else
            REQUIRE(w == 96 && h == 64);
        REQUIRE(vulkan_last_result(d) == VK_SUCCESS);
    }
    d->destroy_surface(s);
    d->destroy_surface(offscreen);
    REQUIRE(vulkan_wait_idle(d));
    REQUIRE(vulkan_statistics(d).validation_errors == 0);
    destroy_vulkan_device(d);
    SDL_DestroyWindow(window);
    SDL_Quit();
    REQUIRE(errors == 0);
    return 0;
}
int main()
{
    sdl3_app app;
    app.hidden = true;
    sdl3_vulkan_app_opts opts;
    opts.width = 320;
    opts.height = 240;
    opts.device.validation = true;
    opts.device.error_callback = diagnostic;
    REQUIRE(sdl3_vulkan_app_init(app, opts));
    REQUIRE(app.sdl_renderer == nullptr && is_vulkan_device(app.device));
    app.device->set_vsync(false);
    window *extra = sdl3_app_open_window(app, "extra", 240, 160, {.hidden = true});
    REQUIRE(extra);
    for (i32 f = 0; f < 24; ++f)
    {
        if (f >= 4 && f < 12)
        {
            SDL_SetWindowSize(static_cast<SDL_Window *>(app.sdl_window), 400 + f * 3, 280 + f * 2);
            app.surface->resize();
        }
        REQUIRE(sdl3_app_pump(app));
        for (window *w : {app.win, extra})
        {
            // Inject logical DPI changes between frames without altering any
            // OS display setting; exercise both windows and shared font images.
            set_window_scale(*w, f >= 8 && f < 16 ? 1.5f : 1.0f);
            begin_frame(app.ctx, *w, f / 60.0, 1.0 / 60.0);
            {
                ui u(app.ctx);
                u.draw_rect(rect::make(0, 0, 64, 64), {255, 0, 0, 255});
                u.text(rect::make(70, 10, 160, 30), "Vulkan SDL", {255, 255, 255, 255});
                if (w == app.win && f >= 2 && f < 5)
                {
                    auto popup =
                        u.popup("native-outside"_id, rect::make(w->client.w - 20, 10, 140, 70));
                    u.draw_rect(popup.content(), {0, 255, 0, 200});
                }
            }
            end_frame(app.ctx);
        }
        REQUIRE(vulkan_last_result(app.device) == VK_SUCCESS);
    }
    REQUIRE(violation_count(app.ctx) == 0);
    REQUIRE(vulkan_wait_idle(app.device));
    REQUIRE(vulkan_statistics(app.device).validation_errors == 0);
    sdl3_app_close_window(app, *extra);
    sdl3_app_shutdown(app);
    REQUIRE(errors == 0);
    REQUIRE(zero_size_frames() == 0);
    std::puts("all Vulkan SDL WSI tests passed");
    return 0;
}
