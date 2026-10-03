// Golden-image tests: render fixed scenes headlessly with SDL's offscreen video
// driver + software renderer, then compare hashes and pixels against committed
// goldens under tests/golden/.
//
//   pui_golden_tests                 compare (default)
//   pui_golden_tests --update        rewrite goldens + hashes (review the BMPs!)
//   pui_golden_tests --scene text    run one scene
//
// Goldens are pinned to the vendored SDL software renderer and the bundled font;
// do not update them without eyeballing the regenerated images.

#include <pufferui/pufferui.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace pui;

// ---------------------------------------------------------------- pixel helpers
static u64 hash_pixels(const u8 *rgba, usize bytes)
{
    u64 h = 1469598103934665603ull; // FNV-1a 64
    for (usize i = 0; i < bytes; ++i)
    {
        h ^= rgba[i];
        h *= 1099511628211ull;
    }
    return h;
}

static bool write_bmp(const char *path, const u8 *rgba, i32 w, i32 h)
{
    const i32 row = w * 4;
    const i32 data_size = row * h;
    std::vector<u8> out(static_cast<usize>(54 + data_size), 0);

    out[0] = 'B';
    out[1] = 'M';
    const i32 file_size = 54 + data_size;
    std::memcpy(&out[2], &file_size, 4);
    const i32 offset = 54;
    std::memcpy(&out[10], &offset, 4);
    const i32 dib = 40;
    std::memcpy(&out[14], &dib, 4);
    std::memcpy(&out[18], &w, 4);
    std::memcpy(&out[22], &h, 4);
    const i16 planes = 1, bpp = 32;
    std::memcpy(&out[26], &planes, 2);
    std::memcpy(&out[28], &bpp, 2);
    std::memcpy(&out[34], &data_size, 4);
    const i32 ppm = 2835;
    std::memcpy(&out[38], &ppm, 4);
    std::memcpy(&out[42], &ppm, 4);

    for (i32 y = 0; y < h; ++y)
    { // BMP rows are bottom-up
        const u8 *src = rgba + static_cast<usize>(h - 1 - y) * row;
        u8 *dst = out.data() + 54 + static_cast<usize>(y) * row;
        for (i32 x = 0; x < w; ++x)
        {
            dst[x * 4 + 0] = src[x * 4 + 2]; // B
            dst[x * 4 + 1] = src[x * 4 + 1]; // G
            dst[x * 4 + 2] = src[x * 4 + 0]; // R
            dst[x * 4 + 3] = 255;
        }
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
    return f.good();
}

static bool read_bmp(const char *path, std::vector<u8> &rgba, i32 &w, i32 &h)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize size = f.tellg();
    f.seekg(0);
    if (size < 54) return false;

    std::vector<u8> raw(static_cast<usize>(size));
    f.read(reinterpret_cast<char *>(raw.data()), size);
    if (!f || raw[0] != 'B' || raw[1] != 'M') return false;

    i32 offset = 0, dib = 0;
    i16 bpp = 0;
    std::memcpy(&offset, &raw[10], 4);
    std::memcpy(&dib, &raw[14], 4);
    std::memcpy(&w, &raw[18], 4);
    std::memcpy(&h, &raw[22], 4);
    std::memcpy(&bpp, &raw[28], 2);
    if (dib < 40 || bpp != 32 || w <= 0 || h == 0) return false;
    const bool top_down = h < 0;
    if (top_down) h = -h;
    if (offset + w * h * 4 > static_cast<i32>(raw.size())) return false;

    rgba.assign(static_cast<usize>(w) * h * 4, 0);
    for (i32 y = 0; y < h; ++y)
    {
        const i32 src_row = top_down ? y : (h - 1 - y);
        const u8 *src = raw.data() + offset + static_cast<usize>(src_row) * w * 4;
        u8 *dst = rgba.data() + static_cast<usize>(y) * w * 4;
        for (i32 x = 0; x < w; ++x)
        {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    return true;
}

// ---------------------------------------------------------------- golden env
inline constexpr i32 GOLDEN_W = 320;
inline constexpr i32 GOLDEN_H = 200;

struct golden_env
{
    SDL_Window *sdl_window = nullptr;
    SDL_Renderer *renderer = nullptr;
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    context *ctx = nullptr;
    window *windows[2]{};
    i32 window_count = 0;
    font_handle font = FONT_INVALID;
    font_handle font_bold = FONT_INVALID;
    font_handle font_oblique = FONT_INVALID;
    texture_handle nine_slice_tex = nullptr; // kept alive for the scene lifetime
    std::vector<u8> pixels;                  // RGBA readback
};

static bool env_init(golden_env &e, i32 window_count)
{
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR); // offscreen probing is noisy
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("golden: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    e.sdl_window = SDL_CreateWindow("golden", GOLDEN_W, GOLDEN_H, SDL_WINDOW_HIDDEN);
    if (!e.sdl_window)
    {
        std::printf("golden: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    e.renderer = SDL_CreateRenderer(e.sdl_window, "software");
    if (!e.renderer)
    {
        std::printf("golden: software renderer unavailable: %s\n", SDL_GetError());
        return false;
    }

    e.device = create_sdl3_device(e.renderer);
    if (!e.device) return false;
    e.surface = e.device->create_surface(e.sdl_window);
    e.ctx = create_context(e.device, e.surface);

    e.font = load_font(e.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (e.font == FONT_INVALID)
    {
        std::printf("golden: bundled font missing\n");
        return false;
    }
    e.font_bold = load_font(e.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Bold.ttf");
    e.font_oblique = load_font(e.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans-Oblique.ttf");
    theme t = default_dark();
    t.font = e.font;
    t.text_size = 14.0f;
    t.button.transition = transition{0.0f, easing::LINEAR}; // no animation noise
    set_theme(e.ctx, t);

    const rect client = rect::make(0, 0, static_cast<f32>(GOLDEN_W), static_cast<f32>(GOLDEN_H));
    for (i32 i = 0; i < window_count; ++i)
    {
        // Both windows share one surface so either scene can be captured from the
        // single offscreen renderer (multi-surface is covered by the demo/tests).
        void *handle = reinterpret_cast<void *>(static_cast<usize>(i + 1));
        e.windows[i] = add_window(e.ctx, handle, e.surface, client);
        if (!e.windows[i]) return false;
    }
    e.window_count = window_count;
    focus_window(e.ctx, *e.windows[0]);
    e.pixels.assign(static_cast<usize>(GOLDEN_W) * GOLDEN_H * 4, 0);

    // Procedural 9-slice atlas (kept alive while any scene draws with it).
    std::vector<u8> px(static_cast<usize>(32) * 32 * 4, 0);
    for (i32 y = 0; y < 32; ++y)
    {
        for (i32 x = 0; x < 32; ++x)
        {
            const bool edge = x < 6 || y < 6 || x >= 26 || y >= 26;
            u8 *p = px.data() + (static_cast<usize>(y) * 32 + x) * 4;
            p[0] = edge ? 60 : 90;
            p[1] = edge ? 90 : 130;
            p[2] = edge ? 160 : 200;
            p[3] = 255;
        }
    }
    e.nine_slice_tex = e.device->create_texture(32, 32, px.data());
    return e.nine_slice_tex != nullptr;
}

static void env_shutdown(golden_env &e)
{
    if (e.device && e.nine_slice_tex) e.device->destroy_texture(e.nine_slice_tex);
    if (e.ctx) destroy_context(e.ctx);
    if (e.device) destroy_sdl3_device(e.device);
    if (e.renderer) SDL_DestroyRenderer(e.renderer);
    if (e.sdl_window) SDL_DestroyWindow(e.sdl_window);
    SDL_Quit();
    e = golden_env{};
}

static bool env_capture(golden_env &e)
{
    SDL_Surface *raw = SDL_RenderReadPixels(e.renderer, nullptr);
    if (!raw) return false;
    SDL_Surface *conv = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);
    if (!conv) return false;
    const bool ok = conv->w == GOLDEN_W && conv->h == GOLDEN_H;
    if (ok)
    {
        const u8 *src = static_cast<const u8 *>(conv->pixels);
        for (i32 y = 0; y < GOLDEN_H; ++y)
        {
            std::memcpy(e.pixels.data() + static_cast<usize>(y) * GOLDEN_W * 4,
                        src + static_cast<usize>(y) * conv->pitch,
                        static_cast<usize>(GOLDEN_W) * 4);
        }
    }
    SDL_DestroySurface(conv);
    return ok;
}

// ---------------------------------------------------------------- scenes
// A scene renders `frames` outer frames at fixed times; each frame from
// `capture_from` on is hashed as `<name>` (single frame) or `<name>_f<frame>`.
struct golden_scene
{
    const char *name;
    i32 window_count = 1;
    i32 target_window = 0;
    i32 frames = 1;
    i32 capture_from = 0;
    void (*input)(golden_env &, i32 frame) = nullptr;
    void (*draw)(golden_env &, i32 window_index, i32 frame) = nullptr;
    // Per-pixel clamp for the compare. Blur scenes raise it: the multi-step
    // bilinear chain accumulates sampling noise between /fp:fast and precise
    // builds, which moves a few soft-edge pixels by more than the default 24.
    i32 max_delta = 24;
};

static f64 frame_time(i32 frame)
{
    return 0.1 + static_cast<f64>(frame) * (1.0 / 60.0);
}

// ---- shapes
static void draw_shapes(golden_env &e, i32, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);

    u.draw_rounded_rect(rect::make(12, 12, 90, 60), th.widget_bg, 0.0f);
    u.draw_rounded_rect(rect::make(114, 12, 90, 60), th.widget_bg, 8.0f);
    u.draw_rounded_rect(rect::make(216, 12, 90, 60), th.panel_bg, 30.0f);
    u.draw_rect(rect::make(12, 84, 90, 40), th.accent);
    u.draw_line(12, 132, 306, 132, th.border, 2.0f);

    const vec2 poly[5] = {{12, 186}, {42, 148}, {78, 186}, {30, 168}, {60, 168}};
    u.draw_polygon(std::span<const vec2>(poly, 5), th.accent_hover);

    u.draw_sector(vec2{150, 160}, 10.0f, 34.0f, -1.0f, 1.4f, th.widget_hover);
    u.draw_arc(vec2{240, 160}, 26.0f, 6.0f, 0.2f, 2.8f, th.accent);

    const skin_image img = make_skin_image(e.nine_slice_tex, 32, 32, rect::make(0, 0, 32, 32), 6, 6,
                                           6, 6, SKIN_CENTER_STRETCH);
    u.draw_nine_slice(img, rect::make(150, 92, 156, 44));
}

// ---- text
static void draw_text(golden_env &e, i32, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);

    {
        text_scope title = u.text_style(16.0f, e.font_bold);
        u.text(rect::make(10, 8, 300, 18), "PufferUI golden: text", th.text, ALIGN_LEFT);
    }
    u.text(rect::make(10, 28, 140, 18),
           "T\xC3\xBCrk\xC3\xA7"
           "e \xC4\x9F\xC3\xBC\xC5\x9F\xC3\xB6",
           th.text, ALIGN_LEFT); // Türkçe ğüşö
    u.text(rect::make(160, 28, 150, 18), "\xCE\xB1\xCE\xB2\xCE\xB3 \xCE\x94 0.5 \xC2\xB5m", th.text,
           ALIGN_LEFT); // αβγ Δ 0.5 µm
    u.text(rect::make(10, 46, 300, 18),
           "x \xC3\x97 y \xC3\xB7 z \xC2\xB1 1 \xE2\x89\xA4 2 \xE2\x89\xA0 3", th.text,
           ALIGN_LEFT); // × ÷ ± ≤ ≠
    u.text(rect::make(10, 64, 300, 18),
           "\xE2\x82\xBA 120 \xE2\x82\xAC 5 \xC2\xB2 \xC2\xB3 \xE2\x86\x92", th.text,
           ALIGN_LEFT); // ₺ € ² ³ →
    u.text(rect::make(10, 82, 300, 18), "\xE2\x9C\x93 \xE2\x9C\x94 \xE2\x9C\x97", th.accent,
           ALIGN_LEFT);
    // kerning pairs are pinned here
    u.text(rect::make(10, 100, 300, 18), "AVATAR To Wa 1/2 \xE2\x80\x94 kerning", th.text,
           ALIGN_LEFT);
    {
        text_scope obl = u.text_style(14.0f, e.font_oblique);
        u.text(rect::make(10, 118, 300, 18), "oblique sample text", th.text, ALIGN_LEFT);
    }
    u.text(rect::make(10, 136, 100, 18), "left", th.text, ALIGN_LEFT);
    u.text(rect::make(110, 136, 100, 18), "centered", th.text, ALIGN_CENTER);
    u.text(rect::make(210, 136, 100, 18), "right", th.text, ALIGN_RIGHT);
    // ellipsis trimming
    u.text_ellipsis(rect::make(10, 158, 140, 18), "a very long label that gets trimmed",
                    th.text_dim, ALIGN_LEFT);
    u.text(rect::make(160, 158, 150, 18), "short", th.text_dim, ALIGN_LEFT);
    u.text_fit(rect::make(10, 176, 140, 22), "wrapped text that does not fit on one line",
               th.text_dim);
}

// ---- widgets
static std::string g_widget_name;
static f32 g_widget_amount = 3.5f;

static void widgets_input(golden_env &e, i32 frame)
{
    window &w = *e.windows[0];
    if (frame == 0)
    {
        mouse_move(w, 40.0f, 52.0f);
        mouse_button(w, true);
    }
    else if (frame == 1)
    {
        mouse_button(w, false);
        text_input_event(w, "Merhaba");
    }
    else if (frame == 2)
    {
        mods_event(w, false, true);
        key_event(w, key::A, true);
    }
    else if (frame == 3)
    {
        key_event(w, key::A, false);
        mods_event(w, false, false);
        mouse_move(w, 250.0f, 52.0f); // hover the right button
    }
    else if (frame == 4)
    {
        mouse_button(w, true); // hold: active state
    }
}

static void draw_widgets(golden_env &e, i32, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);
    u.text(rect::make(20, 14, 200, 18), "widgets", th.text, ALIGN_LEFT);

    u.text_field(rect::make(20, 40, 180, 26), g_widget_name, "golden_name"_id);
    u.number_field(rect::make(20, 76, 100, 26), g_widget_amount, "golden_amount"_id, "%.2f");
    (void)u.button(rect::make(250, 40, 60, 26), "Norm", "golden_norm"_id);
    (void)u.button(rect::make(220, 76, 90, 26), "State", "golden_state"_id, "primary"_id);

    u.card(rect::make(20, 116, 280, 48));
    u.text(rect::make(30, 126, 260, 18), "card content", th.text, ALIGN_LEFT);
    u.text_fit(rect::make(30, 146, 260, 18), "text_fit wraps when the line is too long",
               th.text_dim);

    // An outline button (transparent bg) and a translucent card over a bright
    // backdrop: the border has to stay a ring, so empty/translucent backgrounds
    // composite with the backdrop instead of the border color.
    (void)u.button(rect::make(20, 168, 90, 26), "Outline", "golden_outline"_id, 0,
                   button_override{.bg = some(color{0, 0, 0, 0}),
                                   .border = some(th.accent),
                                   .border_thickness = some(1.5f)});
    u.draw_rect(rect::make(130, 166, 170, 30), color{220, 140, 60, 255});
    u.card(rect::make(130, 166, 170, 30), card_override{.bg = some(color{30, 40, 60, 110}),
                                                        .border = some(th.accent),
                                                        .border_thickness = some(1.5f)});
}

// ---- layers
static bool g_layers_popup = false;

static void layers_input(golden_env &e, i32 frame)
{
    window &w = *e.windows[0];
    if (frame == 0)
    {
        mouse_move(w, 60.0f, 32.0f);
        mouse_button(w, true);
    }
    else if (frame == 1)
    {
        mouse_button(w, false); // click opens the popup
    }
}

static void draw_layers(golden_env &e, i32, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);

    // a banded background so the blur has something to smear
    for (i32 i = 0; i < 8; ++i)
    {
        const u8 v = static_cast<u8>(30 + i * 18);
        u.draw_rect(rect::make(0.0f, static_cast<f32>(i * 25), static_cast<f32>(GOLDEN_W), 25.0f),
                    color{v, static_cast<u8>(60 + i * 10), static_cast<u8>(140 - i * 8), 255});
    }
    // high-contrast shapes inside the blur region: the smear has to be obvious
    u.draw_rounded_rect(rect::make(178.0f, 34.0f, 56.0f, 56.0f), color{235, 240, 250, 255}, 28.0f);
    u.draw_rect(rect::make(252.0f, 26.0f, 44.0f, 78.0f), color{225, 120, 60, 255});

    if (u.button(rect::make(20, 20, 90, 26), "Open", "golden_open"_id, "primary"_id))
        g_layers_popup = true;

    const rect float_bounds = {160.0f, 20.0f, 150.0f, 92.0f};
    u.blur(float_bounds, 14.0f, 8.0f, 1.0f);
    rect fb = float_bounds;
    {
        // A translucent frosted surface over the blurred rect: the smear stays
        // visible (an opaque panel would hide it completely).
        panel_scope p =
            u.panel("Blur", fb,
                    {.id = "blur_panel"_id,
                     .flags = PANEL_NO_CONTROLS,
                     .style = panel_override{.bg = some(color{70, 100, 150, 90}),
                                             .titlebar_bg = some(color{30, 40, 60, 120})}});
        column pc(p.content(), 4.0f);
        u.text(pc.next(16.0f), "blur", th.text, ALIGN_LEFT);
        u.text(pc.next(14.0f), "panel", th.text_dim, ALIGN_LEFT);
    }

    if (g_layers_popup)
    {
        const rect menu = {20.0f, 64.0f, 140.0f, 96.0f};
        popup_scope p = u.popup("golden_popup"_id, menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
        style_scope sc(u, button_override{.radius = some(0.0f)});
        u.draw_rounded_rect(menu, th.panel_bg, 6.0f);
        u.draw_rect(rect::make(menu.x, menu.y, menu.w, 2.0f), th.accent);
        u.text(rect::make(menu.x, menu.y + 6.0f, menu.w, 20.0f), "Menu", th.text, ALIGN_CENTER);
        column items(menu.pad(10.0f, 30.0f, 10.0f, 10.0f), 4.0f);
        (void)u.button(items.next(24.0f), "One", "golden_popup_a"_id, "primary"_id);
        (void)u.button(items.next(24.0f), "Two", "golden_popup_b"_id);
    }
}

// ---- menus: combo dropdown, tooltip and context menu
static i32 g_menus_kind = 0;
static bool g_menus_fired = false;

static void menus_input(golden_env &e, i32 frame)
{
    window &w = *e.windows[0];
    if (frame == 0)
    {
        mouse_move(w, 100.0f, 40.0f); // over the closed combo header
        mouse_button(w, true);
    }
    else if (frame == 1)
    {
        mouse_button(w, false); // click opens the dropdown
    }
    else if (frame == 2)
    {
        mouse_move(w, 100.0f, 72.0f); // hover the second dropdown row
    }
    else if (frame == 3)
    {
        mouse_move(w, 300.0f, 140.0f);                // over the canvas...
        mouse_button(w, pointer_button::RIGHT, true); // ...right-press opens the menu
    }
    else if (frame == 4)
    {
        mouse_button(w, pointer_button::RIGHT, false);
    }
    else if (frame == 5)
    {
        // Keep the pointer put: hovering the first canvas row highlights it.
        mouse_move(w, 300.0f, 152.0f);
    }
}

static void draw_menus(golden_env &e, i32, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);
    u.text(rect::make(20, 10, 200, 16), "combo / tooltip / context menu", th.text_dim, ALIGN_LEFT);

    // Combo: the dropdown opens below the header and stays open for frames 2+.
    static const char *kinds[4] = {"Sprite", "Emitter", "Light", "Camera"};
    if (u.combo(rect::make(20, 28, 170, 26), "add", kinds, 4, g_menus_kind, "golden_combo"_id,
                90.0f))
        g_menus_fired = true;

    // A bright card drawn AFTER the combo, overlapping the open dropdown: the
    // deferred overlay draw must keep the dropdown on top of it (z-order proof).
    u.draw_rounded_rect(rect::make(30, 70, 120, 60), color{220, 140, 60, 255}, 6.0f);
    u.text(rect::make(40, 92, 100, 16), "later card", th.bg, ALIGN_LEFT);

    // A hovered button with a tooltip (past its delay from frame 1 on: t grows).
    (void)u.button(rect::make(240, 20, 70, 24), "Save", "golden_tt_btn"_id);
    u.tooltip(rect::make(240, 20, 70, 24), "golden_tt_btn"_id, "Write the scene to disk");

    // Canvas + context menu (right-press to open, at the pointer).
    const rect canvas = rect::make(200, 110, 220, 120);
    u.draw_rounded_rect(canvas, th.panel_bg, 6.0f);
    u.text(canvas, "right-press me", th.text_dim, ALIGN_CENTER);
    static const char *actions[3] = {"Fit to view", "Duplicate", "Delete"};
    const i32 picked = u.context_menu("golden_menu"_id, canvas, actions, 3);
    if (picked >= 0) g_menus_fired = true;

    // If anything was picked, note it (state persists so the capture shows it).
    if (g_menus_fired) u.text(rect::make(20, 130, 170, 18), "picked", th.accent, ALIGN_LEFT);
}

// ---- anim
static void draw_anim(golden_env &e, i32, i32 frame)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);

    const f32 t = u.animate("golden_t"_id, 1.0f, spring{260.0f, 0.7f});
    u.draw_rounded_rect(rect::make(20.0f + 200.0f * t, 60.0f, 60.0f, 40.0f), th.accent, 8.0f);

    const f32 tw =
        u.animate("golden_tw"_id, frame >= 1 ? 1.0f : 0.0f, tween{0.2f, easing::EASE_OUT});
    u.draw_rect(rect::make(20, 140, 280.0f * tw, 8.0f), th.widget_hover);
    u.text(rect::make(20, 160, 200, 18), "anim", th.text_dim, ALIGN_LEFT);
}

