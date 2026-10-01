// Shared test helpers (r88 split). Each area file includes this.
#pragma once
#include <pufferui/pufferui.h>

#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

using namespace pui;

inline int g_failures = 0;

// Expected violations are captured instead of printed, and the guard that fired
// is asserted by message (install with set_violation_handler).
inline const char *g_last_violation = nullptr;
inline i32 g_violation_events = 0;

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s  (%s:%d)\n", #x, __FILE__, __LINE__);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)
inline void capture_violation(void *, const char * /*condition*/, const char *message,
                              const char * /*file*/, i32 /*line*/)
{
    g_last_violation = message;
    ++g_violation_events;
}

inline bool violation_was(const char *expected)
{
    return g_last_violation != nullptr && std::strcmp(g_last_violation, expected) == 0;
}

// A recording render_device for draw-list snapshot tests: captures each flush
// as text ("draw tex=%d verts=%d v0=(x,y,a,r,g,b)") — platform-stable,
// reviewable, no pixels involved. `calls` holds one line per draw flush.
struct snapshot_device : null_device
{
    std::vector<std::string> calls;

    void draw(texture_handle tex, const vertex *verts, i32 vcount, const i32 *idx,
              i32 icount) override
    {
        char line[160];
        const vertex &v0 = verts[0];
        std::snprintf(line, sizeof(line), "draw tex=%d verts=%d v0=(%.0f,%.0f,%d,%d,%d,%d)",
                      static_cast<int>(reinterpret_cast<ptrdiff_t>(tex)), vcount,
                      static_cast<double>(v0.x), static_cast<double>(v0.y),
                      static_cast<int>(v0.c.a), static_cast<int>(v0.c.r),
                      static_cast<int>(v0.c.g), static_cast<int>(v0.c.b));
        calls.push_back(line);
        (void)idx;
        (void)icount;
    }
};

struct recorder_host : window_host
{
    i32 moves = 0;
    i32 resizes = 0;
    i32 minimizes = 0;
    i32 maximizes = 0;
    f32 last_x = 0.0f, last_y = 0.0f, last_w = 0.0f, last_h = 0.0f;
    // When set, toggle_maximize flips `w.maximized` the way the real event
    // pump reflects SDL's state (the OS reports the maximized flag).
    bool reflect_maximize = false;
    // zero: the base class default (no native chrome)
    void move_window(window &, f32 x, f32 y) override
    {
        moves += 1;
        last_x = x;
        last_y = y;
    }
    void resize_window(window &, f32 w, f32 h) override
    {
        resizes += 1;
        last_w = w;
        last_h = h;
    }
    void minimize_window(window &) override { minimizes += 1; }
    void toggle_maximize(window &w) override
    {
        maximizes += 1;
        if (reflect_maximize) w.maximized = !w.maximized;
    }
};

template <typename DrawFn> inline void tf_click(context *c, f32 x, f32 y, f64 t, DrawFn &&draw)
{
    mouse_move(c, x, y);
    mouse_button(c, true);
    tf_frame(c, t, draw);
    mouse_button(c, false);
    tf_frame(c, t + 0.016, draw);
}
template <typename DrawFn>
inline void tf_key(context *c, key k, bool ctrl, bool shift, f64 t, DrawFn &&draw)
{
    mods_event(c, shift, ctrl);
    key_event(c, k, true);
    tf_frame(c, t, draw);
    key_event(c, k, false);
    mods_event(c, false, false);
}
// The caret is measured in pixels, so these tests need the bundled font and a // real
// (null) device for the glyph atlas.

struct tf_env
{
    null_device nd;
    context *c = nullptr;
    explicit tf_env(i32 w = 300, i32 h = 200)
    {
        nd.my_surface.w = w;
        nd.my_surface.h = h;
        c = create_context(&nd, nd.create_surface());
    }
    ~tf_env() { destroy_context(c); }
    tf_env(const tf_env &) = delete;
    tf_env &operator=(const tf_env &) = delete;
};

inline bool tf_needs_font(context *c)
{
    const font_handle fh = load_font(c, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    if (fh == FONT_INVALID)
    {
        std::printf("note: bundled font not found; skipping text input test\n");
        return true;
    }
    theme t = default_dark();
    t.font = fh;
    set_theme(c, t);
    return false;
}

struct sv_state
{
    // model: components write directly
    i32 count = 0;
    bool open = false;
    // view: derived once per frame, then only read
    struct view_t
    {
        i32 count = 0;
        bool open = false;
        char label[32]{};

    } view;
    void update_view()
    {
        view.count = count;
        view.open = open;
        std::snprintf(view.label, sizeof(view.label), "count %d", count);
    }
};

template <typename DrawFn> inline void tf_frame(context *c, f64 t, DrawFn &&draw)
{
    begin_frame(c, t, 0.016, rect::make(0, 0, 300, 200));

    {
        ui u(c);
        draw(u);
    }
    end_frame(c);
}

inline bool popup_present(context *c, uiid id, bool prev)
{
    const i32 depth = prev ? c->prev_popup_depth : c->popup_depth;
    for (i32 i = 0; i < depth; ++i)
        if ((prev ? c->prev_popups : c->popups)[i].id == id) return true;
    return false;
}

struct test_clipboard : clipboard
{
    std::string data;
    bool get(std::string &out) override
    {
        out = data;
        return true;
    }
    void set(std::string_view text) override { data.assign(text); }
};

