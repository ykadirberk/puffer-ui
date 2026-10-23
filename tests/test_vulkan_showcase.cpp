// Exercise the actual application's initial HUD state and backend policy together.
#define main showcase_original_main
#include "../examples/pui_showcase.cpp"
#undef main
#include <chrono>

#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static i32 frames(example_app &app, i32 count)
{
    example_clock clock;
    i32 focused = 0;
    for (i32 i = 0; i < count; ++i)
    {
        example_tick(app, clock);
        example_pump(app);
        if (SDL_GetWindowFlags(app.sdl_window) & SDL_WINDOW_INPUT_FOCUS) ++focused;
        begin_frame(app.ctx, *app.win, app.now, app.dt);
        {
            ui u(app.ctx);
            showcase_frame(u, app);
        }
        end_frame(app.ctx);
        ++app.frame_index;
    }
    return focused;
}

int main(int argc, char **argv)
{
#if defined(_WIN32)
    _putenv_s("PUFFERUI_VULKAN_VALIDATION", "1");
#else
    setenv("PUFFERUI_VULKAN_VALIDATION", "1", 1);
#endif
    const bool visible = argc > 1 && std::strcmp(argv[1], "--visible") == 0;
    for (bool interactive : {false, true})
    {
        g_app = showcase{};
        seed(g_app);
        g_start_page = PAGE_OVERVIEW;
        g_fuzz = 0;
        example_app app;
        char program[] = "showcase-vsync-test";
        char selftest[] = "--selftest";
        char *arguments[] = {program, selftest};
        REQUIRE(
            example_init(app, "Vulkan showcase startup", 1360, 860, visible ? 1 : 2, arguments));
        REQUIRE(app.renderer == nullptr && is_vulkan_device(app.device));
        // Test both the benchmark bootstrap (uncapped) and an interactive
        // startup after a caller has changed the device policy. The model must
        // establish the intended initial policy, not assume the device default.
        app.selftest = !interactive;
        app.device->set_vsync(false);
        frames(app, 3);
        REQUIRE(g_app.vsync == interactive && g_app.vsync_applied == interactive);
        if (interactive) REQUIRE(vulkan_present_mode(app.surface) == VK_PRESENT_MODE_FIFO_KHR);

        if (visible && interactive)
        {
            SDL_RaiseWindow(app.sdl_window);
            const SDL_DisplayMode *display =
                SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(app.sdl_window));
            const f32 refresh = display ? display->refresh_rate : 0.0f;
            const auto native = vulkan_handles(app.device);
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(native.physical_device, &properties);
            for (bool sync : {true, false, true})
            {
                g_app.vsync = sync;
                frames(app, 3); // settle the requested swapchain change
                const auto start = std::chrono::steady_clock::now();
                const i32 focused = frames(app, 180);
                const f64 elapsed =
                    std::chrono::duration<f64>(std::chrono::steady_clock::now() - start).count();
                std::printf("device=%s requested_vsync=%d present_mode=%d frames=180 "
                            "focused=%d refresh_hz=%.3f seconds=%.6f fps=%.3f\n",
                            properties.deviceName, sync,
                            static_cast<i32>(vulkan_present_mode(app.surface)), focused, refresh,
                            elapsed, 180.0 / elapsed);
                if (sync) REQUIRE(vulkan_present_mode(app.surface) == VK_PRESENT_MODE_FIFO_KHR);
            }
        }
        g_app.vsync = false;
        frames(app, 2);
        REQUIRE(!g_app.vsync_applied);
        g_app.vsync = true;
        frames(app, 2);
        REQUIRE(g_app.vsync_applied &&
                vulkan_present_mode(app.surface) == VK_PRESENT_MODE_FIFO_KHR);
        REQUIRE(vulkan_last_result(app.device) == VK_SUCCESS &&
                vulkan_statistics(app.device).validation_errors == 0);
        REQUIRE(example_shutdown(app) == 0);
    }
    std::puts("all Vulkan showcase startup/vsync tests passed");
    return 0;
}