// ---- windows (two windows, one captured per scene)
static void draw_windows(golden_env &e, i32 wi, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);

    if (wi == 0)
    {
        u.draw_rect(rect::make(0, 0, GOLDEN_W, 34), th.panel_bg);
        u.text(rect::make(10, 8, 300, 18), "window one", th.text, ALIGN_LEFT);
        (void)u.button(rect::make(20, 50, 100, 28), "A", "golden_wa"_id, "primary"_id);
        u.card(rect::make(20, 90, 280, 60));
        u.text(rect::make(30, 100, 260, 18), "shared device", th.text_dim, ALIGN_LEFT);
    }
    else
    {
        u.draw_rect(rect::make(0, 0, GOLDEN_W, 34), th.panel_bg);
        u.text(rect::make(10, 8, 300, 18), "window two", th.text, ALIGN_LEFT);
        (void)u.button(rect::make(200, 50, 100, 28), "B", "golden_wb"_id, "danger"_id);
        u.text_fit(rect::make(20, 90, 280, 40),
                   "second window with its own focus list and dock tree", th.text_dim);
    }
}

// ---------------------------------------------------------------- runner
// ---- layout (track row, auto-fit grid, scroll views)
static void layout_input(golden_env &e, i32 frame)
{
    if (frame == 1)
    {
        // one wheel notch over the overlay scroll view
        mouse_move(*e.windows[0], 220.0f, 165.0f);
        mouse_wheel(*e.windows[0], 0.0f, -1.0f);
    }
}

