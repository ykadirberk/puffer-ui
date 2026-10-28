// The real example bootstrap must select the same font from any working directory.
#include "../examples/example_common.h"
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line%d %s\n", __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

namespace
{
namespace fs = std::filesystem;
struct working_directory
{
    fs::path original = fs::current_path();
    fs::path scratch =
        fs::path(PUFFERUI_ASSET_DIR).parent_path() / "out/test-tmp" /
        ("pui-example-font-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    working_directory() { fs::create_directories(scratch); }
    ~working_directory()
    {
        std::error_code error;
        fs::current_path(original, error);
        fs::remove(scratch, error); // only this test's empty directory
    }
};
} // namespace

int main()
{
    constexpr i32 WIDTH = 320, HEIGHT = 160, FRAMES = 4;
    working_directory directory;
    const fs::path project = fs::path(PUFFERUI_ASSET_DIR).parent_path();
    const std::string explicit_assets = std::string(PUFFERUI_ASSET_DIR) + "/.";
    std::vector<u8> expected;
    for (u32 variant = 0; variant < 3; ++variant)
    {
        fs::current_path(variant == 0 ? project : directory.scratch);
        example_app app;
        if (variant == 2) app.boot.asset_dir = explicit_assets.c_str();
        char program[] = "example-font-root", selftest[] = "--selftest",
             no_script[] = "--no-script";
        char *arguments[] = {program, selftest, no_script};
        REQUIRE(example_init(app, "Example font root regression", WIDTH, HEIGHT, 3, arguments));
        REQUIRE(app.boot.asset_dir &&
                std::string(app.boot.asset_dir) ==
                    (variant == 2 ? explicit_assets : std::string(PUFFERUI_ASSET_DIR)));
        REQUIRE(app.font != FONT_INVALID && app.font_bold != FONT_INVALID);
        const font_handle reference =
            load_font(app.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
        REQUIRE(reference != FONT_INVALID);
        std::vector<u8> pixels(WIDTH * HEIGHT * 4);
        for (i32 frame = 0; frame < FRAMES; ++frame)
        {
            begin_frame(app.ctx, *app.win, frame / 60.0, 1.0 / 60.0);
            {
                ui u(app.ctx);
                u.draw_rect({0, 0, WIDTH, HEIGHT}, {23, 37, 61, 255});
                f32 actual = 0, wanted = 0;
                {
                    auto text = u.text_style(17, app.font);
                    actual = u.text_width("Office ffi / PufferUI");
                    u.text({7.25f, 9.5f, 300, 34}, "Office ffi / PufferUI", {237, 223, 201, 255});
                }
                {
                    auto text = u.text_style(17, reference);
                    wanted = u.text_width("Office ffi / PufferUI");
                }
                REQUIRE(actual > 0 && actual == wanted);
                {
                    auto text = u.text_style(24, app.font_bold);
                    u.text({9, 51, 300, 43}, "Bundled font", {201, 231, 247, 193});
                }
            }
            end_frame(app.ctx);
            REQUIRE(!violation_count(app.ctx));
            REQUIRE(app.device->read_pixels(app.surface->scene_target(), 0, 0, WIDTH, HEIGHT,
                                            pixels.data()));
            if (variant == 0)
                expected.insert(expected.end(), pixels.begin(), pixels.end());
            else
                REQUIRE(std::equal(pixels.begin(), pixels.end(),
                                   expected.begin() + frame * pixels.size()));
        }
        REQUIRE(!vulkan_statistics(app.device).validation_errors);
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
        REQUIRE(vulkan_statistics(app.device).glyph_upload_bytes > 0);
#endif
        example_shutdown(app);
    }
    std::puts(
        "all example font-root tests passed (project, empty cwd, explicit root; 12 exact frames)");
    return 0;
}
