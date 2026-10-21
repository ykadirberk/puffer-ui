// New core-fix goldens, recorded with the unchanged 79c7007 implementation.
// Covers gutter scratch, long layout, animated rects, and sector cache collisions.
#include <pufferui/pufferui.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace pui;

int main(int argc, char **argv)
{
    const bool record = argc > 1 && std::strcmp(argv[1], "--record") == 0;
    const char *capture = argc > 2 ? argv[2] : nullptr;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window *win = SDL_CreateWindow("core performance goldens", 800, 600, SDL_WINDOW_HIDDEN);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, "software") : nullptr;
    if (!ren) return 1;
    render_device *dev = create_sdl3_device(ren);
    render_surface *surface = dev->create_surface(win);
    context *c = create_context(dev, surface);
    window *w = add_window(c, win, surface, {0, 0, 800, 600});
    if (!w) return 1;
    const font_handle regular = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    const font_handle bold = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    if (regular == FONT_INVALID || bold == FONT_INVALID) return 1;
    std::string document, wrapped, label;
    for (i32 i = 0; i < 1000; ++i) document += "AV To rect cutting engine\n";
    for (i32 i = 0; i < 20; ++i) wrapped += "layout AV To glyphs \xC3\xA7\xC4\x9F\xC5\x9F ";
    for (i32 i = 0; i < 100; ++i) label += "AV To ffi \xC3\xA7\xC4\x9F\xC5\x9F ";
    constexpr u64 expected[24] = {
        0xc373c19616154683ull, 0x96b5135023b6f5f0ull, 0x820c5b28e917f0fbull, 0x3c7bd2015358b308ull,
        0x0eea5d6844516c53ull, 0x51c18d5f36983654ull, 0x2a84813ea9a1010bull, 0x10ad844127966173ull,
        0x2ba8706aa1da92fcull, 0x407e89307ab3d20eull, 0x890c3e2b49667727ull, 0xe71f7730f62a5124ull,
        0x73280d1373bd3b8bull, 0xdc9a57812539dca4ull, 0x69ba2cb7d6312563ull, 0xaf0082e0559b9412ull,
        0xef410dd6b24dcea2ull, 0x6f0d26f378cc9ad0ull, 0xdc6aec50e4d38f1eull, 0x955bc77c702507a7ull,
        0xed48bb8cd03219b5ull, 0xd4370c28ef940c47ull, 0xae52d4f7e88933dbull, 0xdcd9e37e96b27204ull};
    std::vector<u8> pixels(800 * 600 * 4);
    i32 failures = 0;
    for (i32 f = 0; f < 24; ++f)
    {
        const f32 density = f < 12 ? 1.0f : 1.75f;
        set_window_scale(*w, density);
        theme t = default_dark();
        t.font = f % 5 ? regular : bold;
        t.text_size = 14.0f + (f % 3) * 0.03125f;
        t.text_gamma = f % 4 ? 1.0f : 1.351f;
        set_theme(c, t);
        mouse_move(c, 50, 50);
        if (f >= 3 && f < 7) mouse_wheel(c, 0, -3);
        const f64 now = f < 18 ? f / 60.0 : 6.0 + (f - 18) / 60.0;
        begin_frame(c, *w, now, 1.0 / 60.0);
        {
            ui u(c);
            u.text_area({8, 8, 220, 180}, document, "log"_id,
                        {.wrap = false, .read_only = true, .line_numbers = f % 4 != 2});
            u.text_area({236, 8, 150, 180}, wrapped, "wrapped"_id,
                        {.wrap = true, .read_only = true, .line_numbers = true});
            u.text({8.25f, 190, 380, 22}, label, t.text, ALIGN_LEFT);
            {
                auto disabled = u.disabled(f % 2 == 0);
                for (i32 i = 0; i < 5; ++i)
                {
                    const rect target{12.0f + i * 70.0f + (f % 7) * 1.25f, 232, 35, 30};
                    const rect r = u.animate_rect(100 + i, target, {.origin = {0, 220}});
                    u.draw_sector({r.x + 14, r.y + 14}, i % 2 ? 0.0f : 8.0f, 11.0f + i, -PI * 0.5f,
                                  PI * (i % 2 ? 1.5f : 0.5f), {50, 160, 230, 170});
                    u.button({r.x, 275, 50, 22}, "AV", 200 + i,
                             {.style = {.bg = some(color{20, 40, 80, 100}), .radius = some(8.0f)}});
                }
            }
        }
        end_frame(c);
        if (!dev->read_pixels(surface->scene_target(), 0, 0, 800, 600, pixels.data())) return 1;
        u64 hash = 1469598103934665603ull;
        for (u8 b : pixels) hash = (hash ^ b) * 1099511628211ull;
        if (record)
            std::printf("%d,0x%016llx\n", f, static_cast<unsigned long long>(hash));
        else if (hash != expected[f])
        {
            std::printf("FAIL core pixels frame=%d actual=0x%016llx expected=0x%016llx\n", f,
                        static_cast<unsigned long long>(hash),
                        static_cast<unsigned long long>(expected[f]));
            ++failures;
        }
        if (capture)
        {
            char path[1024];
            std::snprintf(path, sizeof(path), "%s-%d.bmp", capture, f);
            SDL_Surface *image =
                SDL_CreateSurfaceFrom(800, 600, SDL_PIXELFORMAT_RGBA32, pixels.data(), 800 * 4);
            if (!image || !SDL_SaveBMP(image, path)) ++failures;
            SDL_DestroySurface(image);
        }
        if (violation_count(c)) ++failures;
    }
    destroy_context(c);
    destroy_sdl3_device(dev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (!record && !failures) std::puts("all 24 core performance pixel baselines matched");
    return failures ? 1 : 0;
}