static void draw_layout(golden_env &e, i32, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);

    // track-sized form row
    std::string name = "PufferUI";
    track_row tr(rect::make(10, 10, 300, 26),
                 {track_size::fixed(56.0f), track_size::flex(), track_size::fixed(64.0f)}, 8.0f);
    u.text(tr.next(), "Name", th.text, ALIGN_LEFT);
    u.text_field(tr.next(), name, "g_name"_id);
    (void)u.button(tr.next(), "Go", "g_go"_id);

    // auto-fit grid of cards (integral cell width keeps goldens fp-stable)
    grid_cursor g = auto_fit_grid(rect::make(10, 46, 300, 92), 6, 90.0f, 42.0f, 9.0f);
    for (i32 i = 0; i < g.count(); ++i)
    {
        const rect cell = g.next();
        u.card(cell);
        char label[16];
        std::snprintf(label, sizeof(label), "Cell %d", i + 1);
        u.text(cell.pad(8.0f, 0.0f), label, th.text, ALIGN_LEFT);
    }

    // scroll view: the bar shows from frame 1 on, content is clipped. Scoped so its
    // clip is popped before the next view: a live sibling would be its parent clip.
    {
        scroll_view sv = u.scroll(rect::make(10, 148, 140, 42), "g_scroll"_id,
                                  scroll_options{4.0f, SCROLL_ALWAYS_RESERVE_BAR});
        column col(sv.content(), 4.0f);
        for (i32 i = 0; i < 5; ++i)
        {
            const rect r = col.next(16.0f);
            u.draw_rect(r, i == 0 ? th.accent : th.widget_bg);
            char label[16];
            std::snprintf(label, sizeof(label), "row %d", i + 1);
            u.text(r.pad(6.0f, 0.0f), label, th.text, ALIGN_LEFT);
        }
        sv.set_content_height(5.0f * 16.0f + 4.0f * 4.0f);
    }

    // overlay scroll view, scrolled by the input callback
    scroll_view ov = u.scroll(rect::make(160, 148, 140, 42), "g_overlay"_id,
                              scroll_options{4.0f, SCROLL_OVERLAY});
    column ocol(ov.content(), 4.0f);
    for (i32 i = 0; i < 5; ++i)
    {
        const rect r = ocol.next(16.0f);
        u.draw_rect(r, th.widget_bg);
        char label[16];
        std::snprintf(label, sizeof(label), "ov %d", i + 1);
        u.text(r.pad(6.0f, 0.0f), label, th.text_dim, ALIGN_LEFT);
    }
    ov.set_content_height(5.0f * 16.0f + 4.0f * 4.0f);
}

