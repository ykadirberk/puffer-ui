// Pixel fingerprints captured from the pre-optimization implementation.
// The original golden scenes/files are intentionally untouched.
#include <pufferui/pufferui.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <string>
#include <vector>
using namespace pui;

int main()
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window *win = SDL_CreateWindow("performance pixels", 640, 360, SDL_WINDOW_HIDDEN);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, "software") : nullptr;
    if (!ren) return 1;
    render_device *dev = create_sdl3_device(ren);
    render_surface *surface = dev->create_surface(win);
    context *c = create_context(dev, surface);
    const font_handle font = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (font == FONT_INVALID) return 1;
    std::string label;
    for (i32 i = 0; i < 100; ++i) label += "AV To ffi \xC3\xA7\xC4\x9F\xC5\x9F ";
    const char *headers[40];
    for (auto &h : headers) h = "Column";
    std::vector<u8> pixels(640 * 360 * 4);
    const f32 radii[] = {2, 6, 24, 48, 2, 4, 20, 21};
    constexpr u64 expected[] = {
        0xd904392488e6d787ull, 0x05ca95c9537e462aull, 0x4b51883c9cb41ffdull, 0x6a2301fd331802caull,
        0xce9560a7f8373a51ull, 0x70c78db5feefab2full, 0x3dc72ef187a8abfdull, 0x8e99b36075e19b34ull,
        0x6654569d0712a0e0ull, 0xd539e818912e8773ull, 0x4b51883c9cb41ffdull, 0xd807222dd06c6a11ull,
        0x62f965bbf4933a9aull, 0x70c78db5feefab2full, 0x38c0d7bda047cd5cull, 0x80cc3783088d3f0bull};
    i32 failures = 0;
    for (i32 f = 0; f < 16; ++f)
    {
        theme t = default_dark();
        t.font = font;
        t.text_size = 14.0f + static_cast<f32>(f % 3) * 0.03125f;
        t.text_gamma = f % 3 == 0 ? 1.0f : f % 3 == 1 ? 1.351f : 1.354f;
        set_theme(c, t);
        begin_frame(c, f / 60.0, 1.0 / 60.0, {0, 0, 640, 360});
        {
            ui u(c);
            for (i32 x = 0; x < 640; x += 7)
                u.draw_rect({static_cast<f32>(x), 0, 3, 150}, {static_cast<u8>(x), 120, 200, 255});
            u.blur({11, 13, 173, 101}, radii[f % 8], 9, 0.85f);
            u.blur({221, 31, 129, 67}, radii[(f + 3) % 8], 0, 1);
            u.text_ellipsis({17.25f, 120, 119.0f + f % 4, 24}, label, color::white());
            u.text({200.25f, 120, 400, 24}, "AV glyphs 0123456789", color::white());
            comp::table(u, {8, 160, 620, 190}, std::span(headers, f % 2 ? 40 : 12), 3,
                        [&](ui &uu, rect r, i32 row, i32 col)
                        {
                            if (row == 0 && col == 0)
                                comp::table(uu, r, std::span(headers, 2), 0, {},
                                            {.id = uu.local("nested"), .header_h = 12});
                            else
                                uu.text(r, "Cell", color::white());
                        },
                        {.id = "table"_id});
        }
        end_frame(c);
        if (!dev->read_pixels(surface->scene_target(), 0, 0, 640, 360, pixels.data())) return 1;
        u64 hash = 1469598103934665603ull;
        for (u8 b : pixels) hash = (hash ^ b) * 1099511628211ull;
        if (hash != expected[f])
        {
            std::printf("FAIL frame=%d hash=0x%016llx expected=0x%016llx\n", f,
                        static_cast<unsigned long long>(hash),
                        static_cast<unsigned long long>(expected[f]));
            ++failures;
        }
        if (violation_count(c)) ++failures;
    }
    destroy_context(c);
    destroy_sdl3_device(dev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (!failures) std::printf("all 16 performance pixel baselines matched\n");
    return failures ? 1 : 0;
}
