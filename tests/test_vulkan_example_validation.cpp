// Exercise the real example bootstrap, not only an environment parser.
#include "../examples/example_common.h"
#include <string>

namespace
{
struct validation_environment
{
    bool present = false;
    std::string value;
    validation_environment()
    {
        if (const char *previous = std::getenv("PUFFERUI_VULKAN_VALIDATION"))
        {
            present = true;
            value = previous;
        }
    }
    void set(const char *next)
    {
#if defined(_WIN32)
        _putenv_s("PUFFERUI_VULKAN_VALIDATION", next ? next : "");
#else
        if (next)
            setenv("PUFFERUI_VULKAN_VALIDATION", next, 1);
        else
            unsetenv("PUFFERUI_VULKAN_VALIDATION");
#endif
    }
    ~validation_environment() { set(present ? value.c_str() : nullptr); }
};
} // namespace

int main(int argc, char **argv)
{
    if (argc == 2 && (std::strcmp(argv[1], "--bootstrap-failure") == 0 ||
                      std::strcmp(argv[1], "--bootstrap-help") == 0))
    {
        const bool help = std::strcmp(argv[1], "--bootstrap-help") == 0;
        if (!SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "pui-invalid-selftest-video",
                                     SDL_HINT_OVERRIDE))
            return 3;
        char program[] = "example-bootstrap-test", selftest[] = "--selftest",
             help_argument[] = "--help";
        char *arguments[] = {program, selftest, help_argument};
        bool called = false;
        const int status = example_run("Bootstrap regression", 160, 120, help ? 3 : 2, arguments,
                                       [&](ui &, example_app &app)
                                       {
                                           called = true;
                                           app.running = false;
                                       });
        return called ? 4 : status;
    }

    bool bootstrap_ok = true;
    for (const bool help : {false, true})
    {
        const char *arguments[] = {argv[0], help ? "--bootstrap-help" : "--bootstrap-failure",
                                   nullptr};
        SDL_PropertiesID properties = SDL_CreateProperties();
        const bool configured =
            properties &&
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,
                                   true);
        SDL_Process *process = configured ? SDL_CreateProcessWithProperties(properties) : nullptr;
        SDL_DestroyProperties(properties);
        if (!process)
        {
            std::fprintf(stderr, "FAIL bootstrap subprocess: %s\n", SDL_GetError());
            return 1;
        }
        size_t size = 0;
        int status = -1;
        char *output = static_cast<char *>(SDL_ReadProcess(process, &size, &status));
        const bool valid = output && size && status == (help ? 0 : 1) &&
                           std::strstr(output, help ? "a PufferUI example" : "SDL_Init:") &&
                           !std::strstr(output, "ok   ");
        if (!valid)
            std::fprintf(stderr, "FAIL bootstrap help=%d exit=%d output=%s\n", help, status,
                         output ? output : "<unavailable>");
        bootstrap_ok &= valid;
        SDL_free(output);
        SDL_DestroyProcess(process);
    }
    if (!bootstrap_ok) return 1;

    validation_environment environment;
    std::vector<u8> expected;
    struct validation_case
    {
        const char *value;
        bool requested;
    };
    constexpr validation_case cases[] = {
        {nullptr, false}, {"0", false}, {"false", false}, {"OFF", false}, {"1", true}};
    for (const auto &test : cases)
    {
        environment.set(test.value);
        example_app app;
        char program[] = "example-validation-test", selftest[] = "--selftest";
        char *arguments[] = {program, selftest};
        if (!example_init(app, "Vulkan validation configuration", 160, 120, 2, arguments))
        {
            example_shutdown(app);
            return 1;
        }
        const bool requested = app.boot.vulkan.validation;
        std::vector<u8> pixels(static_cast<usize>(app.width) * app.height * 4);
        begin_frame(app.ctx, *app.win, 0, 1.0 / 60.0);
        {
            ui u(app.ctx);
            u.draw_rect({0, 0, 160, 120}, {19, 41, 67, 255});
            u.draw_rounded_rect({7.25f, 9.75f, 71.5f, 63.25f}, {91, 57, 131, 179}, 11);
        }
        end_frame(app.ctx);
        const bool read = app.device->read_pixels(app.surface->scene_target(), 0, 0, app.width,
                                                  app.height, pixels.data());
        const i32 shutdown = example_shutdown(app);
        if (requested != test.requested || !read || shutdown ||
            (!expected.empty() && pixels != expected))
        {
            std::fprintf(stderr,
                         "FAIL validation value=%s requested=%d expected=%d read=%d "
                         "shutdown=%d\n",
                         test.value ? test.value : "<unset>", requested, test.requested, read,
                         shutdown);
            return 1;
        }
        expected = std::move(pixels);
    }
    std::puts("all Vulkan example validation configuration tests passed");
    return 0;
}