// ---- blur: alignment, rounded corners and radius response
static void draw_blur_align(golden_env &e, i32, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);

    // Sharp reference markers.
    u.draw_rounded_rect(rect::make(14, 34, 30, 30), color{240, 244, 250, 255}, 6.0f);
    u.draw_rounded_rect(rect::make(14, 94, 30, 30), color{230, 130, 50, 255}, 6.0f);
    u.text(rect::make(6, 12, 60, 14), "sharp", th.text_dim, ALIGN_LEFT);

    // The same markers blurred at three radii: a bigger radius must smear more
    // (the pyramid depth + extra passes are driven by the radius).
    const f32 radii[3] = {3.0f, 12.0f, 28.0f};
    for (i32 i = 0; i < 3; ++i)
    {
        const f32 x = 60.0f + static_cast<f32>(i) * 90.0f;
        u.draw_rounded_rect(rect::make(x + 12.0f, 34.0f, 30.0f, 30.0f), color{240, 244, 250, 255},
                            6.0f);
        u.draw_rounded_rect(rect::make(x + 12.0f, 94.0f, 30.0f, 30.0f), color{230, 130, 50, 255},
                            6.0f);
        u.blur(rect::make(x, 20.0f, 54.0f, 120.0f), radii[i], 10.0f, 1.0f);
        char label[24];
        std::snprintf(label, sizeof(label), "r=%.0f", static_cast<double>(radii[i]));
        u.text(rect::make(x, 148.0f, 54.0f, 14.0f), label, th.text_dim, ALIGN_CENTER);
    }

    // Text under a blur: with alpha 1 the blurred copy *replaces* the sharp
    // content, so a >=5px radius must make the text illegible. (A lower alpha
    // deliberately blends toward the sharp original, for fade-ins.)
    u.text(rect::make(10.0f, 172.0f, 140.0f, 20.0f), "readable text sample", th.text, ALIGN_LEFT);
    u.text(rect::make(170.0f, 172.0f, 140.0f, 20.0f), "readable text sample", th.text, ALIGN_LEFT);
    u.blur(rect::make(162.0f, 164.0f, 156.0f, 34.0f), 5.0f, 4.0f, 1.0f);
}

