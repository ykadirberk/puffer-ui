// The original Spiral workload and timers, selecting only the new blur filter.
#include <pufferui/pufferui.h>
static bool kawase_app_init(pui::sdl3_app &app, const char *title, pui::i32 width, pui::i32 height,
                            int argc, char **argv)
{
    app.vulkan.blur = pui::vulkan_blur_mode::KAWASE;
    return pui::sdl3_app_init(app, title, width, height, argc, argv);
}
#define sdl3_app_init kawase_app_init
#include "bench_vulkan_painter.cpp"
#undef sdl3_app_init