// ---- animated layout: six tiles switch from a 3x2 grid to a column on frame 1;
// frames 1 and 2 catch u.animate_rect mid-flight (springs from where each
// tile was, sizes included)
static void draw_relayout(golden_env &e, i32, i32 frame)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);
    const color cols[6] = {{230, 110, 90, 255}, {240, 180, 70, 255},  {120, 200, 110, 255},
                           {70, 190, 200, 255}, {100, 130, 240, 255}, {200, 110, 220, 255}};
    for (i32 i = 0; i < 6; ++i)
    {
        rect target{};
        if (frame == 0)
            target = rect::make(20.0f + static_cast<f32>(i % 3) * 96.0f,
                                20.0f + static_cast<f32>(i / 3) * 86.0f, 84.0f, 74.0f);
        else
            target = rect::make(200.0f, 14.0f + static_cast<f32>(i) * 30.0f, 100.0f, 24.0f);
        id_scope sc = u.scope(static_cast<uiid>(i + 1));
        const rect r = u.animate_rect(u.local("pos"), target, {.spec = spring{220.0f, 0.8f}});
        u.draw_rounded_rect(r, cols[i], 8.0f);
    }
}

// ---- blur strength: the edge spread must track the radius
static void draw_blur_edge(golden_env &e, i32, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);

    // A dark/light step edge per radius: the 10-90% transition width is the
    // effective blur, which must grow with the requested radius (text under a
    // >=5px blur has to be unreadable).
    const f32 radii[3] = {3.0f, 5.0f, 12.0f};
    for (i32 i = 0; i < 3; ++i)
    {
        const f32 x = 20.0f + static_cast<f32>(i) * 100.0f;
        u.draw_rect(rect::make(x, 40.0f, 40.0f, 120.0f), color{16, 18, 22, 255});
        u.draw_rect(rect::make(x + 40.0f, 40.0f, 40.0f, 120.0f), color{240, 244, 250, 255});
        u.blur(rect::make(x, 20.0f, 80.0f, 160.0f), radii[i], 0.0f, 1.0f);
        char label[24];
        std::snprintf(label, sizeof(label), "r=%.0f", static_cast<double>(radii[i]));
        u.text(rect::make(x, 186.0f, 80.0f, 14.0f), label, th.text_dim, ALIGN_CENTER);
    }
}

// ---- inputs: selection, drag & drop, clipping
static std::string g_inputs_a = "drag this text";
static std::string g_inputs_b;
static std::string g_inputs_c = "a long value that will not fit inside this field";

static void inputs_input(golden_env &e, i32 frame)
{
    window &w = *e.windows[0];
    if (frame == 0)
    {
        mouse_move(w, 30.0f, 33.0f);
        mouse_button(w, true);
    }
    else if (frame == 1)
    {
        mouse_button(w, false);
        mods_event(w, false, true);
        key_event(w, key::A, true); // select all
    }
    else if (frame == 2)
    {
        key_event(w, key::A, false);
        mods_event(w, false, false);
        mouse_move(w, 40.0f, 33.0f); // inside the selection
        mouse_button(w, true);
    }
    else if (frame == 3)
        mouse_move(w, 40.0f, 120.0f); // over the second field: drag in flight
}

static void draw_inputs(golden_env &e, i32, i32)
{
    ui u(e.ctx);
    const theme &th = u.th();
    u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), th.bg);

    u.text(rect::make(14, 6, 200, 14), "text input", th.text_dim, ALIGN_LEFT);
    u.text_field(rect::make(20, 20, 200, 26), g_inputs_a, "golden_in_a"_id);  // source: dimmed
    u.text_field(rect::make(20, 110, 200, 26), g_inputs_b, "golden_in_b"_id); // drop target
    u.text_field(rect::make(20, 150, 90, 26), g_inputs_c, "golden_in_c"_id);  // clipped long value
    u.text(rect::make(118, 156, 190, 14), "clipped long value", th.text_dim, ALIGN_LEFT);
}

static const golden_scene SCENES[] = {
    {"shapes", 1, 0, 1, 0, nullptr, draw_shapes},
    {"text", 1, 0, 1, 0, nullptr, draw_text},
    // Widget scenes raise the per-pixel clamp too: the outline ring corners
    // (sector annuli) shift a fraction of a pixel between /fp:fast and precise
    // builds.
    {"widgets", 1, 0, 5, 3, widgets_input, draw_widgets, 96},
    {"layers", 1, 0, 2, 1, layers_input, draw_layers},
    {"menus", 1, 0, 6, 2, menus_input, draw_menus},
    {"anim", 1, 0, 3, 0, nullptr, draw_anim},
    {"windows_a", 2, 0, 1, 0, nullptr, draw_windows},
    {"windows_b", 2, 1, 1, 0, nullptr, draw_windows},
    {"layout", 1, 0, 3, 2, layout_input, draw_layout},
    {"blur", 1, 0, 2, 1, nullptr, draw_blur_align, 96},
    {"blur_edge", 1, 0, 2, 1, nullptr, draw_blur_edge, 96},
    {"inputs", 1, 0, 4, 3, inputs_input, draw_inputs},
    {"relayout", 1, 0, 9, 7, nullptr, draw_relayout},
};
inline constexpr i32 SCENE_COUNT = static_cast<i32>(sizeof(SCENES) / sizeof(SCENES[0]));

struct golden_hashes
{
    std::vector<std::pair<std::string, u64>> entries;

    bool load(const char *path)
    {
        std::ifstream f(path);
        if (!f) return false;
        std::string name;
        while (f >> name)
        {
            if (!name.empty() && name[0] == '#')
            {
                std::getline(f, name);
                continue;
            }
            unsigned long long hash = 0;
            if (!(f >> std::hex >> hash)) break;
            entries.emplace_back(name, static_cast<u64>(hash));
        }
        return true;
    }
    u64 get(const std::string &name) const
    {
        for (const auto &kv : entries)
            if (kv.first == name) return kv.second;
        return 0;
    }
    void set(const std::string &name, u64 hash)
    {
        for (auto &kv : entries)
            if (kv.first == name)
            {
                kv.second = hash;
                return;
            }
        entries.emplace_back(name, hash);
    }
    bool save(const char *path) const
    {
        std::ofstream f(path);
        if (!f) return false;
        f << "# Generated by pui_golden_tests --update; do not edit by hand.\n";
        char line[128];
        for (const auto &kv : entries)
        {
            std::snprintf(line, sizeof(line), "%s %016llx\n", kv.first.c_str(),
                          static_cast<unsigned long long>(kv.second));
            f << line;
        }
        return f.good();
    }
};

static std::string golden_dir()
{
    return std::string(PUFFERUI_GOLDEN_DIR);
}
static std::string scene_path(const std::string &name, const char *ext)
{
    return golden_dir() + "/" + name + ext;
}

static i32 compare_pixels(const std::vector<u8> &a, const std::vector<u8> &b, i32 &max_delta)
{
    max_delta = 0;
    if (a.size() != b.size()) return -1;
    i32 differing = 0;
    for (usize i = 0; i < a.size(); i += 4)
    {
        i32 d = 0;
        for (i32 c = 0; c < 3; ++c)
        {
            const i32 dd = static_cast<i32>(a[i + static_cast<usize>(c)]) -
                           static_cast<i32>(b[i + static_cast<usize>(c)]);
            if (dd > d) d = dd;
            if (-dd > d) d = -dd;
        }
        if (d > 2) ++differing;
        if (d > max_delta) max_delta = d;
    }
    return differing;
}

static bool write_diff(const char *path, const std::vector<u8> &actual,
                       const std::vector<u8> &golden)
{
    std::vector<u8> diff(actual.size(), 0);
    for (usize i = 0; i < actual.size(); i += 4)
    {
        i32 d = 0;
        for (i32 c = 0; c < 3; ++c)
        {
            const i32 dd = static_cast<i32>(actual[i + static_cast<usize>(c)]) -
                           static_cast<i32>(golden[i + static_cast<usize>(c)]);
            if (dd > d) d = dd;
            if (-dd > d) d = -dd;
        }
        const u8 v = static_cast<u8>(d > 2 ? 255 : 40);
        diff[i + 0] = v;
        diff[i + 1] = d > 2 ? 0 : v;
        diff[i + 2] = d > 2 ? 0 : v;
        diff[i + 3] = 255;
    }
    return write_bmp(path, diff.data(), GOLDEN_W, GOLDEN_H);
}

static i32 run_scene(const golden_scene &sc, golden_hashes &hashes, bool update, const char *only,
                     i32 &captured)
{
    if (only && std::strcmp(only, sc.name) != 0) return 0;

    golden_env e;
    if (!env_init(e, sc.window_count))
    {
        env_shutdown(e);
        std::printf("FAIL %s: environment init failed\n", sc.name);
        return 1;
    }

    i32 failures = 0;
    std::vector<std::pair<std::string, std::vector<u8>>> images;

    for (i32 frame = 0; frame < sc.frames; ++frame)
    {
        if (sc.input) sc.input(e, frame);
        for (i32 wi = 0; wi < sc.window_count; ++wi)
        {
            window &w = *e.windows[wi];
            begin_frame(e.ctx, w, frame_time(frame), 1.0 / 60.0);
            {
                ui u(e.ctx);
                sc.draw(e, wi, frame);
            }
            end_frame(e.ctx);

            if (wi != sc.target_window || frame < sc.capture_from) continue;
            if (!env_capture(e))
            {
                std::printf("FAIL %s: pixel readback failed\n", sc.name);
                ++failures;
                continue;
            }
            std::string name = sc.name;
            if (sc.frames > 1) name += "_f" + std::to_string(frame);
            images.emplace_back(name, e.pixels);
        }
    }

    for (const auto &img : images)
    {
        const std::string &name = img.first;
        const std::vector<u8> &actual = img.second;
        const u64 hash = hash_pixels(actual.data(), actual.size());
        ++captured;

        if (update)
        {
            hashes.set(name, hash);
            if (!write_bmp(scene_path(name, ".bmp").c_str(), actual.data(), GOLDEN_W, GOLDEN_H))
            {
                std::printf("FAIL %s: could not write golden\n", name.c_str());
                ++failures;
            }
            std::printf("update %-12s 0x%016llx\n", name.c_str(),
                        static_cast<unsigned long long>(hash));
            continue;
        }

        const u64 golden_hash = hashes.get(name);
        if (golden_hash == 0)
        {
            std::printf("FAIL %-12s no golden hash (run --update)\n", name.c_str());
            ++failures;
            continue;
        }
        if (golden_hash != hash)
        {
            std::printf("FAIL %-12s hash 0x%016llx != golden 0x%016llx\n", name.c_str(),
                        static_cast<unsigned long long>(hash),
                        static_cast<unsigned long long>(golden_hash));
        }

        std::vector<u8> golden;
        i32 gw = 0, gh = 0;
        if (!read_bmp(scene_path(name, ".bmp").c_str(), golden, gw, gh))
        {
            std::printf("FAIL %-12s golden image missing or unreadable\n", name.c_str());
            ++failures;
            continue;
        }
        i32 max_delta = 0;
        const i32 differing = compare_pixels(actual, golden, max_delta);
        const i32 tolerance = (GOLDEN_W * GOLDEN_H) / 1000; // 0.1% of the frame
        // AA edges can shift a fraction of a pixel under /fp:fast (debug vs
        // release), which moves a handful of edge pixels by a small step —
        // especially through translucent stacks and blur chains, where each
        // layer amplifies the coverage change. `differing` (any channel > 2)
        // still catches real changes: a moved widget or a color tweak moves far
        // more pixels than the budget, so only single-pixel edge noise can pass.
        const bool pixels_ok =
            differing >= 0 && differing <= tolerance && max_delta <= sc.max_delta;
        if (!pixels_ok)
        {
            const std::string dump_actual = golden_dir() + "/dump/" + name + "_actual.bmp";
            const std::string dump_diff = golden_dir() + "/dump/" + name + "_diff.bmp";
            write_bmp(dump_actual.c_str(), actual.data(), GOLDEN_W, GOLDEN_H);
            write_diff(dump_diff.c_str(), actual, golden);
            std::printf("FAIL %-12s %d pixels differ (max delta %d); wrote dump/%s_*.bmp\n",
                        name.c_str(), differing < 0 ? -1 : differing, max_delta, name.c_str());
            ++failures;
        }
        else if (golden_hash == hash)
        {
            std::printf("ok   %-12s 0x%016llx\n", name.c_str(),
                        static_cast<unsigned long long>(hash));
        }
    }

    env_shutdown(e);
    return failures;
}

// ---------------------------------------------------------------- checks
// Regression: the SDL3 backend's blit BLENDED instead of copying (SDL_RenderTexture
// uses the texture's blend mode, not the draw blend mode), so blur-pyramid
// texels with alpha < 1 mixed with the scratch targets' previous contents. In
// an app shell - a page-wide fade blur, a wide frosted bar, a narrow frosted
// column - the column's frosted color slid a little darker every frame. A
// still scene must blur to the same pixels frame after frame.
static i32 check_blur_stable(const char *only)
{
    if (only && std::strcmp(only, "blur_stable") != 0) return 0;
    golden_env e;
    if (!env_init(e, 1))
    {
        env_shutdown(e);
        std::printf("FAIL blur_stable: environment init failed\n");
        return 1;
    }
    const color flat{80, 40, 160, 255};
    const color tint{255, 255, 255, 20};
    const rect fade_r = rect::make(60.0f, 0.0f, 260.0f, 200.0f);
    const rect bar = rect::make(70.0f, 8.0f, 240.0f, 44.0f);
    const rect side = rect::make(6.0f, 8.0f, 30.0f, 184.0f); // ~4 texels at 1/16
    i32 first[3] = {-1, -1, -1};
    i32 drift = 0;
    i32 failures = 0;
    for (i32 frame = 0; frame < 30; ++frame)
    {
        begin_frame(e.ctx, *e.windows[0], frame_time(frame), 1.0 / 60.0);
        {
            ui u(e.ctx);
            u.draw_rect(rect::make(0, 0, GOLDEN_W, GOLDEN_H), flat);
            u.blur(fade_r, 16.0f, 0.0f, 0.5f);
            u.blur(bar, 28.0f, 12.0f, 1.0f);
            u.draw_rounded_rect(bar, tint, 12.0f);
            u.blur(side, 22.0f, 12.0f, 1.0f);
            u.draw_rounded_rect(side, tint, 12.0f);
        }
        end_frame(e.ctx);
        if (!env_capture(e))
        {
            std::printf("FAIL blur_stable: pixel readback failed\n");
            ++failures;
            break;
        }
        const u8 *p = e.pixels.data() + (static_cast<usize>(120) * GOLDEN_W + 21) * 4;
        for (i32 c = 0; c < 3; ++c)
        {
            if (first[c] < 0) first[c] = p[c];
            const i32 d = std::abs(static_cast<i32>(p[c]) - first[c]);
            drift = d > drift ? d : drift;
        }
    }
    env_shutdown(e);
    if (failures == 0 && drift > 1)
    {
        std::printf("FAIL blur_stable  a still scene's blur drifted by %d over 30 frames\n", drift);
        ++failures;
    }
    else if (failures == 0)
    {
        std::printf("ok   blur_stable  (drift %d over 30 frames)\n", drift);
    }
    return failures;
}

int main(int argc, char **argv)
{
    bool update = false;
    const char *only = nullptr;
    for (i32 i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--update") == 0)
            update = true;
        else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
            only = argv[++i];
    }

    golden_hashes hashes;
    hashes.load(scene_path("hashes", ".txt").c_str());

    i32 failures = 0, captured = 0;
    for (const golden_scene &sc : SCENES) failures += run_scene(sc, hashes, update, only, captured);
    if (!update) failures += check_blur_stable(only);

    if (update)
    {
        if (!hashes.save(scene_path("hashes", ".txt").c_str()))
        {
            std::printf("FAIL: could not write hashes.txt\n");
            ++failures;
        }
        else
        {
            std::printf("golden: updated %d scene(s)\n", captured);
        }
    }
    else
    {
        std::printf("golden: %d scene(s), %d failure(s)\n", captured, failures);
    }
    return failures == 0 ? 0 : 1;
}
