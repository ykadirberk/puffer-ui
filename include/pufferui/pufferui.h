#pragma once
// ============================================================================
// PufferUI — redesigned core (immediate mode, organized around rect slicing).
//
// Single header, real declaration / implementation split. Define
// PUFFERUI_IMPLEMENTATION in exactly one translation unit.
//
// Implemented: Phase 0 (types, validation, rect algebra, context, region/ID
// stack, interaction, cursors), Phase 1 (draw list + batching, minimal theme,
// SDL3 backend), and text (UTF-8 decode, stb_truetype glyph atlas, textures,
// ui.text / ui.text_width / ui.button).
//
// Optional macros:
//   PUFFERUI_IMPLEMENTATION  - compile the implementation (one TU)
//   PUFFERUI_ENABLE_SDL3     - also compile the SDL3 renderer backend
//
// Naming: types/functions/variables lower_snake_case; macros and
// constants/enumerators UPPER_SNAKE_CASE.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <span>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <new>

namespace pui
{

// ---------------------------------------------------------------- core types
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;
using uiid = u64;
using texture_handle = void *;
using font_handle = i32;

inline constexpr f32 PI = 3.14159265358979323846f;
inline constexpr font_handle FONT_INVALID = -1;
inline constexpr u32 UTF8_END = 0xFFFFFFFFu;
inline constexpr u32 UTF8_REPLACEMENT = 0xFFFDu;

inline constexpr i32 MAX_ID_DEPTH = 64;
inline constexpr i32 MAX_CLIP_DEPTH = 64;
// MAX_FRAME_IDS was removed (r89): region-id duplicate checking is a per-frame
// hash set, complete at any scale. VIOL_FRAME_IDS_OVERFLOW stays in the enum
// for code stability but is no longer reachable.
inline constexpr i32 MAX_STYLE_SCOPES = 16;
inline constexpr i32 MAX_POPUPS = 8;
inline constexpr i32 MAX_PANELS = 16;
inline constexpr i32 MAX_DOCK_PANELS = 8;
inline constexpr i32 MAX_DOCK_DEPTH = 8;

// ---------------------------------------------------------------- validation

struct context;

context *current_context();
void set_current_context(context *);
// The current-context slot is thread-local: one context per thread. Threads
// that draw must create their own context (or serialize access to a shared
// one around whole frames).

// Every contract guard in the library, enumerated. `violation_last_code`
// hands the last one back (VIOL_NONE when clean); the codes are stable, and
// tests and overlays may rely on them. The guard text stays the human part.
enum violation_code : u32
{
    VIOL_NONE = 0,
    VIOL_INVALID_SLICE,         // a cut/cursor/split produced an invalid slice
    VIOL_INVALID_RECT,          // a draw call got an invalid rect
    VIOL_INVALID_AREA,          // a region/scroll area is invalid
    VIOL_DUP_REGION_ID,         // duplicate region id among siblings
    VIOL_WINDOW_LIMIT,          // add_window beyond MAX_WINDOWS
    VIOL_SHARED_DEVICE,         // a second window without backend_caps::SHARED_DEVICE
    VIOL_REMOVE_WHILE_OPEN,     // remove_window while a frame is open
    VIOL_DESTROY_WHILE_OPEN,    // destroy_context with an open window frame
    VIOL_FRAME_ALREADY_OPEN,    // begin_frame: another window frame is open
    VIOL_WINDOW_NOT_REGISTERED, // begin_frame: window is not registered
    VIOL_END_WITHOUT_BEGIN,     // end_frame without begin_frame
    VIOL_CLIP_UNBALANCED,       // region clip push without pop at end_frame
    VIOL_SCOPE_UNBALANCED,      // a button style scope left open or overflowed
    VIOL_POPUP_OVERFLOW,        // popup stack overflow
    VIOL_COMBO_EMPTY,           // combo needs at least one item
    VIOL_LAYOUT_CLAMPED,        // a requested slice was clamped (off by default)
    VIOL_NO_FONT,               // text drawn while the theme has no font
    VIOL_INPUT_OVERFLOW,        // text input did not fit the per-frame buffer
    VIOL_DUP_WIDGET_ID,         // the same widget id interacted twice in a frame
    VIOL_FRAME_IDS_OVERFLOW,    // over MAX_FRAME_IDS regions; dup checking incomplete
    VIOL_COUNT
};

void report_violation(violation_code code, const char *condition, const char *message,
                      const char *file, int line, bool fatal);

// Per-context violation sink. When set, every check failure is reported to it
// instead of stderr (the count still accumulates). Tests install one to assert
// the exact guard that fired; apps can use it to route violations to a log.
using violation_handler = void (*)(void *user, const char *condition, const char *message,
                                   const char *file, i32 line);
void set_violation_handler(context *c, violation_handler handler, void *user);

#define PUFFERUI_CHECK(code, cond, message)                                                        \
    do                                                                                             \
    {                                                                                              \
        if (!(cond)) ::pui::report_violation(code, #cond, message, __FILE__, __LINE__, false);     \
    } while (0)
#define PUFFERUI_ASSERT(code, cond, message)                                                       \
    do                                                                                             \
    {                                                                                              \
        if (!(cond)) ::pui::report_violation(code, #cond, message, __FILE__, __LINE__, true);      \
    } while (0)
#define PUFFERUI_VERIFY(cond)                                                                      \
    ((cond) ? true                                                                                 \
            : (::pui::report_violation(VIOL_NONE, #cond, "", __FILE__, __LINE__, false), false))

// ---------------------------------------------------------------- math helpers
inline constexpr f32 min2(f32 a, f32 b)
{
    return a < b ? a : b;
}
inline constexpr f32 max2(f32 a, f32 b)
{
    return a > b ? a : b;
}
inline constexpr f32 clampf(f32 v, f32 lo, f32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
inline constexpr bool is_finite(f32 v)
{
    return v == v && v > -3.0e38f && v < 3.0e38f;
}

// ---------------------------------------------------------------- UTF-8
// Decodes the next codepoint from s starting at index i (advanced past it).
// Returns UTF8_END at end of string, UTF8_REPLACEMENT on malformed input.
inline u32 utf8_decode(std::string_view s, usize &i)
{
    if (i >= s.size()) return UTF8_END;
    u8 c0 = static_cast<u8>(s[i++]);
    if (c0 < 0x80) return c0;

    i32 extra;
    u32 cp;
    if ((c0 & 0xE0) == 0xC0)
    {
        extra = 1;
        cp = c0 & 0x1Fu;
    }
    else if ((c0 & 0xF0) == 0xE0)
    {
        extra = 2;
        cp = c0 & 0x0Fu;
    }
    else if ((c0 & 0xF8) == 0xF0)
    {
        extra = 3;
        cp = c0 & 0x07u;
    }
    else
    {
        return UTF8_REPLACEMENT;
    }

    for (i32 k = 0; k < extra; ++k)
    {
        if (i >= s.size()) return UTF8_REPLACEMENT;
        u8 cc = static_cast<u8>(s[i++]);
        if ((cc & 0xC0) != 0x80) return UTF8_REPLACEMENT;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    return cp;
}

inline usize utf8_count(std::string_view s)
{
    usize n = 0, i = 0;
    while (i < s.size())
    {
        (void)utf8_decode(s, i);
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------- color
struct vec2
{
    f32 x = 0.0f, y = 0.0f;
};

// Non-owning callable reference (no allocation, no std::function).
template <class Signature> class function_ref;
template <class R, class... Args> class function_ref<R(Args...)>
{
    void *obj_ = nullptr;
    R (*fn_)(void *, Args...) = nullptr;

public:
    function_ref() = default;
    template <class F>
    function_ref(F &&f)
        : obj_(const_cast<void *>(static_cast<const void *>(&f))),
          fn_([](void *o, Args... a) -> R
              { return (*static_cast<std::remove_reference_t<F> *>(o))(std::forward<Args>(a)...); })
    {
    }
    R operator()(Args... a) const { return fn_(obj_, std::forward<Args>(a)...); }
    explicit operator bool() const { return fn_ != nullptr; }
};

struct measure_size
{
    f32 width = 0.0f, height = 0.0f;
};

struct color
{
    u8 r = 255, g = 255, b = 255, a = 255;
    static color white() { return {255, 255, 255, 255}; }
    static color black() { return {0, 0, 0, 255}; }
    static color transparent() { return {0, 0, 0, 0}; }
};

// ---------------------------------------------------------------- rect algebra
struct rect
{
    f32 x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    f32 left() const { return x; }
    f32 right() const { return x + w; }
    f32 top() const { return y; }
    f32 bottom() const { return y + h; }
    f32 center_x() const { return x + w * 0.5f; }
    f32 center_y() const { return y + h * 0.5f; }

    bool is_finite() const
    {
        return pui::is_finite(x) && pui::is_finite(y) && pui::is_finite(w) && pui::is_finite(h);
    }
    bool is_valid() const { return is_finite() && w >= 0.0f && h >= 0.0f; }

    static rect make(f32 px, f32 py, f32 pw, f32 ph) { return {px, py, pw, ph}; }

    static rect intersect(const rect &a, const rect &b)
    {
        f32 x0 = max2(a.x, b.x), y0 = max2(a.y, b.y);
        f32 x1 = min2(a.right(), b.right()), y1 = min2(a.bottom(), b.bottom());
        return {x0, y0, max2(0.0f, x1 - x0), max2(0.0f, y1 - y0)};
    }

    // The four mutating cuts. Ref-qualified `&` with the rvalue overload
    // deleted: `r.content().cut_top(h)` (cutting a temporary) is the classic
    // silent-overlap bug, and the compiler now rejects it outright. Chaining
    // on `rect::make(...)` stays legal via a named local.
    [[nodiscard]] rect cut_top(f32 amount) &
    {
        f32 a = clampf(amount, 0.0f, max2(0.0f, h));
        rect out{x, y, w, a};
        y += a;
        h -= a;
        PUFFERUI_CHECK(VIOL_INVALID_SLICE, out.is_valid(), "cut_top produced an invalid slice");
        return out;
    }
    rect cut_top(f32 amount) && = delete;
    [[nodiscard]] rect cut_bottom(f32 amount) &
    {
        f32 a = clampf(amount, 0.0f, max2(0.0f, h));
        rect out{x, y + h - a, w, a};
        h -= a;
        PUFFERUI_CHECK(VIOL_INVALID_SLICE, out.is_valid(), "cut_bottom produced an invalid slice");
        return out;
    }
    rect cut_bottom(f32 amount) && = delete;
    [[nodiscard]] rect cut_left(f32 amount) &
    {
        f32 a = clampf(amount, 0.0f, max2(0.0f, w));
        rect out{x, y, a, h};
        x += a;
        w -= a;
        PUFFERUI_CHECK(VIOL_INVALID_SLICE, out.is_valid(), "cut_left produced an invalid slice");
        return out;
    }
    rect cut_left(f32 amount) && = delete;
    [[nodiscard]] rect cut_right(f32 amount) &
    {
        f32 a = clampf(amount, 0.0f, max2(0.0f, w));
        rect out{x + w - a, y, a, h};
        w -= a;
        PUFFERUI_CHECK(VIOL_INVALID_SLICE, out.is_valid(), "cut_right produced an invalid slice");
        return out;
    }
    rect cut_right(f32 amount) && = delete;

    // Ratio cuts and non-mutating slices (pure rect algebra helpers).
    [[nodiscard]] rect cut_top_ratio(f32 ratio) & { return cut_top(h * clampf(ratio, 0.0f, 1.0f)); }
    rect cut_top_ratio(f32 ratio) && = delete;
    [[nodiscard]] rect cut_bottom_ratio(f32 ratio) &
    {
        return cut_bottom(h * clampf(ratio, 0.0f, 1.0f));
    }
    rect cut_bottom_ratio(f32 ratio) && = delete;
    [[nodiscard]] rect cut_left_ratio(f32 ratio) &
    {
        return cut_left(w * clampf(ratio, 0.0f, 1.0f));
    }
    rect cut_left_ratio(f32 ratio) && = delete;
    [[nodiscard]] rect cut_right_ratio(f32 ratio) &
    {
        return cut_right(w * clampf(ratio, 0.0f, 1.0f));
    }
    rect cut_right_ratio(f32 ratio) && = delete;

    rect top_slice(f32 amount) const
    {
        const f32 a = clampf(amount, 0.0f, h);
        return rect::make(x, y, w, a);
    }
    rect bottom_slice(f32 amount) const
    {
        const f32 a = clampf(amount, 0.0f, h);
        return rect::make(x, bottom() - a, w, a);
    }
    rect left_slice(f32 amount) const
    {
        const f32 a = clampf(amount, 0.0f, w);
        return rect::make(x, y, a, h);
    }
    rect right_slice(f32 amount) const
    {
        const f32 a = clampf(amount, 0.0f, w);
        return rect::make(right() - a, y, a, h);
    }

    // Align this rect inside `in_that` (this rect's size is kept).
    rect align_center(rect in_that) const
    {
        return rect::make(in_that.center_x() - w * 0.5f, in_that.center_y() - h * 0.5f, w, h);
    }
    rect align_h_center(rect in_that) const
    {
        return rect::make(in_that.center_x() - w * 0.5f, y, w, h);
    }
    rect align_v_center(rect in_that) const
    {
        return rect::make(x, in_that.center_y() - h * 0.5f, w, h);
    }
    rect align_left(rect in_that) const { return rect::make(in_that.x, y, w, h); }
    rect align_right(rect in_that) const { return rect::make(in_that.right() - w, y, w, h); }
    rect align_top(rect in_that) const { return rect::make(x, in_that.y, w, h); }
    rect align_bottom(rect in_that) const { return rect::make(x, in_that.bottom() - h, w, h); }

    // Shrink to the given width/height aspect ratio, centered in this rect.
    rect fit_aspect(f32 aspect) const
    {
        if (aspect <= 0.0f || w <= 0.0f || h <= 0.0f) return *this;
        f32 nw = h * aspect, nh = h;
        if (nw > w)
        {
            nw = w;
            nh = w / aspect;
        }
        return rect::make(center_x() - nw * 0.5f, center_y() - nh * 0.5f, nw, nh);
    }

    rect pad(f32 amount) const
    {
        return {x + amount, y + amount, max2(0.0f, w - 2.0f * amount),
                max2(0.0f, h - 2.0f * amount)};
    }
    rect pad(f32 pad_x, f32 pad_y) const
    {
        return {x + pad_x, y + pad_y, max2(0.0f, w - 2.0f * pad_x), max2(0.0f, h - 2.0f * pad_y)};
    }
    rect pad(f32 l, f32 t, f32 r, f32 b) const
    {
        return {x + l, y + t, max2(0.0f, w - l - r), max2(0.0f, h - t - b)};
    }

    bool contains(f32 px, f32 py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    bool contains(const rect &other) const
    {
        return other.x >= x && other.y >= y && other.right() <= right() &&
               other.bottom() <= bottom();
    }
};

// ---------------------------------------------------------------- hashing / ids
inline constexpr u64 FNV_OFFSET = 14695981039346656037ull;
inline constexpr u64 FNV_PRIME = 1099511628211ull;

constexpr u64 hash(std::string_view s)
{
    u64 value = FNV_OFFSET;
    for (unsigned char c : s)
    {
        value ^= c;
        value *= FNV_PRIME;
    }
    return value;
}

constexpr uiid operator""_id(const char *s, usize n)
{
    return hash(std::string_view(s, n));
}

inline uiid id_child(uiid parent, uiid salt)
{
    u64 h = FNV_OFFSET;
    for (i32 i = 0; i < 8; ++i)
    {
        h ^= static_cast<u8>((parent >> (i * 8)) & 0xFF);
        h *= FNV_PRIME;
    }
    for (i32 i = 0; i < 8; ++i)
    {
        h ^= static_cast<u8>((salt >> (i * 8)) & 0xFF);
        h *= FNV_PRIME;
    }
    return h;
}
inline uiid id_child(uiid parent, std::string_view key)
{
    return id_child(parent, hash(key));
}

// ---------------------------------------------------------------- alignment
enum class align : u8
{
    LEFT,
    CENTER,
    RIGHT
};
inline constexpr align ALIGN_LEFT = align::LEFT;
inline constexpr align ALIGN_CENTER = align::CENTER;
inline constexpr align ALIGN_RIGHT = align::RIGHT;

// ---------------------------------------------------------------- keyboard
enum class key : u8
{
    ESCAPE,
    ENTER,
    TAB,
    BACKSPACE,
    SPACE,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    HOME,
    END,
    A,
    C,
    X,
    V,
    Z,
    Y,
    DEL
};
inline constexpr i32 KEY_COUNT = 18;

enum class cursor : u8
{
    ARROW,
    IBEAM,
    HAND,
    HRESIZE,
    VRESIZE
};
inline constexpr cursor CURSOR_ARROW = cursor::ARROW;
inline constexpr cursor CURSOR_IBEAM = cursor::IBEAM;
inline constexpr cursor CURSOR_HAND = cursor::HAND;
inline constexpr cursor CURSOR_HRESIZE = cursor::HRESIZE;
inline constexpr cursor CURSOR_VRESIZE = cursor::VRESIZE;

// Clipboard abstraction; supply one (e.g. SDL-backed) via set_clipboard().
struct clipboard
{
    virtual ~clipboard() = default;
    virtual bool get(std::string &out) = 0;
    virtual void set(std::string_view text) = 0;
};

// ---------------------------------------------------------------- animation
// An animation is a value that moves toward a target across frames. Progress is
// keyed UI state: call the same function every frame and use the returned value.
enum class easing : u8
{
    LINEAR = 0,
    EASE_IN,
    EASE_OUT,
    EASE_IN_OUT,
    EASE_OUT_BACK,
};

inline f32 ease(easing e, f32 t)
{
    t = clampf(t, 0.0f, 1.0f);
    switch (e)
    {
    case easing::LINEAR:
        return t;
    case easing::EASE_IN:
        return t * t;
    case easing::EASE_OUT:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case easing::EASE_IN_OUT:
        return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    case easing::EASE_OUT_BACK:
    {
        const f32 c1 = 1.70158f, c3 = c1 + 1.0f;
        const f32 u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u;
    }
    }
    return t;
}

// tween: time-parametric, settles in a fixed duration.
struct tween
{
    f32 duration = 0.2f;
    easing curve = easing::EASE_OUT;
};

// spring: physics; settles when it stops moving. Retargets keep velocity.
struct spring
{
    f32 stiffness = 180.0f;
    f32 damping_ratio = 1.0f; // 1 = critical, < 1 bounces
    f32 mass = 1.0f;
};

// Optional implicit transition for built-in style groups (duration 0 = off).
struct transition
{
    f32 duration = 0.0f;
    easing curve = easing::EASE_OUT;
};

// ---------------------------------------------------------------- style cascade
// Partial overrides use opt<T>: unset fields fall through the cascade
// (theme default -> role -> scopes -> instance).
template <class T> struct opt
{
    T value{};
    bool set = false;
};
template <class T> constexpr opt<T> some(T v)
{
    return {v, true};
}

struct button_style
{
    color bg = {40, 46, 56, 255};
    color hover_bg = {54, 62, 74, 255};
    color active_bg = {32, 38, 48, 255};
    color border = {48, 56, 68, 255};
    color text = {235, 240, 245, 255};
    f32 radius = 4.0f;
    f32 border_thickness = 0.0f; // full outline; 0 = flat
    f32 pad_x = 10.0f;
    f32 pad_y = 6.0f;
    struct transition transition{}; // implicit hover/active color animation
};

struct button_override
{
    opt<color> bg, hover_bg, active_bg, border, text;
    opt<f32> radius, border_thickness, pad_x, pad_y;
    opt<transition> transition;
};

// ---- toggle controls (the pui::comp library) --------------------------------
// Style structs + per-instance overrides for the built-in toggles. The theme
// carries one slot each (`theme::switch_ctrl` / `radio` / `segmented`);
// components resolve theme -> override.
struct switch_style
{
    color track_off = {40, 46, 56, 255};
    color track_on = {86, 156, 255, 255};
    color knob = {235, 240, 245, 255};
    f32 width = 38.0f;
    f32 height = 22.0f;
    f32 knob_pad = 3.0f;
    transition anim{0.12f, easing::EASE_OUT};
    bool animate = true;
};
struct switch_override
{
    opt<color> track_off, track_on, knob;
    opt<f32> width, height, knob_pad;
};

struct radio_style
{
    color ring = {70, 80, 94, 255};
    color fill = {86, 156, 255, 255};
    f32 size = 16.0f;
    f32 gap = 10.0f;   // dot-to-label gap
    f32 ring_w = 1.5f; // outline thickness
};
struct radio_override
{
    opt<color> ring, fill;
    opt<f32> size, gap, ring_w;
};

struct segmented_style
{
    color bg = {26, 30, 38, 255};
    color selected = {86, 156, 255, 255};
    color text = {236, 240, 246, 255};
    color text_selected = {14, 18, 24, 255};
    f32 radius = 6.0f;
    f32 pad = 2.0f;
    transition anim{0.10f, easing::EASE_OUT};
};
struct segmented_override
{
    opt<color> bg, selected, text, text_selected;
    opt<f32> radius, pad;
};

struct tabs_style
{
    color text = {140, 150, 165, 255};
    color text_active = {236, 240, 246, 255};
    color underline = {86, 156, 255, 255};
    color hover_bg = {44, 50, 62, 200};
    f32 radius = 6.0f;
    f32 underline_h = 2.0f;
    f32 gap = 4.0f;
    transition anim{0.10f, easing::EASE_OUT};
};
struct tabs_override
{
    opt<color> text, text_active, underline, hover_bg;
    opt<f32> radius, underline_h, gap;
};

struct accordion_style
{
    color header_bg = {30, 34, 42, 255};
    color header_hover = {42, 48, 58, 255};
    color text = {236, 240, 246, 255};
    color chevron = {140, 150, 165, 255};
    f32 radius = 8.0f;
    f32 header_h = 30.0f;
    transition anim{0.14f, easing::EASE_OUT};
};
struct accordion_override
{
    opt<color> header_bg, header_hover, text, chevron;
    opt<f32> radius, header_h;
};

struct drawer_style
{
    color bg = {22, 25, 32, 252};
    color border = {44, 50, 61, 255};
    color scrim = {0, 0, 0, 110};
    f32 radius = 0.0f;
    transition anim{0.16f, easing::EASE_OUT};
};
struct drawer_override
{
    opt<color> bg, border, scrim;
    opt<f32> radius;
};

struct toast_style
{
    color bg = {30, 34, 42, 246};
    color info = {86, 156, 255, 255};
    color success = {92, 190, 120, 255};
    color danger = {232, 90, 90, 255};
    color text = {236, 240, 246, 255};
    f32 width = 260.0f;
    f32 height = 44.0f;
    f32 gap = 8.0f;
    f32 radius = 8.0f;
    f32 lifetime = 3.5f;
    f32 fade = 0.25f;
    transition anim{0.18f, easing::EASE_OUT};
};
struct toast_override
{
    opt<color> bg, info, success, danger, text;
    opt<f32> width, height, gap, radius, lifetime, fade;
};

struct table_style
{
    color header_bg = {26, 30, 38, 255};
    color row_bg = {22, 25, 32, 255};
    color row_alt = {26, 30, 38, 255};
    color row_hover = {40, 46, 56, 255};
    color header_text = {140, 150, 165, 255};
    color text = {236, 240, 246, 255};
    color border = {44, 50, 61, 255};
    f32 row_h = 26.0f;
    f32 header_h = 28.0f;
    f32 radius = 0.0f;
};
struct table_override
{
    opt<color> header_bg, row_bg, row_alt, row_hover, header_text, text, border;
    opt<f32> row_h, header_h, radius;
};

struct palette_style
{
    color scrim = {0, 0, 0, 120};
    color bg = {26, 30, 38, 252};
    color border = {48, 56, 68, 255};
    color text = {236, 240, 246, 255};
    color hint = {140, 150, 165, 255};
    color selected = {44, 52, 64, 255};
    color accent = {86, 156, 255, 255};
    f32 width = 420.0f;
    f32 item_h = 30.0f;
    f32 radius = 10.0f;
};
struct palette_override
{
    opt<color> scrim, bg, border, text, hint, selected, accent;
    opt<f32> width, item_h, radius;
};

// The options form for buttons: named, order-independent, extensible.
struct button_opts
{
    uiid role = 0;           // theme role id ("primary"_id, "danger"_id, ...)
    button_override style{}; // per-instance style override
};

inline button_style merge_style(button_style s, const button_override &o)
{
    if (o.bg.set) s.bg = o.bg.value;
    if (o.hover_bg.set) s.hover_bg = o.hover_bg.value;
    if (o.active_bg.set) s.active_bg = o.active_bg.value;
    if (o.border.set) s.border = o.border.value;
    if (o.text.set) s.text = o.text.value;
    if (o.radius.set) s.radius = o.radius.value;
    if (o.border_thickness.set) s.border_thickness = o.border_thickness.value;
    if (o.pad_x.set) s.pad_x = o.pad_x.value;
    if (o.pad_y.set) s.pad_y = o.pad_y.value;
    if (o.transition.set) s.transition = o.transition.value;
    return s;
}

// ---------------------------------------------------------------- skins
// A 9-slice (nine-patch) image: an atlas region plus border insets and a center
// mode. Corners and borders stay crisp at any destination size; edges and center
// follow the mode. `uv` is normalized to the atlas; insets are source pixels.
inline constexpr i32 SKIN_CENTER_STRETCH = 0;
inline constexpr i32 SKIN_CENTER_TILE = 1;
inline constexpr i32 SKIN_CENTER_NONE = 2;

struct skin_image
{
    texture_handle tex = nullptr;   // atlas texture (nullptr = unset)
    rect uv{0, 0, 0, 0};            // normalized atlas region (u0, v0, u1-u0, v1-v0)
    f32 src_w = 0.0f, src_h = 0.0f; // source region size in pixels
    f32 inset_l = 0.0f, inset_t = 0.0f, inset_r = 0.0f, inset_b = 0.0f;
    i32 center_mode = SKIN_CENTER_STRETCH;

    bool valid() const { return tex != nullptr && uv.w > 0.0f && uv.h > 0.0f; }
};

// Build an image from a pixel rect in a `atlas_w` x `atlas_h` atlas.
inline skin_image make_skin_image(texture_handle tex, i32 atlas_w, i32 atlas_h, rect px,
                                  f32 inset_l = 0.0f, f32 inset_t = 0.0f, f32 inset_r = 0.0f,
                                  f32 inset_b = 0.0f, i32 center_mode = SKIN_CENTER_STRETCH)
{
    skin_image img;
    img.tex = tex;
    img.src_w = px.w;
    img.src_h = px.h;
    if (atlas_w > 0 && atlas_h > 0)
    {
        const f32 aw = static_cast<f32>(atlas_w), ah = static_cast<f32>(atlas_h);
        img.uv = rect::make(px.x / aw, px.y / ah, px.w / aw, px.h / ah);
    }
    img.inset_l = inset_l;
    img.inset_t = inset_t;
    img.inset_r = inset_r;
    img.inset_b = inset_b;
    img.center_mode = center_mode;
    return img;
}

// ---------------------------------------------------------------- panel & card styles
enum panel_flags : u32
{
    PANEL_NONE = 0,
    PANEL_NO_TITLEBAR = 1 << 0,
    PANEL_NO_CONTROLS = 1 << 1,
    PANEL_NO_DRAG = 1 << 2,
    PANEL_NO_SHADOW = 1 << 3,
};

struct panel_style
{
    color bg = {28, 32, 40, 245};
    color titlebar_bg = {20, 24, 30, 255};
    color titlebar_text = {230, 236, 242, 255};
    color border = {48, 56, 68, 255};
    color close = {220, 70, 70, 255};
    f32 radius = 6.0f;
    f32 border_thickness = 1.0f;
    f32 titlebar_h = 30.0f;
    f32 padding = 10.0f;
    bool shadow = true;
};
struct panel_override
{
    opt<color> bg, titlebar_bg, titlebar_text, border, close;
    opt<f32> radius, border_thickness, titlebar_h, padding;
    opt<bool> shadow;
};

// The options form for panels: named, order-independent, extensible.
struct panel_opts
{
    u32 flags = PANEL_NONE;
    uiid dock_panel = 0;             // dock into a leaf/tab node of the app-owned tree
    const char *dock_name = nullptr; // the tab title when docked
    panel_override style{};          // per-instance style override
};
inline panel_style merge_style(panel_style s, const panel_override &o)
{
    if (o.bg.set) s.bg = o.bg.value;
    if (o.titlebar_bg.set) s.titlebar_bg = o.titlebar_bg.value;
    if (o.titlebar_text.set) s.titlebar_text = o.titlebar_text.value;
    if (o.border.set) s.border = o.border.value;
    if (o.close.set) s.close = o.close.value;
    if (o.radius.set) s.radius = o.radius.value;
    if (o.border_thickness.set) s.border_thickness = o.border_thickness.value;
    if (o.titlebar_h.set) s.titlebar_h = o.titlebar_h.value;
    if (o.padding.set) s.padding = o.padding.value;
    if (o.shadow.set) s.shadow = o.shadow.value;
    return s;
}

struct card_style
{
    color bg = {20, 24, 30, 200};
    color border = {48, 56, 68, 200};
    f32 radius = 4.0f;
    f32 border_thickness = 1.0f;
    f32 padding = 10.0f;
};
struct card_override
{
    opt<color> bg, border;
    opt<f32> radius, border_thickness, padding;
};
inline card_style merge_style(card_style s, const card_override &o)
{
    if (o.bg.set) s.bg = o.bg.value;
    if (o.border.set) s.border = o.border.value;
    if (o.radius.set) s.radius = o.radius.value;
    if (o.border_thickness.set) s.border_thickness = o.border_thickness.value;
    if (o.padding.set) s.padding = o.padding.value;
    return s;
}

// ---------------------------------------------------------------- popups
enum class popup_flags : u32
{
    NONE = 0,
    CLOSE_ON_ESCAPE = 1 << 0,
    CLOSE_ON_CLICK_OUTSIDE = 1 << 1,
    MODAL = 1 << 2,
};
inline constexpr popup_flags operator|(popup_flags a, popup_flags b)
{
    return static_cast<popup_flags>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline constexpr bool has_flag(popup_flags flags, popup_flags bit)
{
    return (static_cast<u32>(flags) & static_cast<u32>(bit)) != 0;
}
inline constexpr popup_flags POPUP_CLOSE_ON_ESCAPE = popup_flags::CLOSE_ON_ESCAPE;
inline constexpr popup_flags POPUP_CLOSE_ON_CLICK_OUTSIDE = popup_flags::CLOSE_ON_CLICK_OUTSIDE;
inline constexpr popup_flags POPUP_MODAL = popup_flags::MODAL;

// ---------------------------------------------------------------- theming
inline constexpr i32 MAX_BUTTON_ROLES = 16;
struct button_role
{
    uiid id = 0;
    button_override ov{};
};

struct scroll_style
{
    f32 thickness = 8.0f;
    f32 margin = 2.0f;
    f32 min_thumb = 24.0f;
    f32 radius = 4.0f;
    color track = {28, 32, 40, 120};
    color thumb = {70, 80, 95, 220};
    color thumb_hover = {95, 108, 126, 235};
    color thumb_active = {60, 180, 255, 235};
};

struct theme
{
    color bg = {18, 20, 26, 255};
    color panel_bg = {28, 32, 40, 255};
    color border = {48, 56, 68, 255};
    color focus_border = {60, 180, 255, 255}; // focused field outline
    color text = {235, 240, 245, 255};
    color text_dim = {140, 150, 160, 255};
    color accent = {60, 180, 255, 255};
    color accent_hover = {90, 200, 255, 255};
    color widget_bg = {40, 46, 56, 255};
    color widget_hover = {54, 62, 74, 255};
    color widget_active = {32, 38, 48, 255};
    f32 radius = 4.0f;
    f32 border_thickness = 0.0f;       // widget outline; 0 = flat
    f32 focus_border_thickness = 1.0f; // focused field outline
    f32 spacing = 6.0f;
    f32 padding = 12.0f;
    // Control metrics (the layout rhythm): interactive rows use `control_h`,
    // dense rows `control_h_small`, glyphs `icon_size`. The size helpers
    // (`u.button_size` / `u.text_size`) read them, so examples and components
    // never hard-code 26–30px magic numbers.
    f32 control_h = 28.0f;
    f32 control_h_small = 22.0f;
    f32 icon_size = 16.0f;
    font_handle font = FONT_INVALID;
    f32 text_size = 15.0f;

    color selection = {60, 120, 200, 140};
    // Semantic tokens (r93): the palette components read for surfaces, text
    // and actions — separated from per-component styles, so retheming means
    // setting tokens once. Component styles default FROM these.
    struct tokens_t
    {
        color surface = {22, 25, 32, 255};           // panels, popups
        color surface_alt = {30, 34, 42, 255};       // controls, headers
        color on_surface = {236, 240, 246, 255};     // primary text
        color on_surface_dim = {140, 150, 165, 255}; // secondary text
        color primary = {86, 156, 255, 255};         // actions, selection
        color primary_hover = {122, 180, 255, 255};
        color danger = {232, 90, 90, 255};
        color success = {92, 190, 120, 255};
        color warning = {230, 170, 70, 255};
        color outline = {44, 50, 61, 255};
    } tokens;
    color caret = {235, 240, 245, 255};
    char decimal_separator = '.';
    cursor button_cursor = CURSOR_HAND;
    cursor text_cursor = CURSOR_IBEAM;

    button_style button;
    switch_style switch_ctrl;  // pui::comp::switch_toggle
    radio_style radio;         // pui::comp::radio_group
    segmented_style segmented; // pui::comp::segmented
    tabs_style tabs;           // pui::comp::tab_bar
    accordion_style accordion; // pui::comp::accordion_scope
    drawer_style drawer;       // pui::comp::drawer_scope
    toast_style toast;         // pui::comp::toast_draw
    table_style table;         // pui::comp::table
    palette_style palette;     // pui::comp::command_palette
    button_role button_roles[MAX_BUTTON_ROLES]{};
    i32 button_role_count = 0;

    panel_style panel;
    card_style card;
    scroll_style scrollbar;
};

// A friendlier default palette: a layered dark ramp (background < surface <
// elevated), a soft outline, generous radii and an 8px rhythm.
inline theme default_dark()
{
    theme t;
    t.bg = {13, 15, 19, 255};
    t.panel_bg = {22, 25, 32, 252};
    t.border = {42, 48, 58, 255};
    t.focus_border = {104, 168, 255, 255};
    t.text = {236, 240, 246, 255};
    t.text_dim = {140, 150, 165, 255};
    t.accent = {86, 156, 255, 255};
    t.accent_hover = {122, 180, 255, 255};
    t.widget_bg = {30, 34, 42, 255};
    t.widget_hover = {40, 46, 56, 255};
    t.widget_active = {24, 28, 35, 255};
    t.radius = 7.0f;
    t.border_thickness = 0.0f;
    t.focus_border_thickness = 1.5f;
    t.spacing = 8.0f;
    t.padding = 14.0f;
    t.selection = {86, 156, 255, 110};
    // tokens (the semantic layer the values above also feed)
    t.tokens.surface = t.panel_bg;
    t.tokens.surface_alt = t.widget_bg;
    t.tokens.on_surface = t.text;
    t.tokens.on_surface_dim = t.text_dim;
    t.tokens.primary = t.accent;
    t.tokens.primary_hover = t.accent_hover;
    t.tokens.outline = t.border;
    t.button.bg = {32, 37, 46, 255};
    t.button.hover_bg = {43, 49, 60, 255};
    t.button.active_bg = {26, 30, 38, 255};
    t.button.border = {52, 59, 71, 255};
    t.button.border_thickness = 1.0f;
    t.button.radius = 7.0f;
    t.panel.radius = 12.0f;
    t.panel.bg = {22, 25, 32, 250};
    t.panel.titlebar_bg = {28, 32, 40, 255};
    t.panel.border = {44, 50, 61, 255};
    t.panel.padding = 12.0f;
    t.card.radius = 10.0f;
    t.card.padding = 12.0f;
    t.scrollbar.thickness = 7.0f;
    t.scrollbar.thumb = {70, 78, 92, 220};
    t.scrollbar.thumb_hover = {96, 106, 122, 235};
    // toggle controls: the accent ramp
    t.switch_ctrl.track_off = {40, 46, 56, 255};
    t.switch_ctrl.track_on = t.accent;
    t.switch_ctrl.knob = {236, 240, 246, 255};
    t.radio.ring = {70, 80, 94, 255};
    t.radio.fill = t.accent;
    t.segmented.bg = {24, 28, 35, 255};
    t.segmented.selected = t.accent;
    t.segmented.text = {236, 240, 246, 255};
    t.segmented.text_selected = {12, 16, 22, 255};
    t.tabs.text = {140, 150, 165, 255};
    t.tabs.text_active = {236, 240, 246, 255};
    t.tabs.underline = t.accent;
    t.accordion.header_bg = {30, 34, 42, 255};
    t.accordion.header_hover = {42, 48, 58, 255};
    t.accordion.text = {236, 240, 246, 255};
    t.drawer.bg = {22, 25, 32, 252};
    t.drawer.border = {44, 50, 61, 255};
    t.toast.info = t.accent;
    t.toast.success = t.tokens.success;
    t.toast.danger = t.tokens.danger;
    t.toast.text = t.text;
    t.table.border = t.border;
    t.table.text = t.text;
    t.palette.accent = t.accent;
    t.palette.text = t.text;
    t.palette.hint = t.text_dim;
    return t;
}

// Interpolates two themes (colors and the layout metrics): the app can ease
// between a dark and a light theme over a few frames. Respects reduced
// motion by simply being driven by the app's own t.
inline color theme_lerp_color(color a, color b, f32 t)
{
    const auto mix = [&](u8 x, u8 y)
    {
        return static_cast<u8>(clampf(
            static_cast<f32>(x) + (static_cast<f32>(y) - static_cast<f32>(x)) * t, 0.0f, 255.0f));
    };
    return color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

inline theme theme_lerp(const theme &a, const theme &b, f32 t)
{
    theme r = a;
    r.bg = theme_lerp_color(a.bg, b.bg, t);
    r.panel_bg = theme_lerp_color(a.panel_bg, b.panel_bg, t);
    r.border = theme_lerp_color(a.border, b.border, t);
    r.focus_border = theme_lerp_color(a.focus_border, b.focus_border, t);
    r.text = theme_lerp_color(a.text, b.text, t);
    r.text_dim = theme_lerp_color(a.text_dim, b.text_dim, t);
    r.accent = theme_lerp_color(a.accent, b.accent, t);
    r.accent_hover = theme_lerp_color(a.accent_hover, b.accent_hover, t);
    r.widget_bg = theme_lerp_color(a.widget_bg, b.widget_bg, t);
    r.widget_hover = theme_lerp_color(a.widget_hover, b.widget_hover, t);
    r.widget_active = theme_lerp_color(a.widget_active, b.widget_active, t);
    r.selection = theme_lerp_color(a.selection, b.selection, t);
    r.tokens.surface = theme_lerp_color(a.tokens.surface, b.tokens.surface, t);
    r.tokens.surface_alt = theme_lerp_color(a.tokens.surface_alt, b.tokens.surface_alt, t);
    r.tokens.on_surface = theme_lerp_color(a.tokens.on_surface, b.tokens.on_surface, t);
    r.tokens.on_surface_dim = theme_lerp_color(a.tokens.on_surface_dim, b.tokens.on_surface_dim, t);
    r.tokens.primary = theme_lerp_color(a.tokens.primary, b.tokens.primary, t);
    r.tokens.primary_hover = theme_lerp_color(a.tokens.primary_hover, b.tokens.primary_hover, t);
    r.tokens.danger = theme_lerp_color(a.tokens.danger, b.tokens.danger, t);
    r.tokens.success = theme_lerp_color(a.tokens.success, b.tokens.success, t);
    r.tokens.warning = theme_lerp_color(a.tokens.warning, b.tokens.warning, t);
    r.tokens.outline = theme_lerp_color(a.tokens.outline, b.tokens.outline, t);
    r.radius = a.radius + (b.radius - a.radius) * t;
    r.spacing = a.spacing + (b.spacing - a.spacing) * t;
    r.padding = a.padding + (b.padding - a.padding) * t;
    r.control_h = a.control_h + (b.control_h - a.control_h) * t;
    return r;
}

inline void set_button_role(theme &t, uiid id, button_override ov)
{
    for (i32 i = 0; i < t.button_role_count; ++i)
    {
        if (t.button_roles[i].id == id)
        {
            t.button_roles[i].ov = ov;
            return;
        }
    }
    if (t.button_role_count < MAX_BUTTON_ROLES)
        t.button_roles[t.button_role_count++] = button_role{id, ov};
}

// ---------------------------------------------------------------- rendering
struct vertex
{
    f32 x = 0, y = 0, u = 0, v = 0;
    color c{};
};

enum class backend_caps : u32
{
    NONE = 0,
    RENDER_TARGETS = 1 << 0, // offscreen targets + filtered blit (blur)
    SCISSOR = 1 << 1,
    STREAMING_TEXTURES = 1 << 2,
    SHARED_DEVICE = 1 << 3, // one device may serve multiple windows
};
inline constexpr backend_caps operator|(backend_caps a, backend_caps b)
{
    return static_cast<backend_caps>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline constexpr bool has_cap(backend_caps caps, backend_caps bit)
{
    return (static_cast<u32>(caps) & static_cast<u32>(bit)) != 0;
}

struct render_surface;

// One device shared by all windows. Owns textures/targets; the core keeps the
// draw list and only submits geometry here.
struct render_device
{
    virtual ~render_device() = default;
    virtual backend_caps caps() const { return backend_caps::SCISSOR; }

    virtual void begin_frame() {}
    virtual void end_frame() {}

    virtual render_surface *create_surface(void *native_window = nullptr)
    {
        (void)native_window;
        return nullptr;
    }

    // Presentation vsync (best effort; backends without control ignore it).
    virtual void set_vsync(bool enabled) { (void)enabled; }

    virtual texture_handle create_texture(i32 w, i32 h, const u8 *rgba) = 0;
    virtual void update_texture(texture_handle tex, i32 x, i32 y, i32 w, i32 h, const u8 *rgba) = 0;
    virtual void destroy_texture(texture_handle tex) = 0;

    virtual texture_handle create_target(i32 w, i32 h)
    {
        (void)w;
        (void)h;
        return nullptr;
    }
    virtual void destroy_target(texture_handle tex) { destroy_texture(tex); }
    virtual void blit(texture_handle src, const rect *src_rect, texture_handle dst,
                      const rect *dst_rect)
    {
        (void)src;
        (void)src_rect;
        (void)dst;
        (void)dst_rect;
    }

    virtual void set_target(texture_handle target) { (void)target; }
    virtual texture_handle current_target() const { return nullptr; }
    virtual void set_clip(const rect *clip) { (void)clip; }
    virtual void set_cursor(cursor c) { (void)c; }
    virtual void clear(color c) { (void)c; }

    virtual void draw(texture_handle tex, const vertex *vertices, i32 vertex_count,
                      const i32 *indices, i32 index_count) = 0;
};

// Per-window backbuffer on the shared device.
struct render_surface
{
    virtual ~render_surface() = default;
    virtual void make_current(render_device &device) { (void)device; }
    virtual texture_handle default_target() { return nullptr; }
    virtual texture_handle scene_target() { return nullptr; }
    virtual void output_size(i32 &w, i32 &h)
    {
        w = 0;
        h = 0;
    }
    virtual void present() {}
    virtual void resize() {}
};

struct null_surface : render_surface
{
    texture_handle scene = reinterpret_cast<texture_handle>(static_cast<usize>(3));
    i32 w = 0, h = 0;
    texture_handle scene_target() override { return scene; }
    void output_size(i32 &ow, i32 &oh) override
    {
        ow = w;
        oh = h;
    }
};

// Headless device used by tests and to prove contract completeness. Reports
// SHARED_DEVICE and hands out one surface per `create_surface` call; set
// `shared = false` to model a single-window backend in gating tests.
struct null_device : render_device
{
    null_surface my_surface;
    static constexpr i32 MAX_EXTRA_SURFACES = 7;
    null_surface extra[MAX_EXTRA_SURFACES];
    i32 surfaces_created = 0;
    bool shared = true;

    i32 draw_calls = 0;
    i32 vertices = 0;
    i32 textures = 0;
    i32 targets = 0;
    texture_handle last_texture = nullptr;
    i32 cursor_sets = 0;
    cursor last_cursor = cursor::ARROW;
    bool vsync = true;

    backend_caps caps() const override
    {
        backend_caps b =
            backend_caps::RENDER_TARGETS | backend_caps::SCISSOR | backend_caps::STREAMING_TEXTURES;
        if (shared) b = b | backend_caps::SHARED_DEVICE;
        return b;
    }
    void begin_frame() override
    {
        draw_calls = 0;
        vertices = 0;
    }
    render_surface *create_surface(void * = nullptr) override
    {
        if (surfaces_created == 0)
        {
            ++surfaces_created;
            return &my_surface;
        }
        if (!shared || surfaces_created > MAX_EXTRA_SURFACES) return nullptr;
        null_surface &s = extra[surfaces_created - 1];
        s.scene = reinterpret_cast<texture_handle>(static_cast<usize>(3 + surfaces_created));
        ++surfaces_created;
        return &s;
    }
    texture_handle create_texture(i32, i32, const u8 *) override
    {
        ++textures;
        return reinterpret_cast<texture_handle>(static_cast<usize>(0x200 + textures));
    }
    texture_handle create_target(i32, i32) override
    {
        ++targets;
        return reinterpret_cast<texture_handle>(static_cast<usize>(0x100 + targets));
    }
    void update_texture(texture_handle, i32, i32, i32, i32, const u8 *) override {}
    void destroy_texture(texture_handle) override {}
    void set_cursor(cursor c) override
    {
        ++cursor_sets;
        last_cursor = c;
    }
    void set_vsync(bool enabled) override { vsync = enabled; }
    void draw(texture_handle tex, const vertex *, i32 vertex_count, const i32 *, i32) override
    {
        ++draw_calls;
        vertices += vertex_count;
        last_texture = tex;
    }
};

// SDL3 device + surface. Returns nullptr when PufferUI was built without
// PUFFERUI_ENABLE_SDL3. Owned by the caller.
render_device *create_sdl3_device(void *sdl_renderer);
void destroy_sdl3_device(render_device *device);

// One-call SDL event routing (the SDL3 glue above): feeds every event to the
// right window (mouse, left+right buttons, wheel, keys with mods, text input,
// IME preedit, focus, move/resize) and keeps window clients in sync with SDL.
// A window-close request sets `window::close_requested` for the app to act on.
// Also installs the SDL3 window_host once, so custom titlebars work.
//   sdl3_route - route one already-polled SDL_Event (SDL_AppEvent style).
//   sdl3_pump  - poll SDL and route everything; returns true on SDL_EVENT_QUIT
//                (the app should exit). The event is passed as void* because
//                the declaration section does not include SDL.
bool sdl3_route(context *c, void *sdl_event);
bool sdl3_pump(context *c);

// ---- one-call bootstrap (SDL3 builds) ----
// Owns the SDL-side objects a single-window app needs and hands them over as
// plain pointers. The declaration stays SDL-free: the SDL handles are void*
// here (the SDL3 glue casts them back).
//
//   sdl3_app_init    - SDL_Init + window (borderless + resizable) + renderer +
//                      device + surface + context + window with the native
//                      chrome installed. Returns false (and cleans up) on any
//                      failure.
//   sdl3_app_pump    - poll SDL, route every event, keep the window client in
//                      sync. Returns false when the app should stop
//                      (SDL_EVENT_QUIT or the window's close_requested).
//   sdl3_app_tick    - advance the app clock: fills `now` / `dt` for
//                      begin_frame.
//   sdl3_app_shutdown- destroy context + device + renderer + window; SDL_Quit.
//
// Fonts and the theme stay app-side (load_font + set_theme); see the README's
// smallest program for the complete app.
struct render_device;
struct render_surface;
struct context;
struct window;
struct sdl3_app
{
    void *sdl_window = nullptr;      // SDL_Window*
    void *sdl_renderer = nullptr;    // SDL_Renderer*
    render_device *device = nullptr; // owned; freed by sdl3_app_shutdown
    render_surface *surface = nullptr;
    context *ctx = nullptr; // owned
    window *win = nullptr;
    font_handle font = FONT_INVALID; // the base font the bootstrap loaded

    // options (set before sdl3_app_init)
    bool hidden = false;                 // create the window hidden (offscreen tests)
    const char *renderer_name = nullptr; // "software" pins the software renderer
    const char *asset_dir = nullptr;     // font root; defaults to PUFFERUI_ASSET_DIR
    // Opt-in idle sleep: when true and the library has nothing animating, the
    // pump waits for events instead of spinning at vsync (tools/editors on
    // laptops). Apps that animate OUTSIDE the library (their own clocks,
    // video, ...) must keep this false or drive their own wake-ups.
    bool wait_when_idle = false;

    // the app's clock
    f64 freq = 0.0;
    u64 last_counter = 0;
    f64 now = 0.0, dt = 1.0 / 60.0;
    i32 width = 0, height = 0;
    bool inited = false;
};
bool sdl3_app_init(sdl3_app &a, const char *title, i32 w, i32 h, int argc, char **argv);
bool sdl3_app_pump(sdl3_app &a);
void sdl3_app_tick(sdl3_app &a);
void sdl3_app_shutdown(sdl3_app &a);

// ---------------------------------------------------------------- context
struct draw_list;
struct text_store;
struct dock_node;

struct popup_entry
{
    uiid id = 0;
    rect area{};
    i32 win = 0;
};
struct panel_entry
{
    uiid id = 0;
    rect area{};
    i32 win = 0;
};

// ---------------------------------------------------------------- windows
// Multi-window: one shared device, one surface per window. Windows are framed
// one after another; a window's widgets only interact with its own input.
// `client` is the window's client rect in DESKTOP coordinates (all windows share
// one desktop space); `area` is the local client rect { 0, 0, w, h }.
inline constexpr i32 MAX_WINDOWS = 8;

// Input queued by the platform layer between frames. The button *held* states
// and the pointer position are global (one pointer); press/release edges and text
// land in the window they were routed to.
struct window_input
{
    bool mouse_pressed = false;
    bool mouse_released = false;
    bool right_pressed = false;
    f32 wheel_x = 0.0f;
    f32 wheel_y = 0.0f;
    bool key_down[KEY_COUNT]{};
    bool key_pressed[KEY_COUNT]{};
    bool key_released[KEY_COUNT]{};
    char text_input[512]{};
    i32 text_len = 0;
    char ime_preedit[128]{};
    i32 ime_preedit_len = 0;
    i32 ime_cursor = 0;
    bool shift_down = false;
    bool ctrl_down = false;
};

struct context;

struct window
{
    context *owner = nullptr;
    void *handle = nullptr; // native window (platform-specific)
    render_surface *surface = nullptr;
    rect client{0, 0, 0, 0}; // client rect, desktop coordinates
    rect area{0, 0, 0, 0};   // local client rect
    uiid root = 0;           // per-window root region id (unique)
    i32 index = 0;
    bool focused = false;
    bool close_requested = false; // the titlebar's close button or the platform asked
    bool maximized = false;       // reflected by the host / event pump
    void *blur = nullptr;         // impl-side per-window blur scratch
    void *focus_store = nullptr;  // impl-side per-window focus list
    window_input in{};

    // persistent per-window widget state (saved/restored around the frame)
    uiid active = 0;
    uiid focus = 0;
    uiid focus_request = 0;
    uiid dragging_panel = 0;
    f32 drag_dx = 0.0f, drag_dy = 0.0f;
    uiid last_click_id = 0;
    f64 last_click_time = -1.0;
};

// Platform actions a custom titlebar needs from the native window. Backends
// that own windows implement it (the SDL3 glue does); with no host installed
// the titlebar widget still draws and reports clicks, but moving, resizing,
// minimizing and maximizing do nothing.
struct window_host
{
    virtual ~window_host() = default;
    // Move the window's client top-left to the desktop point (screen pixels on
    // the platform's global space, the same space as `window::client`).
    virtual void move_window(window &w, f32 desktop_x, f32 desktop_y) = 0;
    virtual void resize_window(window &w, f32 width, f32 height) = 0;
    virtual void minimize_window(window &w) { (void)w; }
    // Maximize when restored, restore when maximized.
    virtual void toggle_maximize(window &w) { (void)w; }
    // Install the platform's native window chrome for a borderless window:
    // native caption dragging (the system's own Aero Snap and its preview),
    // and native edge/corner resizing. Backends that can map the library's
    // chrome geometry onto platform hit-testing override it; a no-op host
    // leaves the window with no native drag/resize.
    virtual void install_system_chrome(window &w) { (void)w; }
};

// What the titlebar widget did this frame (besides platform actions through
// the host; a close click sets window::close_requested rather than returning).
struct titlebar_result
{
    bool close_clicked = false;
    bool minimize_clicked = false;
    bool maximize_clicked = false;
};

// Platform "global mouse" queries may return a different coordinate space than
// window clients (SDL3's SDL_GetGlobalMouseState returns unscaled screen
// pixels while its event coordinates are content-scaled); the titlebar uses
// the pointer tracked through `mouse_move(window&, ...)` instead.
void set_window_host(context *c, window_host *host);
window_host *window_host_of(context *c);

// An in-app text drag (browser-like): a selection is lifted from its source field
// and dropped into another (or the same) field. Move by default; hold Ctrl while
// dropping to copy. The pointer is global, so this belongs to the context.
struct text_drag_payload
{
    bool active = false;
    uiid source_id = 0;   // text field id that owns the range
    usize lo = 0, hi = 0; // byte range in the source buffer
    std::string text;     // the lifted text
};

struct context
{
    render_device *device = nullptr;
    render_surface *surface = nullptr;
    draw_list *dl = nullptr;
    text_store *ts = nullptr;
    void *edits = nullptr;
    void *anim = nullptr;
    bool reduced_motion = false;
    void *blur = nullptr;
    void *splits = nullptr;
    void *scrolls = nullptr; // impl-side scroll offsets keyed by uiid

    // dock drag state (panel dragged by its tab)
    uiid dock_panel = 0;
    dock_node *dock_source = nullptr;
    const char *dock_panel_name = nullptr; // points into the owned buffer below
    char dock_panel_name_buf[128]{};       // owned: the drag ghost outlives the caller
    bool dock_dragging = false;
    f32 dock_press_x = 0.0f, dock_press_y = 0.0f;

    theme active_theme = default_dark();

    uiid parent_stack[MAX_ID_DEPTH]{};
    i32 parent_depth = 0;
    uiid current_region = 0;
    void *frame_id_set = nullptr;  // detail::flat_map<u8> — per-frame region ids
    void *widget_id_set = nullptr; // debug: interacted widget ids -> first rect
    u64 seq = 0;

    rect clip_stack[MAX_CLIP_DEPTH]{};
    i32 clip_depth = 0;

    uiid hot = 0;
    uiid active = 0;
    uiid focus = 0;
    uiid focus_request = 0;
    bool focus_request_selects_all = false; // Tab focus selects the field's value

    uiid dragging_panel = 0;
    f32 drag_dx = 0.0f, drag_dy = 0.0f;

    clipboard *clip = nullptr;
    bool shift_down = false;
    bool ctrl_down = false;

    uiid last_click_id = 0;
    f64 last_click_time = -1.0;

    void *focus_store = nullptr; // impl-side dynamic focusable list
    cursor want_cursor = cursor::ARROW;
    window_host *host = nullptr; // platform actions for custom titlebars

    // Layout-overflow reporting (see set_report_layout_overflow). Off by default.
    bool report_layout_overflow = false;
    i32 overflow_count = 0;

    char ime_preedit[128]{};
    i32 ime_preedit_len = 0;
    i32 ime_cursor = 0;

    f32 mouse_x = 0.0f, mouse_y = 0.0f;
    bool mouse_down = false;
    bool mouse_pressed = false;
    bool mouse_released = false;
    bool right_down = false;
    bool right_pressed = false;
    f32 wheel_x = 0.0f, wheel_y = 0.0f; // this frame's wheel delta
    uiid wheel_target = 0;              // innermost hovered scroll view this frame

    text_drag_payload text_drag; // active in-app text drag (browser-like)

    bool key_down[KEY_COUNT]{};
    bool key_pressed[KEY_COUNT]{};
    bool key_released[KEY_COUNT]{};

    char text_input[512]{};
    i32 text_len = 0;

    u64 frame = 0;
    f64 now = 0.0, dt = 0.0;
    rect screen{0, 0, 0, 0};
    rect desktop{0, 0, 0, 0}; // union of all window clients (desktop coords)

    window *windows = nullptr; // heap-owned, grown on demand (start: MAX_WINDOWS)
    i32 window_capacity = 0;
    i32 window_count = 0;
    window *current_window = nullptr; // window whose frame is open (null between)
    window *focused_win = nullptr;    // receives keyboard input
    f32 mouse_global_x = 0.0f, mouse_global_y = 0.0f;

    button_override button_scopes[MAX_STYLE_SCOPES]{};
    i32 button_scope_depth = 0;

    // Growth-on-demand stacks (same pattern as the window list): the old
    // fixed caps (MAX_POPUPS/MAX_PANELS) are the INITIAL capacity; the arrays
    // double when full, so deep nesting never silently refuses a layer.
    popup_entry *popups = nullptr;
    i32 popup_depth = 0;
    i32 popup_capacity = 0;
    popup_entry *prev_popups = nullptr;
    i32 prev_popup_depth = 0;

    // combo/context_menu state (keyed by uiid, per window via the popup table).
    i32 combo_hot = -1;   // item index the dropdown/menu highlights (kbd + mouse)
    uiid tip_last_id = 0; // tooltip queue: widget the last tooltip belonged to
    f64 tip_last_shown = -1.0;
    uiid tip_stuck_id = 0; // tooltip queue: widget currently waiting out the delay
    f64 tip_wait_start = -1.0;
    f32 context_menu_x = 0.0f, context_menu_y = 0.0f; // where the menu was opened

    // Deferred overlays (see combo / context_menu / tooltip): the open menu and
    // the visible tooltip are drawn once at the end of the frame, so widgets
    // painted after their anchor can never cover them. One menu surface and
    // one tooltip at a time. The menu's labels are COPIED into context-owned
    // storage (reached via the `defers` handle; the caller's array may be a
    // stack local that dies before end_frame); the pick reports through
    // `defer_result_id`/`defer_result_pick`, which the widget's next call
    // consumes and applies to its model.
    uiid defer_menu_id = 0;
    rect defer_menu{};
    void *defers = nullptr; // detail::defer_store (owned label copies)
    i32 defer_count = 0;
    bool defer_fresh = false;
    uiid defer_result_id = 0; // pick result awaiting the next widget call
    i32 defer_result_pick = -1;
    rect defer_tip{};
    char defer_tip_text[240]{};
    bool popup_click_outside = false;
    uiid popup_click_id = 0;
    bool in_popup = false; // currently drawing popup content
    i32 popup_layer_depth = 0;

    panel_entry *panels = nullptr;
    i32 panel_depth = 0;
    i32 panel_capacity = 0;
    panel_entry *prev_panels = nullptr;
    i32 prev_panel_depth = 0;
    uiid *panel_stack = nullptr;
    i32 panel_stack_depth = 0;
    i32 panel_stack_capacity = 0;

    i32 violations = 0;
    violation_handler vhandler = nullptr;
    void *vhandler_user = nullptr;
    char vlast[240]{}; // the last violation's message ("" when none)
    violation_code vlast_code = VIOL_NONE;
    bool break_on_violation = true; // debug builds stop the debugger (opt out per app)

    // Topmost-wins interaction: this frame's press claim and its rect (the
    // last submitted enabled rect containing the press position).
    uiid press_claim = 0;
    rect press_claim_rect{};
};

// ---------------------------------------------------------------- region (RAII)
struct ui;

struct region
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    uiid parent_ = 0;
    rect area_{};
    rect content_{};
    rect clip_{};
    bool pushed_clip_ = false;

    region(ui &u, uiid key, rect area, bool push_clip = true);
    ~region();

    region(const region &) = delete;
    region &operator=(const region &) = delete;
    region(region &&) = delete;
    region &operator=(region &&) = delete;

    uiid id() const { return id_; }
    rect area() const { return area_; }
    [[nodiscard]] rect content() const { return content_; }
    rect clip() const { return clip_; }

    // A badge-sized rect pinned to one of this region's content corners, inset by
    // `margin` — the "label pinned to a corner, clear of other corners" helper.
    // `n` in 0..3, clockwise from the top-left corner (0 TL, 1 TR, 2 BR, 3 BL).
    rect corner(i32 n, f32 w, f32 h, f32 margin = 8.0f) const;
};

// Scoped button restyle: applies to buttons drawn within its lifetime.
struct style_scope
{
    ui *u_ = nullptr;
    bool pushed_ = false;

    style_scope(ui &u, button_override ov);
    ~style_scope();

    style_scope(const style_scope &) = delete;
    style_scope &operator=(const style_scope &) = delete;
    style_scope(style_scope &&) = delete;
    style_scope &operator=(style_scope &&) = delete;
};

// Scoped identity (RAII): pushes an id scope for loops and reusable
// components — widgets inside derive their ids with `u.local("part")`, so two
// instances of the same component never collide. Pure identity: no area, no
// clip. Duplicate scope keys are reported like duplicate regions.
struct id_scope
{
    ui *u_ = nullptr;
    uiid parent_ = 0;

    id_scope(ui &u, uiid key);
    ~id_scope();

    id_scope(const id_scope &) = delete;
    id_scope &operator=(const id_scope &) = delete;
    id_scope(id_scope &&) = delete;
    id_scope &operator=(id_scope &&) = delete;
};

// Scroll view flags
inline constexpr u32 SCROLL_NONE = 0;
inline constexpr u32 SCROLL_ALWAYS_RESERVE_BAR = 1 << 0; // stable gutter, no layout shift
inline constexpr u32 SCROLL_OVERLAY = 1 << 1;            // floating bar, no gutter
inline constexpr u32 SCROLL_NO_CLIP = 1 << 2;            // skip the viewport scissor

struct scroll_options
{
    f32 padding = 4.0f;
    u32 flags = SCROLL_NONE;
};

struct popup_scope;
struct panel_scope;
struct text_scope;
struct scroll_view;

// ---------------------------------------------------------------- docking
// App-owned dock tree. The framework reads it, resizes splits, switches tabs,
// and reports structural moves through `dock_action`; it never moves panels
// between nodes itself.
inline constexpr i32 DOCK_SPLIT_H = 0;
inline constexpr i32 DOCK_SPLIT_V = 1;
inline constexpr i32 DOCK_LEAF = 2;
inline constexpr i32 DOCK_TABS = 3;

inline constexpr i32 DOCK_ZONE_CENTER = 0;
inline constexpr i32 DOCK_ZONE_LEFT = 1;
inline constexpr i32 DOCK_ZONE_RIGHT = 2;
inline constexpr i32 DOCK_ZONE_TOP = 3;
inline constexpr i32 DOCK_ZONE_BOTTOM = 4;

struct dock_node
{
    i32 kind = DOCK_LEAF;
    f32 ratio = 0.5f;       // split fraction (top/left share)
    dock_node *a = nullptr; // split children
    dock_node *b = nullptr;
    uiid panels[MAX_DOCK_PANELS]{}; // leaf/tabs
    const char *panel_names[MAX_DOCK_PANELS]{};
    i32 panel_count = 0;
    i32 active = 0;
};

struct dock_action
{
    bool active = false;         // a drop happened this frame
    uiid panel = 0;              // which panel moved
    dock_node *target = nullptr; // nullptr == undock (floating)
    i32 zone = DOCK_ZONE_CENTER;
};

// ---------------------------------------------------------------- interaction
enum class widget_state : u8
{
    NORMAL,
    HOVERED,
    ACTIVE,
    FOCUSED,
    DISABLED,
    INVALID,
    SELECTED
};

struct interaction
{
    bool hovered = false;
    bool pressed = false;
    bool activated = false; // press edge: became active this frame
    bool held = false;
    bool clicked = false;
    bool double_clicked = false;
    bool right_clicked = false; // right-press edge while hovered
    bool focused = false;
    bool disabled = false;
    bool captured = false;
    bool blocked = false; // a popup/panel layer swallowed this widget's input
};

// Which pointer button an input event belongs to.
enum class pointer_button : u8
{
    LEFT = 0,
    RIGHT = 1,
    MIDDLE = 2
};

// ---------------------------------------------------------------- ui
struct ui
{
    context *ctx = nullptr;
    explicit ui(context *c) : ctx(c) {}

    region region(rect area, uiid key, bool push_clip = true);
    // Scoped identity for loops and reusable components: widgets inside
    // derive with `local("part")`. RAII — destroy before end_frame.
    id_scope scope(uiid key) { return id_scope(*this, key); }

    uiid local(std::string_view key) const { return id_child(ctx->current_region, key); }
    uiid auto_id() { return id_child(0x5055494155544F00ull, ++ctx->seq); }

    // Layout metrics from the theme — the control rhythm (no magic numbers).
    f32 control_h() const { return ctx->active_theme.control_h; }
    f32 control_h_small() const { return ctx->active_theme.control_h_small; }
    f32 spacing() const { return ctx->active_theme.spacing; }
    f32 padding() const { return ctx->active_theme.padding; }
    // A button's natural size: label width + the theme's horizontal padding,
    // at the control height. `row::next(u.button_size("OK").x)` sizes rows.
    // (Non-const: first call may rasterize glyphs.)
    vec2 button_size(std::string_view label);
    // A single-line text's natural size (its drawn extent).
    vec2 text_size(std::string_view s);
    uiid hot_id() const { return ctx->hot; } // widget hovered this frame (tooltips)

    const theme &th() const { return ctx->active_theme; }

    bool key_pressed(key k) const { return ctx->key_pressed[static_cast<i32>(k)]; }
    bool key_down(key k) const { return ctx->key_down[static_cast<i32>(k)]; }

    // Pointer state for a rect. `focusable = false` keeps the widget out of
    // the Tab ring (a click-capturing scrim, a passive hit area) while it
    // still interacts normally.
    interaction interact(uiid id, rect area, bool enabled = true, bool focusable = true);

    // text
    f32 text_width(std::string_view s);
    void text(rect r, std::string_view s, color c, align a = ALIGN_LEFT);

    // measurement + wrapping
    measure_size measure_text(std::string_view s, f32 available_width);
    measure_size measure(function_ref<measure_size(f32)> fn, f32 available_width);
    void text_wrapped(rect r, std::string_view s, color c);
    // The height `text_wrapped` would draw into a `width`-wide rect: word wrap
    // with newline handling, measured without drawing. Use it to size containers
    // around wrapped paragraphs.
    f32 text_wrapped_height(f32 width, std::string_view s);
    // Truncates with a trailing U+2026 ellipsis when the line does not fit.
    void text_ellipsis(rect r, std::string_view s, color c, align a = ALIGN_LEFT);
    // Scoped font/size override for text drawn within its lifetime.
    text_scope text_style(f32 size, font_handle font = FONT_INVALID);
    // True when the active font has a real glyph for `cp` (not the replacement).
    bool has_glyph(u32 cp);
    // One line if it fits, otherwise wrapped within the rect. Returns the height
    // used (a single line, or the wrapped block's height).
    f32 text_fit(rect r, std::string_view s, color c, align a = ALIGN_LEFT);
    f32 line_height() const { return ctx->active_theme.text_size * 1.2f; }

    // True when `r` can paint: it intersects the active clip (always true
    // with no clip active). Widgets and custom drawing can skip work early.
    bool is_visible(rect r) const;

    // built-in widget (uses only the public API above)
    button_style resolve_button_style(uiid role_id = 0, const button_override &ov = {}) const;
    bool button(rect r, std::string_view label, uiid id, uiid role_id = 0, button_override ov = {});
    // Options-struct form: `u.button(r, "Save", "save"_id, {.role = "primary"_id})`.
    // The positional form stays as a thin wrapper.
    bool button(rect r, std::string_view label, uiid id, const button_opts &opts);
    void card(rect r, card_override ov = {});

    // common widgets
    bool checkbox(rect r, std::string_view label, bool &value, uiid id);
    bool slider_float(rect r, std::string_view label, f32 &value, f32 min_value, f32 max_value,
                      uiid id, const char *fmt = "%.1f");
    void progress_bar(rect r, f32 fraction, color fill, color bg);

    // Scrollable viewport (RAII): clips, offsets content, handles wheel + the
    // draggable scrollbar. State is keyed by `id`; set the content height while
    // the scope is alive (`set_content_height`).
    scroll_view scroll(rect viewport, uiid id, scroll_options ov = {});

    panel_scope panel(std::string_view title, rect &bounds, u32 flags = PANEL_NONE,
                      uiid dock_panel = 0, const char *dock_name = nullptr, panel_override ov = {});
    // Options form: `u.panel("Find", bounds, {.dock_panel = "left"_id})`.
    panel_scope panel(std::string_view title, rect &bounds, const panel_opts &opts);

    // splitters + docking
    std::pair<rect, rect> split_horizontal_interactive(uiid id, rect bounds, f32 *position,
                                                       f32 min_left = 60.0f, f32 max_left = 1.0e9f,
                                                       f32 thickness = 6.0f, f32 gap = 2.0f);
    std::pair<rect, rect> split_vertical_interactive(uiid id, rect bounds, f32 *position,
                                                     f32 min_top = 40.0f, f32 max_top = 1.0e9f,
                                                     f32 thickness = 6.0f, f32 gap = 2.0f);
    dock_action dock_space(uiid id, rect area, dock_node &root,
                           function_ref<void(uiid, rect, bool)> draw_panel);

    // typed inputs (write-back). text_field edits std::string directly;
    // number_field formats/parses a float using the codec behind the scenes.
    bool text_field(rect r, std::string &value, uiid id);
    bool number_field(rect r, f32 &value, uiid id, const char *fmt = "%.2f");

    // overlay layer (draw after base UI; input capture uses the previous frame)
    popup_scope popup(uiid id, rect area, popup_flags flags = popup_flags::NONE);

    // Custom window chrome (for borderless windows): draws the titlebar with
    // minimize / maximize / close glyph buttons on the right. Native dragging
    // and resizing (the system's own Aero Snap, its preview, and
    // maximize-on-double-click) come from the platform through
    // `install_window_chrome`; this widget only paints and handles the
    // buttons. `bounds` shrinks by the bar's height (lay out below it). A
    // close click sets `window::close_requested` for the app to act on.
    titlebar_result titlebar(window &w, rect &bounds, std::string_view title);

    // combo: a closed button showing `items[selected]`; clicking opens a dropdown
    // popup listing every item. Returns true when `selected` changed this frame.
    // The dropdown is 6.2 * line height tall per item plus padding, scrolls when
    // taller than `popup_height` (0 = size to content, up to `max_popup_height`).
    bool combo(rect r, std::string_view label, const char *const *items, i32 item_count,
               i32 &selected, uiid id, f32 max_popup_height = 220.0f);

    // tooltip: draws a small dark tooltip under `anchor` while the widget `id`
    // is hovered. First tooltip waits ~0.5 s; moving between widgets within
    // ~1.2 s opens instantly (the emilkowal delay pattern). The paint defers to
    // the end of the frame (nothing drawn later can cover it); returns the
    // placement rect, or a zero rect when nothing was placed this frame.
    rect tooltip(rect anchor, uiid id, std::string_view tip);

    // context menu: one call owns the whole pattern — opens on right press while
    // `anchor` is hovered, draws `item_labels` as a menu positioned at the
    // pointer (clamped to the window), closes on pick / outside press / escape.
    // Returns the picked item index, or -1 while nothing was picked. The app
    // keeps no open flag: the menu state lives under `id` (see context_menu_open).
    i32 context_menu(uiid id, rect anchor, const char *const *item_labels, i32 item_count);

    // primitives
    void draw_triangles(texture_handle tex, const vertex *vertices, i32 vertex_count,
                        const i32 *indices, i32 index_count);
    void draw_rect(rect r, color c);
    void draw_line(f32 x0, f32 y0, f32 x1, f32 y1, color c, f32 thickness = 1.0f);
    void draw_rounded_rect(rect r, color c, f32 radius = 0.0f);

    // images: one stretched quad, or a 9-slice patch (skins, custom widgets)
    void draw_image(const skin_image &img, rect dst);
    void draw_nine_slice(const skin_image &img, rect dst);

    // ---- animation (keys are region-scoped unless suffixed `_global`) ----
    // Call every frame; returns the current value moving toward `target`.
    f32 animate(uiid key, f32 target, const tween &spec);
    f32 animate(uiid key, f32 target, const spring &spec);
    f32 animate_global(uiid key, f32 target, const tween &spec);
    f32 animate_global(uiid key, f32 target, const spring &spec);
    // Frame-rate-independent exponential approach for continuously moving targets.
    f32 smooth(uiid key, f32 target, f32 half_life = 0.1f);
    // 0 on the first frame the key is seen, animating to 1 once (mount animation).
    f32 appear(uiid key, f32 duration = 0.2f, easing curve = easing::EASE_OUT);
    color animate_color(uiid key, color target, const tween &spec);
    bool animations_active() const;
    void set_reduced_motion(bool on) { ctx->reduced_motion = on; }

    // custom shapes (used by custom widgets, e.g. radial menus)
    void set_cursor(cursor c) { ctx->want_cursor = c; }
    void blur(rect r, f32 blur_radius = 12.0f, f32 corner_radius = 0.0f, f32 alpha = 1.0f);
    void draw_polygon(std::span<const vec2> points, color c);
    void draw_sector(vec2 center, f32 r_in, f32 r_out, f32 a0, f32 a1, color c);
    void draw_arc(vec2 center, f32 radius, f32 thickness, f32 a0, f32 a1, color c);
};

// ---------------------------------------------------------------- components
// The built-in component library (`pui::comp`): reusable, themeable widgets
// built ONLY on the public API, following the component convention — explicit
// ids (never label-derived), props/result structs, theme-driven styles, no
// file-scope statics, keyboard operable, cursor feedback included. The
// implementations live in the implementation section.
namespace comp
{
struct switch_props
{
    uiid id = 0; // required: identity is explicit
    bool enabled = true;
    switch_override style{};
};
struct switch_result
{
    bool changed = false;
    interaction in{};
    explicit operator bool() const { return changed; }
};
// The track width + gap + label width: `row.next(comp::switch_size(u, "Wi-Fi").x)`.
vec2 switch_size(ui &u, std::string_view label);
// An animated toggle: click, or Space/Enter when focused. Writes `value`.
switch_result switch_toggle(ui &u, rect area, std::string_view label, bool &value,
                            const switch_props &p = {});

struct radio_props
{
    uiid id = 0; // required
    bool enabled = true;
    f32 row_h = 0.0f; // 0 = the theme's control height
    radio_override style{};
};
struct radio_result
{
    bool changed = false;
    i32 selected = -1;
    interaction in{};
    explicit operator bool() const { return changed; }
};
// A vertical radio group, one row per label. Every row joins the Tab ring;
// Space/Enter selects the focused row.
radio_result radio_group(ui &u, rect area, std::span<const char *const> labels, i32 &selected,
                         const radio_props &p = {});

struct segmented_props
{
    uiid id = 0; // required
    bool enabled = true;
    segmented_override style{};
};
struct segmented_result
{
    bool changed = false;
    i32 selected = -1;
    interaction in{};
    explicit operator bool() const { return changed; }
};
// A horizontal segmented control, one segment per label: the selected
// segment is filled; clicks and Space/Enter select.
segmented_result segmented(ui &u, rect area, std::span<const char *const> labels, i32 &selected,
                           const segmented_props &p = {});

struct tabs_props
{
    uiid id = 0; // required
    bool enabled = true;
    tabs_override style{};
};
struct tabs_result
{
    bool changed = false;
    i32 active = -1;
    interaction in{};
    explicit operator bool() const { return changed; }
};
// A horizontal tab bar (the app draws the content below): the active tab is
// highlighted with an underline; clicks and Space/Enter activate.
tabs_result tab_bar(ui &u, rect area, std::span<const char *const> labels, i32 &active,
                    const tabs_props &p = {});

struct accordion_props
{
    uiid id = 0; // required
    bool enabled = true;
    f32 header_h = 0.0f;  // 0 = the theme's
    f32 content_h = 0.0f; // the content's natural height (drives the animation)
    accordion_override style{};
};
// A collapsible section: the header toggles `open`, the content area animates
// its height and clips. Draw the content into `content()` while the scope is
// alive. `toggled()` reports the header interaction.
struct accordion_scope
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    bool open_ = false;
    bool toggled_ = false;
    bool pushed_clip_ = false;
    rect content_{};

    accordion_scope(ui &u, rect area, std::string_view title, bool &open,
                    const accordion_props &p = {});
    ~accordion_scope();

    accordion_scope(const accordion_scope &) = delete;
    accordion_scope &operator=(const accordion_scope &) = delete;
    accordion_scope(accordion_scope &&) = delete;
    accordion_scope &operator=(accordion_scope &&) = delete;

    [[nodiscard]] rect content() const { return content_; }
    explicit operator bool() const { return open_; }
    bool toggled() const { return toggled_; }
};

enum class drawer_edge : u8
{
    LEFT,
    RIGHT
};

struct drawer_props
{
    uiid id = 0; // required
    bool enabled = true;
    f32 width = 280.0f;
    drawer_edge edge = drawer_edge::LEFT;
    bool scrim = true; // dim + click-to-close behind the drawer
    bool close_on_scrim_click = true;
    drawer_override style{};
};
// A sliding side drawer over `host`: dims (optionally), clips and slides its
// content in from an edge. The scrim captures clicks (and closes when
// `close_on_scrim_click`); the content widgets interact above it.
struct drawer_scope
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    bool open_ = false;
    bool toggled_ = false;
    bool pushed_clip_ = false;
    rect content_{};

    drawer_scope(ui &u, rect host, bool &open, const drawer_props &p = {});
    ~drawer_scope();

    drawer_scope(const drawer_scope &) = delete;
    drawer_scope &operator=(const drawer_scope &) = delete;
    drawer_scope(drawer_scope &&) = delete;
    drawer_scope &operator=(drawer_scope &&) = delete;

    [[nodiscard]] rect content() const { return content_; }
    explicit operator bool() const { return open_; }
    bool toggled() const { return toggled_; }
};

// ---- toast host -------------------------------------------------------------
// An app-owned queue of transient notifications. `push` from anywhere (any
// frame); `toast_draw` renders the stack, ages it out and is a no-op while
// empty. Kind: 0 info, 1 success, 2 danger.
struct toast_item
{
    char text[160]{};
    u32 kind = 0;
    u64 serial = 0;
    f64 born = 0.0; // stamped on the first draw
    bool stamped = false;
    bool used = false;
};
struct toast_host
{
    static constexpr i32 MAX_TOASTS = 8;
    toast_item items[MAX_TOASTS]{};
    u64 next_serial = 1;

    void push(const char *text, u32 kind = 0);
    i32 alive() const;
};

struct toast_props
{
    uiid id = 0;
    bool enabled = true;
    toast_override style{};
};
// Stacks the live toasts downward from `anchor`'s top-right corner.
void toast_draw(ui &u, rect anchor, toast_host &host, const toast_props &p = {});

// ---- table ------------------------------------------------------------------
struct table_props
{
    uiid id = 0;
    bool enabled = true;
    f32 row_h = 0.0f;    // 0 = the theme's
    f32 header_h = 0.0f; // 0 = the theme's
    table_override style{};
};
struct table_result
{
    i32 clicked_row = -1;
    i32 clicked_col = -1;
    i32 focused_row = -1;
    explicit operator bool() const { return clicked_row >= 0; }
};
// A uniform-column table over `area` (headers pinned, body scrolls with
// virtual_list: only visible rows are submitted). `cell_draw` paints one
// cell — the component owns the chrome, hit-testing and scrolling.
table_result table(ui &u, rect area, std::span<const char *const> headers, i32 row_count,
                   function_ref<void(ui &, rect, i32 row, i32 col)> cell_draw,
                   const table_props &p = {});

// ---- command palette --------------------------------------------------------
struct palette_command
{
    const char *name = "";
    const char *hint = "";
};
struct palette_state
{
    std::string query;
    i32 active = 0;
};
struct palette_props
{
    uiid id = 0;
    palette_override style{};
};
struct palette_result
{
    i32 chosen = -1; // the ORIGINAL command index, or -1
    i32 active = -1; // the highlighted entry this frame (filtered order)
    i32 shown = 0;   // how many entries passed the filter
    explicit operator bool() const { return chosen >= 0; }
};
// A modal filter-and-run overlay (Ctrl+K style). While `open`: the scrim
// captures clicks, the query field takes focus, Up/Down move, Enter chooses,
// Escape closes. Returns the chosen command index (or -1).
palette_result command_palette(ui &u, rect screen, bool &open, palette_state &st,
                               std::span<const palette_command> commands,
                               const palette_props &p = {});
} // namespace comp

// A top input-capturing layer. Construct it (as a prvalue) after the base UI so
// it draws on top; it pops on destruction.
struct popup_scope
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    rect area_{};
    bool open = true;
    bool close_requested = false;

    popup_scope(ui &u, uiid id, rect area, popup_flags flags);
    ~popup_scope();

    popup_scope(const popup_scope &) = delete;
    popup_scope &operator=(const popup_scope &) = delete;
    popup_scope(popup_scope &&) = delete;
    popup_scope &operator=(popup_scope &&) = delete;

    [[nodiscard]] rect content() const { return area_; }
    explicit operator bool() const { return open; }
};

// A draggable floating container. The client area is a region scope, so children
// are ID-scoped and clipped; it pops on destruction.
struct panel_scope
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    rect bounds_{};
    rect content_{};
    region client_;
    bool close_requested = false;
    bool open = true;
    bool layer_pushed = false;

    panel_scope(ui &u, uiid id, rect bounds, rect client, rect content, bool close_clicked,
                bool pushed)
        : u_(&u), id_(id), bounds_(bounds), content_(content), client_(u, id, client, true),
          close_requested(close_clicked), open(true), layer_pushed(pushed)
    {
    }
    ~panel_scope();

    panel_scope(const panel_scope &) = delete;
    panel_scope &operator=(const panel_scope &) = delete;
    panel_scope(panel_scope &&) = delete;
    panel_scope &operator=(panel_scope &&) = delete;

    [[nodiscard]] rect content() const { return content_; }
    explicit operator bool() const { return open; }
};

// Scoped font/size override for text drawn within its lifetime. Restores the
// previous theme font/size on destruction (keep it in a scoped block).
struct text_scope
{
    ui *u_ = nullptr;
    f32 size_ = 0.0f;
    font_handle font_ = FONT_INVALID;
    f32 old_size_ = 0.0f;
    font_handle old_font_ = FONT_INVALID;

    text_scope(ui &u, f32 size, font_handle font = FONT_INVALID);
    ~text_scope();

    text_scope(const text_scope &) = delete;
    text_scope &operator=(const text_scope &) = delete;
    text_scope(text_scope &&) = delete;
    text_scope &operator=(text_scope &&) = delete;
};

// Scrollable viewport (RAII). Clips to `viewport`, offsets its content, applies
// wheel input when the pointer is over it (innermost view wins), and draws a
// draggable scrollbar. Offset and content height are keyed by `id` and collected
// after 5 s of disuse, like animation keys.
//
//   {
//       scroll_view sv = u.scroll(area, "list"_id);
//       column col(sv.content(), 6.0f);
//       for (...) { ... col.next(...) ... }
//       sv.set_content_height(used);
//   }
struct scroll_view
{
    ui *u_ = nullptr;
    uiid id_ = 0;
    rect viewport_{};
    rect content_{};
    f32 content_h_ = 0.0f;
    f32 padding_ = 4.0f;
    u32 flags_ = SCROLL_NONE;
    bool overflows_ = false;
    bool clip_pushed_ = false;
    rect bar_track_{};
    rect bar_thumb_{};
    bool thumb_hovered_ = false;

    scroll_view(ui &u, uiid id, rect viewport, scroll_options ov);
    ~scroll_view();

    scroll_view(const scroll_view &) = delete;
    scroll_view &operator=(const scroll_view &) = delete;
    scroll_view(scroll_view &&) = delete;
    scroll_view &operator=(scroll_view &&) = delete;

    [[nodiscard]] rect content() const { return content_; }
    f32 offset() const;
    f32 content_height() const { return content_h_; }
    bool overflows() const { return overflows_; }
    void set_content_height(f32 height);
    void scroll_to(f32 offset);
    void scroll_by(f32 delta);
    void ensure_visible(rect r, f32 margin = 4.0f);
    // Draws the visible slice of a uniform list. Rows outside the viewport
    // are never submitted this frame — the callback only runs for indices
    // that can paint. The content height is derived from count * item_height
    // (no separate set_content_height call); irregular rows keep using manual
    // slicing with content().
    void virtual_list(i32 count, f32 item_height, function_ref<void(ui &, i32, rect)> fn);
};

// ---------------------------------------------------------------- cursors
struct column
{
    rect bounds_{};
    f32 gap_ = 6.0f;
    explicit column(rect area, f32 gap = 6.0f) : bounds_(area), gap_(gap) {}
    rect next(f32 height = 0.0f);
    [[nodiscard]] rect cut_top(f32 height) &;
    rect cut_top(f32 height) && = delete;
    [[nodiscard]] rect cut_bottom(f32 height) &;
    rect cut_bottom(f32 height) && = delete;
    void space(f32 amount = 6.0f);
    rect remaining() const { return bounds_; }
    column(const column &) = delete;
    column(column &&) = delete;
};

struct row
{
    rect bounds_{};
    f32 gap_ = 6.0f;
    explicit row(rect area, f32 gap = 6.0f) : bounds_(area), gap_(gap) {}
    rect next(f32 width = 0.0f);
    [[nodiscard]] rect cut_left(f32 width) &;
    rect cut_left(f32 width) && = delete;
    [[nodiscard]] rect cut_right(f32 width) &;
    rect cut_right(f32 width) && = delete;
    void space(f32 amount = 6.0f);
    rect remaining() const { return bounds_; }
    row(const row &) = delete;
    row(row &&) = delete;
};

// ---------------------------------------------------------------- tracks & grids
inline constexpr i32 MAX_TRACKS = 16;

enum class track_type
{
    FIXED,
    FLEX,
    RATIO,
    FIT_CONTENT
};

struct track_size
{
    track_type type = track_type::FLEX;
    f32 value = 1.0f;
    f32 min_size = 0.0f;
    f32 max_size = 1e9f;

    static track_size fixed(f32 px) { return {track_type::FIXED, px, px, px}; }
    static track_size flex(f32 weight = 1.0f) { return {track_type::FLEX, weight, 0.0f, 1e9f}; }
    static track_size ratio(f32 fraction) { return {track_type::RATIO, fraction, 0.0f, 1e9f}; }
    static track_size fit_content(f32 fallback_px = 0.0f, f32 max_px = 1e9f)
    {
        return {track_type::FIT_CONTENT, 0.0f, fallback_px, max_px};
    }

    track_size min(f32 m) const
    {
        track_size s = *this;
        s.min_size = m;
        return s;
    }
    track_size max(f32 m) const
    {
        track_size s = *this;
        s.max_size = m;
        return s;
    }
};

// Resolves FIXED / RATIO / FIT_CONTENT tracks against `available`, then shares
// what is left over the FLEX tracks (min/max clamped, redistributing space freed
// by clamped tracks). Writes at most `max_out` sizes; returns the count written.
i32 resolve_track_sizes(f32 available, std::span<const track_size> tracks, f32 *out, i32 max_out);

// Cursor over resolved horizontal tracks — a `row` that knows each column's size.
struct track_row
{
    rect bounds_{};
    f32 gap_ = 0.0f;
    f32 sizes_[MAX_TRACKS]{};
    i32 count_ = 0;
    i32 index_ = 0;

    track_row(rect area, std::span<const track_size> tracks, f32 gap = 0.0f);
    track_row(rect area, std::initializer_list<track_size> tracks, f32 gap = 0.0f)
        : track_row(area, std::span<const track_size>(tracks.begin(), tracks.size()), gap)
    {
    }

    rect next(f32 height = 0.0f);
    rect remaining() const;
    f32 size(i32 i) const { return (i >= 0 && i < count_) ? sizes_[i] : 0.0f; }
    i32 count() const { return count_; }
    i32 index() const { return index_; }
    bool done() const { return index_ >= count_; }
    track_row(const track_row &) = delete;
    track_row(track_row &&) = delete;
};

// Cursor over resolved vertical tracks.
struct track_column
{
    rect bounds_{};
    f32 gap_ = 0.0f;
    f32 sizes_[MAX_TRACKS]{};
    i32 count_ = 0;
    i32 index_ = 0;

    track_column(rect area, std::span<const track_size> tracks, f32 gap = 0.0f);
    track_column(rect area, std::initializer_list<track_size> tracks, f32 gap = 0.0f)
        : track_column(area, std::span<const track_size>(tracks.begin(), tracks.size()), gap)
    {
    }

    rect next(f32 width = 0.0f);
    rect remaining() const;
    f32 size(i32 i) const { return (i >= 0 && i < count_) ? sizes_[i] : 0.0f; }
    i32 count() const { return count_; }
    i32 index() const { return index_; }
    bool done() const { return index_ >= count_; }
    track_column(const track_column &) = delete;
    track_column(track_column &&) = delete;
};

// CSS `repeat(auto-fit, minmax(min_item_w, 1fr))`-style grid cursor. `next()`
// returns the next cell in reading order (a zero-size rect once exhausted);
// `cell(i)` addresses a cell directly.
struct grid_cursor
{
    rect bounds_{};
    f32 gap_ = 8.0f;
    f32 item_w_ = 0.0f;
    f32 item_h_ = 0.0f;
    i32 columns_ = 1;
    i32 rows_ = 1;
    i32 count_ = 0;
    i32 index_ = 0;

    rect next();
    rect cell(i32 i) const;
    bool done() const { return index_ >= count_; }
    i32 count() const { return count_; }
    i32 index() const { return index_; }
    i32 columns() const { return columns_; }
    i32 rows() const { return rows_; }
    f32 item_width() const { return item_w_; }
    f32 item_height() const { return item_h_; }
};

grid_cursor auto_fit_grid(rect container, i32 total_items, f32 min_item_w, f32 item_h,
                          f32 gap = 8.0f);

// Layout-overflow reporting: when enabled, a `column::next`/`row::next` (or
// track/grid slice) that gets clamped because the remaining space is smaller than
// requested reports a *non-fatal* violation ("column slice clamped: requested N
// px > remaining M px") — visible through `set_violation_handler`/`violation_count`,
// so authoring mistakes surface in tests instead of only in screenshots. Off by
// default; clamping itself is unchanged (still silent when disabled).
void set_report_layout_overflow(context *c, bool enabled);
i32 layout_overflow_count(context *c); // clamps counted since enable (or since reset)

// ---- development overlay ----
// The last violation message recorded on this context, exactly as the guard
// wrote it ("" when none). The messages are stable string literals, so tests
// may match on them; they are not localized and never change shape between
// builds.
const char *violation_last(context *c);
// The enumerated code of the last violation (VIOL_NONE when clean) - the
// stable, non-string identity tests may rely on. `violation_code_name`
// renders it.
violation_code violation_last_code(context *c);
const char *violation_code_name(violation_code code);
// Opt out of the debug build's stop-at-the-violating-call behavior (default:
// on in debug builds, only when no handler is installed).
void set_break_on_violation(context *c, bool enabled);

// ---- dock layout persistence ----
// Serialize a dock tree: one line per node — "0 split h 0.60" /
// "1 leaf 0 id1|Name1;id2|Name2" (depth, kind, then the split ratio or the
// active tab plus id|name pairs). Indent-free and line-based, so it round-
// trips through any text storage (settings files, clipboard, defaults).
std::string dock_save_tree(const dock_node &root);
// Restore into caller-owned node storage (`nodes`/`node_cap` — the same
// pattern as a drag-alloc pool; nodes are consumed in order). Every restored
// panel name points into `name_storage`, which must outlive the tree.
// Returns false on malformed input. `out_root` is the new tree (or null for
// an empty serialization).
bool dock_restore_tree(std::string_view text, dock_node *nodes, i32 node_cap,
                       std::string &name_storage, dock_node *&out_root);
// Draws a small "PufferUI: N violation(s) - <last message>" strip into `r`
// when the context has violations (a no-op otherwise, so release frames stay
// clean). Intended as an opt-in development overlay.
void draw_violation_overlay(ui &u, rect r);

// ---------------------------------------------------------------- frame + input
context *create_context(render_device *device = nullptr, render_surface *surface = nullptr);
void destroy_context(context *c);
void set_device(context *c, render_device *device, render_surface *surface);
void set_theme(context *c, const theme &t);

font_handle load_font(context *c, const char *path);

// ---- windows ----
// The first window may always be added; every later window requires
// backend_caps::SHARED_DEVICE (without it the call is a reported violation and
// returns nullptr, never a silent degrade). `handle` is the native window and
// is what event routing matches on.
//
// remove_window compacts the context's window list: every `window*` handed out
// for a window AFTER the removed one is invalidated (it may point past the
// list or at a different window, and using it is a reported violation).
// Re-fetch pointers with `window_at(handle)` after any removal; the first
// window's pointer never moves.
window *add_window(context *c, void *handle, render_surface *surface, rect client);
void remove_window(context *c, window &w);
window *window_at(context *c, void *handle);
window *first_window(context *c);

void set_window_client(window &w, rect client);
void focus_window(context *c, window &w);
window *focused_window(context *c);
rect desktop_rect(context *c);

// Pointer position in desktop coordinates (one pointer, all windows). The value
// MUST use the same coordinate space as the window client rects; prefer feeding
// window-local mouse events (`mouse_move(window&, ...)`), which the core converts
// into the shared space. Platform "global mouse" queries may return a different
// space (SDL3's SDL_GetGlobalMouseState returns unscaled screen pixels while its
// window and event coordinates are content-scaled).
void set_global_mouse(context *c, f32 x, f32 y);

// ---- frames ----
// One window's frame. Windows are framed one after another; the context keeps a
// single draw list, so a window's frame must be closed before the next begins.
void begin_frame(context *c, window &w, f64 now, f64 dt);
void begin_frame(context *c, f64 now, f64 dt, rect screen); // primary window
void end_frame(context *c);
// Conservative "needs redraw" gate for idle sleeping: true when an animation
// is unsettled, a text field is focused (the caret blinks), or a text drag is
// in flight. The bootstrap's `wait_when_idle` uses it per pump.
bool needs_redraw(const context *c);

// ---- input ----
// Per-window: mouse edges and text go to the window that received the event;
// the held state and pointer position are global.
void mouse_move(window &w, f32 x, f32 y);
void mouse_button(window &w, bool down);                   // left button
void mouse_button(window &w, pointer_button b, bool down); // any button
void key_event(window &w, key k, bool down);
void text_input_event(window &w, const char *utf8);
void ime_event(window &w, const char *preedit, i32 cursor);
void mods_event(window &w, bool shift, bool ctrl);
void mouse_wheel(window &w, f32 dx, f32 dy);

// Single-window convenience: routes to the primary window.
void mouse_move(context *c, f32 x, f32 y);
void mouse_button(context *c, bool down);
void mouse_button(context *c, pointer_button b, bool down);
void key_event(context *c, key k, bool down);
void text_input_event(context *c, const char *utf8);
void ime_event(context *c, const char *preedit, i32 cursor);
void mods_event(context *c, bool shift, bool ctrl);
void mouse_wheel(context *c, f32 dx, f32 dy);
void set_clipboard(context *c, clipboard *clip);

// ---- native window chrome (borderless windows) ----
// Install the platform's native chrome handling for a borderless window:
// native caption dragging (the system's own Aero Snap, its preview, and
// maximize-on-double-click) and native edge/corner resizing with generous
// borders. Call once per window after add_window; no-op without a backend
// that supports it.
void install_window_chrome(context *c, window &w);

i32 violation_count(context *c);

} // namespace pui

// ============================================================================
// ============================ implementation ================================
// ============================================================================
#if defined(PUFFERUI_IMPLEMENTATION)

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#if defined(PUFFERUI_ENABLE_SDL3)
#include <SDL3/SDL.h>
#endif

namespace pui
{

namespace detail
{
inline context **current_slot()
{
    // One context per thread: the slot is genuinely thread-local, so two
    // threads (or an editor with an embedded preview on a worker thread)
    // never cross-report violations or layout clamps.
    thread_local context *slot = nullptr;
    return &slot;
}

// Non-fatal truncation note (see set_report_layout_overflow). Called only when
// the requested slice does not fit the remaining space. column/row carry no
// context pointer, so this uses the current context of the calling thread
// (a cursor is only ever used inside its frame, where the slot is set — and
// the slot is thread-local: one context per thread).
inline void report_slice_clamp(const char *what, f32 requested, f32 granted)
{
    context *c = *current_slot();
    if (!c || !c->report_layout_overflow || requested <= granted + 0.001f) return;
    c->overflow_count += 1;
    char message[160];
    std::snprintf(message, sizeof(message),
                  "%s slice clamped: requested %.0f px > remaining %.0f px", what,
                  static_cast<double>(requested), static_cast<double>(granted));
    report_violation(VIOL_LAYOUT_CLAMPED, "clamped", message, "", 0, false);
}
} // namespace detail

// Deferred-menu label storage (owned copies; see context::defers).
struct defer_store
{
    std::vector<char> buf;          // packed label strings
    std::vector<const char *> ptrs; // one per label, into buf
};

namespace detail
{
inline defer_store *ensure_defers(context *c)
{
    if (!c->defers) c->defers = new defer_store();
    return static_cast<defer_store *>(c->defers);
}

// Copies a panel dock name into context-owned storage: the drag ghost draws
// across frames, so the caller string need not outlive the gesture.
inline const char *intern_dock_name(context *c, const char *name)
{
    if (!name)
    {
        c->dock_panel_name_buf[0] = '\0';
        return c->dock_panel_name_buf;
    }
    std::snprintf(c->dock_panel_name_buf, sizeof(c->dock_panel_name_buf), "%s", name);
    return c->dock_panel_name_buf;
}
// Growth-on-demand for the overlay stacks: the fixed caps are initial
// capacities, so deep popup/panel nesting never refuses a layer (and the
// VIOL_POPUP_OVERFLOW guard is no longer reachable).
inline void ensure_popup_capacity(context *c, i32 need)
{
    if (need <= c->popup_capacity) return;
    i32 cap = c->popup_capacity > 0 ? c->popup_capacity : MAX_POPUPS;
    while (cap < need) cap *= 2;
    popup_entry *fresh = new popup_entry[cap];
    for (i32 i = 0; i < c->popup_depth; ++i) fresh[i] = c->popups[i];
    popup_entry *fresh_prev = new popup_entry[cap];
    for (i32 i = 0; i < c->prev_popup_depth; ++i) fresh_prev[i] = c->prev_popups[i];
    delete[] c->popups;
    delete[] c->prev_popups;
    c->popups = fresh;
    c->prev_popups = fresh_prev;
    c->popup_capacity = cap;
}

inline void ensure_panel_capacity(context *c, i32 need)
{
    if (need <= c->panel_capacity) return;
    i32 cap = c->panel_capacity > 0 ? c->panel_capacity : MAX_PANELS;
    while (cap < need) cap *= 2;
    panel_entry *fresh = new panel_entry[cap];
    for (i32 i = 0; i < c->panel_depth; ++i) fresh[i] = c->panels[i];
    panel_entry *fresh_prev = new panel_entry[cap];
    for (i32 i = 0; i < c->prev_panel_depth; ++i) fresh_prev[i] = c->prev_panels[i];
    delete[] c->panels;
    delete[] c->prev_panels;
    c->panels = fresh;
    c->prev_panels = fresh_prev;
    c->panel_capacity = cap;
}

inline void ensure_panel_stack_capacity(context *c, i32 need)
{
    if (need <= c->panel_stack_capacity) return;
    i32 cap = c->panel_stack_capacity > 0 ? c->panel_stack_capacity : MAX_PANELS;
    while (cap < need) cap *= 2;
    uiid *fresh = new uiid[cap];
    for (i32 i = 0; i < c->panel_stack_depth; ++i) fresh[i] = c->panel_stack[i];
    delete[] c->panel_stack;
    c->panel_stack = fresh;
    c->panel_stack_capacity = cap;
}
} // namespace detail

// Records a deferred menu's labels into context-owned storage. All strings
// are appended first, then the pointers are built, so no pointer can dangle
// behind a later reallocation.
inline void defer_set_labels(context *c, const char *const *items, i32 count)
{
    defer_store *ds = detail::ensure_defers(c);
    ds->buf.clear();
    ds->ptrs.clear();
    for (i32 i = 0; i < count; ++i)
    {
        const char *src = items[i] ? items[i] : "";
        usize n = 0;
        while (src[n] != '\0') ++n;
        const usize at = ds->buf.size();
        ds->buf.resize(at + n + 1);
        std::memcpy(ds->buf.data() + at, src, n + 1);
    }
    usize off = 0;
    for (i32 i = 0; i < count; ++i)
    {
        ds->ptrs.push_back(ds->buf.data() + off);
        off += std::strlen(ds->buf.data() + off) + 1;
    }
    c->defer_count = count;
}

void set_window_host(context *c, window_host *host)
{
    if (c) c->host = host;
}

window_host *window_host_of(context *c)
{
    return c ? c->host : nullptr;
}

context *current_context()
{
    return *detail::current_slot();
}
void set_current_context(context *c)
{
    *detail::current_slot() = c;
}

// The debug break, portably. SDL3 builds use SDL's breakpoint (every platform
// SDL3 supports); the backend-agnostic core falls back to compiler intrinsics
// (MSVC: __debugbreak; GCC/clang: __builtin_trap, a SIGTRAP a debugger picks
// up).
#if defined(PUFFERUI_ENABLE_SDL3)
#define PUFFERUI_BREAK() SDL_TriggerBreakpoint()
#elif defined(_MSC_VER)
#define PUFFERUI_BREAK() __debugbreak()
#elif defined(__GNUC__)
#define PUFFERUI_BREAK() __builtin_trap()
#else
#define PUFFERUI_BREAK() ((void)0)
#endif

void report_violation(violation_code code, const char *condition, const char *message,
                      const char *file, int line, bool fatal)
{
    if (context *c = current_context())
    {
        ++c->violations;
        c->vlast_code = code;
        std::snprintf(c->vlast, sizeof(c->vlast), "%s", message ? message : "");
        if (c->vhandler && !fatal)
        {
            // A handler captures contract violations; a fatal one is a broken
            // invariant and stops immediately below without consulting it.
            c->vhandler(c->vhandler_user, condition, message, file, line);
            return;
        }
        if (fatal)
        {
            // PUFFERUI_ASSERT is a broken invariant, not a contract nudge:
            // report loudly and stop, in every build, handled or not.
            std::fprintf(stderr, "[pufferui] fatal assertion failed: %s (%s) at %s:%d\n",
                         message ? message : "", condition ? condition : "", file, line);
            std::fflush(stderr);
            PUFFERUI_BREAK();
            std::abort();
        }
#if !defined(NDEBUG)
        // Debug builds stop the debugger at the violating call: a violation
        // nobody handles is a bug, and the fix is fastest exactly there.
        // Tests install a handler (they capture and assert) so they never
        // break here; retail builds keep the cheap detection but never stop.
        // SDL3 builds use SDL's portable breakpoint (every platform SDL3
        // supports); the backend-agnostic core falls back to compiler
        // intrinsics.
        if (c->break_on_violation) PUFFERUI_BREAK();
#endif
    }
    std::fprintf(stderr, "[pufferui] violation: %s (%s) at %s:%d\n", message ? message : "",
                 condition ? condition : "", file, line);
}

void set_violation_handler(context *c, violation_handler handler, void *user)
{
    c->vhandler = handler;
    c->vhandler_user = user;
}

// ---- text store ----
struct glyph
{
    texture_handle tex = nullptr;
    f32 xoff = 0, yoff = 0, advance = 0;
    f32 u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

struct font_data
{
    std::vector<u8> buffer;
    stbtt_fontinfo info{};
    bool ok = false;
};

template <class V> struct flat_map
{
    struct slot
    {
        u64 hash = 0; // 0 = empty (real hashes are mixed to never be 0)
        uiid key = 0;
        V value{};
    };
    std::vector<slot> slots;
    usize used = 0;

    static u64 mix(u64 h)
    {
        h *= 0x9E3779B97F4A7C15ull;
        h ^= h >> 32;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 31;
        return h ? h : 1;
    }

    void rehash(usize new_cap)
    {
        std::vector<slot> old;
        old.swap(slots);
        slots.assign(new_cap, slot{});
        for (const slot &s : old)
        {
            if (!s.hash) continue;
            usize i = s.hash & (new_cap - 1);
            while (slots[i].hash) i = (i + 1) & (new_cap - 1);
            slots[i] = s;
        }
    }

    V *find(uiid key)
    {
        if (used == 0 || slots.empty()) return nullptr;
        const u64 h = mix(key);
        const usize mask = slots.size() - 1;
        usize i = static_cast<usize>(h) & mask;
        while (slots[i].hash)
        {
            if (slots[i].hash == h && slots[i].key == key) return &slots[i].value;
            i = (i + 1) & mask;
        }
        return nullptr;
    }

    V &at(uiid key) // find-or-insert
    {
        if (slots.empty() || used * 4 >= slots.size() * 3) // load factor 0.75
        {
            const usize cap = slots.empty() ? 64 : slots.size() * 2;
            rehash(cap);
        }
        const u64 h = mix(key);
        const usize mask = slots.size() - 1;
        usize i = static_cast<usize>(h) & mask;
        while (slots[i].hash)
        {
            if (slots[i].hash == h && slots[i].key == key) return slots[i].value;
            i = (i + 1) & mask;
        }
        slots[i].hash = h;
        slots[i].key = key;
        used += 1;
        return slots[i].value;
    }

    // Wholesale clear (per frame); capacity is kept.
    void clear()
    {
        for (slot &s : slots) s.hash = 0;
        used = 0;
    }
};

// Direct-mapped cache: a fixed array of {tag, value}; lookup is one probe.
// Collision = overwrite + recompute, so only use it for data whose
// recomputation is cheap and side-effect-free (the purge disappears).
template <class T, usize N> struct direct_cache
{
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
    struct slot
    {
        u64 tag = 0;
        T value{};
    };
    slot slots[N];

    static u64 mix(u64 h)
    {
        h *= 0x9E3779B97F4A7C15ull;
        h ^= h >> 32;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 31;
        return h ? h : 1;
    }

    T *find(u64 h)
    {
        const u64 m = mix(h);
        slot &s = slots[static_cast<usize>(m) & (N - 1)];
        return (s.tag == m) ? &s.value : nullptr;
    }
    void put(u64 h, const T &v)
    {
        const u64 m = mix(h);
        slot &s = slots[static_cast<usize>(m) & (N - 1)];
        s.tag = m;
        s.value = v;
    }
};

struct text_store
{
    std::vector<font_data> fonts;

    // (font, size, codepoint) as a struct key: no aliasing between (size, cp)
    // pairs, unlike the old packed u64 (cp can exceed the packing multiplier).
    struct glyph_key
    {
        i32 font;
        i32 size_q; // size quantized to quarter pixels
        u32 cp;
        bool operator==(const glyph_key &) const = default;
    };
    struct glyph_key_hash
    {
        usize operator()(const glyph_key &k) const
        {
            u64 h = static_cast<u64>(static_cast<u32>(k.font));
            h = h * 0x9E3779B97F4A7C15ull + static_cast<u64>(k.size_q);
            h = h * 0x9E3779B97F4A7C15ull + static_cast<u64>(k.cp);
            h ^= h >> 33;
            h *= 0xff51afd7ed558ccdull;
            h ^= h >> 33;
            return static_cast<usize>(h);
        }
    };
    std::unordered_map<glyph_key, glyph, glyph_key_hash> glyphs;

    // Text layout cache (direct-mapped, one probe; collisions rebuild —
    // side-effect-free): (font, size, string hash) -> width + the per-glyph
    // run (kern-in + glyph copy). text_width reads the width; ui::text emits
    // from the run — one cache, no decode and no glyph-map lookups on hit.
    struct cached_glyph
    {
        f32 kern_in = 0.0f;
        glyph g{};
    };
    struct text_layout
    {
        f32 width = 0.0f;
        std::vector<cached_glyph> gs;
    };
    direct_cache<text_layout, 4096> layout_cache;

    // Glyph atlases. One 1024x1024 page holds a full UI set; pages spill when
    // it fills, so non-Latin vocabularies scale (each glyph carries its own
    // page's texture, and batching already flushes on texture change).
    struct atlas_page
    {
        texture_handle tex = nullptr;
        i32 shelf_x = 0, shelf_y = 0, shelf_h = 0;
    };
    std::vector<atlas_page> atlases;
    i32 atlas_w = 0, atlas_h = 0;
};

struct edit_state
{
    std::string buffer;
    usize caret = 0;
    usize anchor = 0;
    f32 scroll_x = 0.0f; // keeps the caret inside the visible field
    bool focused = false;
    bool dragging = false;     // mouse drag-select
    bool pending_drag = false; // pressed inside the selection: may become a move drag
    bool drag_moved = false;   // moved past the drag threshold this press
    f32 press_x = 0.0f;
    f32 press_y = 0.0f;
    i32 click_streak = 0; // double/triple click detection
    f64 last_click_time = -1.0;
    f32 last_click_x = 0.0f, last_click_y = 0.0f;

    // Undo/redo: snapshots of (text, caret, anchor). Consecutive single-character
    // edits coalesce into one step.
    struct snapshot
    {
        std::string text;
        usize caret = 0;
        usize anchor = 0;
    };
    std::vector<snapshot> undo;
    std::vector<snapshot> redo;
    f64 last_edit_time = -1.0;
};

struct edit_store
{
    std::unordered_map<uiid, edit_state> map;
};

struct focus_store
{
    std::vector<uiid> current;
    std::vector<uiid> prev;
};

struct blur_store
{
    texture_handle half = nullptr, quarter = nullptr, eighth = nullptr, sixteenth = nullptr;
    i32 w = 0, h = 0;
};

struct split_store
{
    std::unordered_map<uiid, f32> offsets;
};

// Animation state, keyed by (region-scoped or global) id. Unused keys are
// collected after 5 s, like other keyed UI state.
struct anim_store
{
    struct entry
    {
        f32 current = 0.0f;
        f32 start = 0.0f;
        f32 target = 0.0f;
        f32 velocity = 0.0f;
        f32 duration = 0.0f;
        easing curve = easing::EASE_OUT;
        f64 t0 = 0.0;
        f64 last_used = 0.0;
        i32 kind = 0; // 0 tween, 1 spring, 2 smooth
        bool initialized = false;
        bool settled = true;
    };
    std::unordered_map<uiid, entry> map;
};

// Scroll offsets + content extents, keyed by scroll view id.
struct scroll_store
{
    struct entry
    {
        f32 offset = 0.0f;
        f32 content_h = 0.0f;
        f32 viewport_h = 0.0f;
        f32 drag_anchor = 0.0f;
        f32 drag_offset = 0.0f;
        f64 last_used = 0.0;
        bool dragging = false;
    };
    std::unordered_map<uiid, entry> map;
};

inline scroll_store *ensure_scrolls(context *c)
{
    if (!c->scrolls) c->scrolls = new scroll_store();
    return static_cast<scroll_store *>(c->scrolls);
}

// ---- draw list (batching by clip + texture) ----
struct draw_list
{
    std::vector<vertex> verts;
    std::vector<i32> idx;
    texture_handle cur_tex = nullptr;
    bool tex_known = false;
    bool clip_active = false;
    rect clip{};
    bool clip_known = false;
    bool solid_uv_remap = false; // the in-flight draw_triangles call is solid
    // Reusable scratch for primitives that build local geometry before the
    // batch copy (draw_sector / draw_polygon): no per-call heap allocation
    // after warm-up.
    std::vector<vertex> scratch_v;
    std::vector<i32> scratch_i;
};

// Open-addressing map with inline values: power-of-two capacity, linear
// probing, no tombstones (wholesale clears or rebuilds only). Keys are uiid
// hashes (u64); the value type must be trivially relocatable enough for the
// growth re-insert. O(1) expected; two cache lines worst case.

namespace detail
{

inline void dl_flush(context *c)
{
    if (!c->dl) return;
    draw_list &dl = *c->dl;
    if (!dl.verts.empty() && c->device)
    {
        c->device->draw(dl.tex_known ? dl.cur_tex : nullptr, dl.verts.data(),
                        static_cast<i32>(dl.verts.size()), dl.idx.data(),
                        static_cast<i32>(dl.idx.size()));
    }
    dl.verts.clear();
    dl.idx.clear();
}

inline void dl_prepare(context *c, texture_handle tex, const rect *clip)
{
    if (!c->dl) return;
    draw_list &dl = *c->dl;

    bool want_active = clip != nullptr && clip->w > 0.0f && clip->h > 0.0f;
    rect want = want_active ? *clip : rect{};
    bool clip_changed = !dl.clip_known || dl.clip_active != want_active ||
                        (want_active && (dl.clip.x != want.x || dl.clip.y != want.y ||
                                         dl.clip.w != want.w || dl.clip.h != want.h));
    if (clip_changed)
    {
        dl_flush(c);
        if (c->device) c->device->set_clip(want_active ? &want : nullptr);
        dl.clip_active = want_active;
        dl.clip = want;
        dl.clip_known = true;
    }
    if (!dl.tex_known || dl.cur_tex != tex)
    {
        dl_flush(c);
        dl.cur_tex = tex;
        dl.tex_known = true;
    }
}

inline void dl_add(context *c, const vertex *verts, i32 vcount, const i32 *idx, i32 icount)
{
    if (!c->dl || vcount <= 0 || icount <= 0) return;
    draw_list &dl = *c->dl;
    i32 base = static_cast<i32>(dl.verts.size());
    dl.verts.insert(dl.verts.end(), verts, verts + vcount);
    if (dl.solid_uv_remap)
    {
        // Solid geometry rides in the glyph batch: remap the copied verts' UVs
        // to the atlas's reserved white texel at (0,0) (its center, so any
        // sampler/filtering returns exactly that texel). The copy already
        // happened, so this is arithmetic on the way in — no extra pass cost
        // beyond the uv writes.
        const f32 wx = 0.5f / static_cast<f32>(c->ts->atlas_w);
        const f32 wy = 0.5f / static_cast<f32>(c->ts->atlas_h);
        const usize first = dl.verts.size() - static_cast<usize>(vcount);
        for (usize i = first; i < dl.verts.size(); ++i)
        {
            dl.verts[i].u = wx;
            dl.verts[i].v = wy;
        }
    }
    dl.idx.reserve(dl.idx.size() + static_cast<usize>(icount));
    for (i32 i = 0; i < icount; ++i) dl.idx.push_back(idx[i] + base);
}

} // namespace detail

// ---- context ----
context *create_context(render_device *device, render_surface *surface)
{
    context *c = new context();
    c->device = device;
    c->surface = surface;
    c->frame_id_set = new flat_map<u8>();
    c->popups = new popup_entry[MAX_POPUPS];
    c->prev_popups = new popup_entry[MAX_POPUPS];
    c->popup_capacity = MAX_POPUPS;
    c->panels = new panel_entry[MAX_PANELS];
    c->prev_panels = new panel_entry[MAX_PANELS];
    c->panel_capacity = MAX_PANELS;
    c->panel_stack = new uiid[MAX_PANELS];
    c->panel_stack_capacity = MAX_PANELS;
    c->widget_id_set = new flat_map<rect>();
    c->dl = new draw_list();
    set_current_context(c);
    return c;
}

namespace detail
{

inline void free_blur_store(context *c, void *&p)
{
    if (!p) return;
    blur_store *bs = static_cast<blur_store *>(p);
    if (c->device)
    {
        if (bs->half) c->device->destroy_target(bs->half);
        if (bs->quarter) c->device->destroy_target(bs->quarter);
        if (bs->eighth) c->device->destroy_target(bs->eighth);
    }
    delete bs;
    p = nullptr;
}

inline window *ensure_primary(context *c)
{
    if (c->window_count > 0) return &c->windows[0];
    return add_window(c, nullptr, c->surface, rect{0, 0, 0, 0});
}

} // namespace detail

void destroy_context(context *c)
{
    if (!c) return;
    PUFFERUI_CHECK(VIOL_DESTROY_WHILE_OPEN, c->current_window == nullptr,
                   "destroy_context with an open window frame");
    if (current_context() == c) set_current_context(nullptr);
    if (c->ts)
    {
        for (text_store::atlas_page &page : c->ts->atlases)
            if (c->device && page.tex) c->device->destroy_texture(page.tex);
        delete c->ts;
    }
    if (c->edits) delete static_cast<edit_store *>(c->edits);
    if (c->anim) delete static_cast<anim_store *>(c->anim);
    if (c->scrolls) delete static_cast<scroll_store *>(c->scrolls);
    if (c->frame_id_set) delete static_cast<flat_map<u8> *>(c->frame_id_set);
    if (c->widget_id_set) delete static_cast<flat_map<rect> *>(c->widget_id_set);
    delete[] c->popups;
    delete[] c->prev_popups;
    delete[] c->panels;
    delete[] c->prev_panels;
    delete[] c->panel_stack;
    if (c->defers) delete static_cast<defer_store *>(c->defers);
    if (c->focus_store)
    {
        delete static_cast<focus_store *>(c->focus_store);
        c->focus_store = nullptr;
    }
    for (i32 i = 0; i < c->window_count; ++i)
    {
        window &w = c->windows[i];
        if (w.focus_store)
        {
            delete static_cast<focus_store *>(w.focus_store);
            w.focus_store = nullptr;
        }
        detail::free_blur_store(c, w.blur);
    }
    detail::free_blur_store(c, c->blur);
    if (c->splits) delete static_cast<split_store *>(c->splits);
    delete c->dl;
    delete[] c->windows;
    c->windows = nullptr;
    c->window_capacity = 0;
    delete c;
}

void set_device(context *c, render_device *device, render_surface *surface)
{
    c->device = device;
    c->surface = surface;
    for (i32 i = 0; i < c->window_count; ++i)
        if (!c->windows[i].surface) c->windows[i].surface = surface;
}
void set_theme(context *c, const theme &t)
{
    c->active_theme = t;
}

// ---- windows ----
namespace detail
{
// Grow the heap-owned window list, keeping interior pointers valid.
inline void grow_windows(context *c)
{
    const i32 new_cap = (c->window_capacity > 0) ? c->window_capacity * 2 : MAX_WINDOWS;
    window *fresh = new window[new_cap];
    for (i32 i = 0; i < c->window_count; ++i) fresh[i] = c->windows[i];
    if (c->current_window) c->current_window = fresh + (c->current_window - c->windows);
    if (c->focused_win) c->focused_win = fresh + (c->focused_win - c->windows);
    delete[] c->windows;
    c->windows = fresh;
    c->window_capacity = new_cap;
}
} // namespace detail

window *add_window(context *c, void *handle, render_surface *surface, rect client)
{
    if (c->window_count >= c->window_capacity) detail::grow_windows(c);
    if (c->window_count > 0 && c->device &&
        !has_cap(c->device->caps(), backend_caps::SHARED_DEVICE))
    {
        PUFFERUI_CHECK(VIOL_SHARED_DEVICE, false,
                       "a second window requires backend_caps::SHARED_DEVICE");
        return nullptr;
    }
    window &w = c->windows[c->window_count];
    w = window{};
    w.owner = c;
    w.handle = handle;
    w.surface = surface ? surface : c->surface;
    w.index = c->window_count;
    set_window_client(w, client);
    w.root = id_child(0x77696E646F770000ull, static_cast<uiid>(w.index) + 1);
    c->window_count += 1;
    if (!c->focused_win) focus_window(c, w);
    return &w;
}

void remove_window(context *c, window &w)
{
    if (c->current_window != nullptr)
    {
        PUFFERUI_CHECK(VIOL_REMOVE_WHILE_OPEN, false, "remove_window while a frame is open");
        return;
    }
    if (&w < c->windows || &w >= c->windows + c->window_count) return;
    const i32 i = static_cast<i32>(&w - c->windows);
    i32 focus_index = c->focused_win ? static_cast<i32>(c->focused_win - c->windows) : -1;
    if (w.focus_store)
    {
        delete static_cast<focus_store *>(w.focus_store);
        w.focus_store = nullptr;
    }
    detail::free_blur_store(c, w.blur);
    for (i32 j = i; j < c->window_count - 1; ++j) c->windows[j] = c->windows[j + 1];
    c->window_count -= 1;

    // Drop this window's last-frame popup/panel entries and remap later indices,
    // so input capture arrays stay in sync with the window list.
    for (i32 k = 0; k < c->popup_depth; ++k)
    {
        if (c->popups[k].win == i)
        {
            for (i32 m = k; m < c->popup_depth - 1; ++m) c->popups[m] = c->popups[m + 1];
            c->popup_depth -= 1;
            --k;
        }
        else if (c->popups[k].win > i)
        {
            c->popups[k].win -= 1;
        }
    }
    for (i32 k = 0; k < c->panel_depth; ++k)
    {
        if (c->panels[k].win == i)
        {
            for (i32 m = k; m < c->panel_depth - 1; ++m) c->panels[m] = c->panels[m + 1];
            c->panel_depth -= 1;
            --k;
        }
        else if (c->panels[k].win > i)
        {
            c->panels[k].win -= 1;
        }
    }

    if (focus_index == i)
        focus_index = (i < c->window_count) ? i : c->window_count - 1;
    else if (focus_index > i)
        focus_index -= 1;
    for (i32 j = 0; j < c->window_count; ++j)
    {
        c->windows[j].owner = c;
        c->windows[j].index = j;
    }
    c->focused_win = (focus_index >= 0 && c->window_count > 0) ? &c->windows[focus_index] : nullptr;
    for (i32 j = 0; j < c->window_count; ++j)
        c->windows[j].focused = (c->focused_win == &c->windows[j]);
}

window *window_at(context *c, void *handle)
{
    for (i32 i = 0; i < c->window_count; ++i)
        if (c->windows[i].handle == handle) return &c->windows[i];
    return nullptr;
}

window *first_window(context *c)
{
    return c->window_count > 0 ? &c->windows[0] : nullptr;
}

void set_window_client(window &w, rect client)
{
    w.client = client;
    w.area = rect::make(0.0f, 0.0f, client.w, client.h);
}

void focus_window(context *c, window &w)
{
    if (&w < c->windows || &w >= c->windows + c->window_count) return;
    c->focused_win = &w;
    for (i32 i = 0; i < c->window_count; ++i) c->windows[i].focused = (&c->windows[i] == &w);
}

window *focused_window(context *c)
{
    return c->focused_win;
}

rect desktop_rect(context *c)
{
    if (c->window_count == 0) return c->screen;
    rect u = c->windows[0].client;
    for (i32 i = 1; i < c->window_count; ++i)
    {
        const rect &r = c->windows[i].client;
        const f32 x0 = min2(u.x, r.x), y0 = min2(u.y, r.y);
        const f32 x1 = max2(u.right(), r.right()), y1 = max2(u.bottom(), r.bottom());
        u = rect::make(x0, y0, x1 - x0, y1 - y0);
    }
    return u;
}

void set_global_mouse(context *c, f32 x, f32 y)
{
    c->mouse_global_x = x;
    c->mouse_global_y = y;
}

namespace detail
{
// The virtual desktop in the current window's local coordinates (used to clamp
// floating panels so they can be dragged across windows).
inline rect desktop_local(context *c)
{
    if (!c->current_window) return c->screen;
    const rect &cl = c->current_window->client;
    return rect::make(c->desktop.x - cl.x, c->desktop.y - cl.y, c->desktop.w, c->desktop.h);
}
} // namespace detail

// Tab focus moves through the ring built during the previous frame; defined
// with the edit helpers below.
namespace detail
{
inline void tab_move(context *c, uiid id, bool back);
}

// ---- frames ----
void begin_frame(context *c, window &w, f64 now, f64 dt)
{
    if (c->current_window != nullptr)
    {
        PUFFERUI_CHECK(VIOL_FRAME_ALREADY_OPEN, false, "begin_frame: another window frame is open");
        return;
    }
    if (!(&w >= c->windows && &w < c->windows + c->window_count))
    {
        PUFFERUI_CHECK(VIOL_WINDOW_NOT_REGISTERED, false, "begin_frame: window is not registered");
        return;
    }

    c->frame += 1;
    c->now = now;
    c->dt = dt;
    c->current_window = &w;
    c->screen = w.area;
    c->desktop = desktop_rect(c);

    // The pointer is one; each window derives its local position from the shared
    // desktop position, so cross-window drags keep working while another window
    // receives the motion events.
    c->mouse_x = c->mouse_global_x - w.client.x;
    c->mouse_y = c->mouse_global_y - w.client.y;
    c->mouse_pressed = w.in.mouse_pressed;
    c->mouse_released = w.in.mouse_released;
    c->right_pressed = w.in.right_pressed;
    c->wheel_x = w.in.wheel_x;
    c->wheel_y = w.in.wheel_y;
    w.in.wheel_x = 0.0f;
    w.in.wheel_y = 0.0f;
    c->wheel_target = 0;
    // c->mouse_down is global: set by whichever window received the press.

    for (i32 i = 0; i < KEY_COUNT; ++i)
    {
        c->key_down[i] = w.in.key_down[i];
        c->key_pressed[i] = w.in.key_pressed[i];
        c->key_released[i] = w.in.key_released[i];
    }
    c->text_len = w.in.text_len;
    if (c->text_len > 0)
        std::memcpy(c->text_input, w.in.text_input, static_cast<usize>(c->text_len) + 1);
    else
        c->text_input[0] = '\0';
    c->ime_preedit_len = w.in.ime_preedit_len;
    if (c->ime_preedit_len > 0)
        std::memcpy(c->ime_preedit, w.in.ime_preedit, static_cast<usize>(c->ime_preedit_len) + 1);
    else
        c->ime_preedit[0] = '\0';
    c->ime_cursor = w.in.ime_cursor;
    c->shift_down = w.in.shift_down;
    c->ctrl_down = w.in.ctrl_down;

    // restore this window's persistent widget state
    c->active = w.active;
    c->focus = w.focus;
    c->focus_request = w.focus_request;
    c->hot = 0;
    c->dragging_panel = w.dragging_panel;
    c->drag_dx = w.drag_dx;
    c->drag_dy = w.drag_dy;
    c->last_click_id = w.last_click_id;
    c->last_click_time = w.last_click_time;

    if (c->widget_id_set) static_cast<flat_map<rect> *>(c->widget_id_set)->clear();
    if (c->frame_id_set) static_cast<flat_map<u8> *>(c->frame_id_set)->clear();
    c->press_claim = 0;
    c->press_claim_rect = rect{};
    c->current_region = 0;
    c->clip_depth = 0;
    c->parent_depth = 0;
    c->seq = 0;

    // popups/panels: this window's previous-frame entries become the input
    // capture set; other windows keep theirs for their own frames.
    {
        const i32 wi = w.index;
        c->prev_popup_depth = 0;
        i32 keep = 0;
        for (i32 i = 0; i < c->popup_depth; ++i)
        {
            if (c->popups[i].win == wi)
            {
                detail::ensure_popup_capacity(c, c->prev_popup_depth + 1);
                c->prev_popups[c->prev_popup_depth++] = c->popups[i];
            }
            else
            {
                c->popups[keep++] = c->popups[i];
            }
        }
        c->popup_depth = keep;

        c->prev_panel_depth = 0;
        keep = 0;
        for (i32 i = 0; i < c->panel_depth; ++i)
        {
            if (c->panels[i].win == wi)
            {
                detail::ensure_panel_capacity(c, c->prev_panel_depth + 1);
                c->prev_panels[c->prev_panel_depth++] = c->panels[i];
            }
            else
            {
                c->panels[keep++] = c->panels[i];
            }
        }
        c->panel_depth = keep;
    }
    c->popup_click_outside = false;
    c->popup_click_id = 0;
    c->in_popup = false;
    c->popup_layer_depth = 0;
    c->panel_stack_depth = 0;

    // per-window focus list and blur scratch migrate into the context for the
    // duration of the frame (they are per-window state).
    if (!w.focus_store) w.focus_store = new focus_store();
    c->focus_store = w.focus_store;
    {
        focus_store *fs = static_cast<focus_store *>(c->focus_store);
        fs->prev.swap(fs->current);
        fs->current.clear();
    }

    // Tab moves keyboard focus through the ring of widgets interacted last
    // frame — text fields and every other focusable widget alike. Nothing
    // focused: the first entry. Shift+Tab goes back. The select-all pending
    // flag applies only to text fields (consumed on their rising edge in
    // edit_buffer); it is cleared at end_frame when nothing claimed it.
    if (c->key_pressed[static_cast<i32>(key::TAB)])
    {
        // The key edge is left set for the frame (apps and tests may still
        // read it); only this block reacts to it, and the edge clears with
        // the next frame's input copy.
        c->focus_request_selects_all = true;
        detail::tab_move(c, c->focus, c->shift_down); // focus 0 -> ring[0]
    }
    if (c->focus_request != 0)
    {
        c->focus = c->focus_request;
        c->focus_request = 0;
    }
    if (!w.blur) w.blur = new blur_store();
    c->blur = w.blur;

    c->want_cursor = cursor::ARROW;

    // Animation state: collect keys unused for more than 5 s (same retention as
    // the other keyed UI state).
    if (c->anim)
    {
        anim_store *as = static_cast<anim_store *>(c->anim);
        for (auto it = as->map.begin(); it != as->map.end();)
        {
            if (now - it->second.last_used > 5.0)
                it = as->map.erase(it);
            else
                ++it;
        }
    }

    // Scroll state follows the same retention.
    if (c->scrolls)
    {
        scroll_store *ss = static_cast<scroll_store *>(c->scrolls);
        for (auto it = ss->map.begin(); it != ss->map.end();)
        {
            if (now - it->second.last_used > 5.0)
                it = ss->map.erase(it);
            else
                ++it;
        }
    }

    c->button_scope_depth = 0;

    if (!c->dl) c->dl = new draw_list();
    c->dl->verts.clear();
    c->dl->idx.clear();
    c->dl->clip_known = false;
    c->dl->clip_active = false;
    c->dl->tex_known = false;
    c->dl->cur_tex = nullptr;

    if (c->device)
    {
        c->device->begin_frame();
        if (w.surface) w.surface->make_current(*c->device);
        c->device->clear(c->active_theme.bg);
    }
}

void begin_frame(context *c, f64 now, f64 dt, rect screen)
{
    window *w = detail::ensure_primary(c);
    if (!w) return;
    if (!w->surface) w->surface = c->surface;
    // Single-window path: the primary window lives at the desktop origin.
    w->area = screen;
    w->client = rect::make(0.0f, 0.0f, screen.w, screen.h);
    begin_frame(c, *w, now, dt);
}

// Defined later in the implementation section; the deferred overlays below use
// them (popup surfaces must draw after the base UI).
namespace detail
{
inline i32 menu_items(ui &u, uiid id, rect area, const char *const *labels, i32 count, i32 &hot,
                      const theme &t, bool fresh_open);
inline void popup_retract(context *c, uiid id);
} // namespace detail

void end_frame(context *c)
{
    window *w = c->current_window;
    PUFFERUI_CHECK(VIOL_END_WITHOUT_BEGIN, w != nullptr, "end_frame without begin_frame");
    if (!w) return;
    PUFFERUI_CHECK(VIOL_CLIP_UNBALANCED, c->clip_depth == 0, "unbalanced region clip push/pop");
    PUFFERUI_CHECK(VIOL_SCOPE_UNBALANCED, c->button_scope_depth == 0,
                   "unbalanced button style scope");

    // Text-drag overlay: while a selection is being dragged, the lifted text
    // follows the pointer. Releasing outside any field cancels the drag.
    if (c->text_drag.active)
    {
        if (!c->mouse_down)
        {
            c->text_drag = text_drag_payload{};
        }
        else if (!c->text_drag.text.empty())
        {
            ui u(c);
            const f32 gw = u.text_width(c->text_drag.text) + 12.0f;
            const f32 gh = u.line_height() + 8.0f;
            const rect ghost{c->mouse_x + 12.0f, c->mouse_y + 10.0f, gw, gh};
            u.draw_rounded_rect(rect::make(ghost.x + 2.0f, ghost.y + 2.0f, ghost.w, ghost.h),
                                color{0, 0, 0, 90}, 4.0f);
            u.draw_rounded_rect(ghost, color{48, 56, 68, 240}, 4.0f);
            u.text(ghost, c->text_drag.text, c->active_theme.text, ALIGN_CENTER);
        }
    }

    // Deferred overlays: the open combo dropdown / context menu and the visible
    // tooltip draw here, after every base widget, so nothing painted later in
    // the frame can cover them (immediate mode paints in call order; popup
    // surfaces must go last, like the text-drag ghost above). The pick is
    // resolved here too: mouse edges are still live, and the entry push/retract
    // behaves exactly as an in-frame popup would.
    if (c->defer_menu_id != 0 && c->defer_count > 0)
    {
        ui u(c);
        popup_scope p = u.popup(c->defer_menu_id, c->defer_menu, POPUP_CLOSE_ON_CLICK_OUTSIDE);
        if (p.close_requested)
        {
            detail::popup_retract(c, c->defer_menu_id); // closing: no ghost entry
        }
        else
        {
            i32 hot = c->combo_hot; // the dropdown and context menus share it
            const i32 got = detail::menu_items(
                u, c->defer_menu_id, c->defer_menu, detail::ensure_defers(c)->ptrs.data(),
                c->defer_count, hot, c->active_theme, c->defer_fresh);
            c->combo_hot = hot;
            if (got >= 0)
            {
                c->defer_result_id = c->defer_menu_id; // the next call reports it
                c->defer_result_pick = got;
                detail::popup_retract(c, c->defer_menu_id); // same-frame close
            }
        }
        c->defer_menu_id = 0;
        c->defer_count = 0;
    }
    if (c->defer_tip.w > 0.0f)
    {
        ui u(c);
        u.draw_rounded_rect(c->defer_tip, color{18, 22, 28, 245}, 6.0f);
        u.text(c->defer_tip, c->defer_tip_text, c->active_theme.text, ALIGN_CENTER);
        c->defer_tip = rect{};
    }

    detail::dl_flush(c);
    if (c->device)
    {
        c->device->set_clip(nullptr);
        // Apply the cursor only from the window under the pointer: with several
        // windows on one device, every window's frame would otherwise overwrite
        // the cursor of the window actually being hovered (the last one wins).
        const bool pointer_over =
            c->window_count <= 1 || w->client.contains(c->mouse_global_x, c->mouse_global_y);
        if (pointer_over) c->device->set_cursor(c->want_cursor);
        c->device->end_frame();
    }
    if (w->surface) w->surface->present();

    if (!c->mouse_down) c->active = 0;

    // Save this window's persistent state; clear the per-frame edges.
    w->active = c->active;
    w->focus = c->focus;
    w->focus_request = c->focus_request;
    w->dragging_panel = c->dragging_panel;
    w->drag_dx = c->drag_dx;
    w->drag_dy = c->drag_dy;
    w->last_click_id = c->last_click_id;
    w->last_click_time = c->last_click_time;
    w->in.mouse_pressed = false;
    w->in.mouse_released = false;
    w->in.right_pressed = false;
    for (i32 i = 0; i < KEY_COUNT; ++i)
    {
        w->in.key_down[i] = c->key_down[i];
        w->in.key_pressed[i] = false;
        w->in.key_released[i] = false;
    }
    w->in.text_len = 0;
    w->in.shift_down = c->shift_down;
    w->in.ctrl_down = c->ctrl_down;

    // Detach the per-window stores again.
    w->focus_store = c->focus_store;
    c->focus_store = nullptr;
    w->blur = c->blur;
    c->blur = nullptr;
    // Tab select-all is a per-frame pending flag: if it was not consumed by a
    // text field this frame (the Tab target was not a field), drop it so a
    // later field focus never selects all by accident.
    c->focus_request_selects_all = false;

    if (c->dl)
    {
        c->dl->clip_known = false;
        c->dl->clip_active = false;
        c->dl->tex_known = false;
        c->dl->cur_tex = nullptr;
    }
    c->mouse_pressed = false;
    c->mouse_released = false;
    c->right_pressed = false;
    for (i32 i = 0; i < KEY_COUNT; ++i)
    {
        c->key_pressed[i] = false;
        c->key_released[i] = false;
    }
    c->text_len = 0;

    // Safety net: if no dock space consumed the drop this frame, or the release
    // landed outside every window, don't leave a stale drag hanging.
    if (!c->mouse_down && c->dock_panel != 0)
    {
        c->dock_panel = 0;
        c->dock_source = nullptr;
        c->dock_panel_name = nullptr;
        c->dock_dragging = false;
    }

    c->current_region = 0;
    c->current_window = nullptr;
}
bool needs_redraw(const context *c)
{
    if (!c) return true;
    if (c->text_drag.active) return true;
    if (c->anim)
    {
        const anim_store *as = static_cast<const anim_store *>(c->anim);
        for (const auto &kv : as->map)
            if (kv.second.initialized && !kv.second.settled) return true;
    }
    if (c->edits)
    {
        const edit_store *es = static_cast<const edit_store *>(c->edits);
        for (const auto &kv : es->map)
            if (kv.second.focused) return true;
    }
    return false;
}

// ---- input ----
namespace detail
{
inline bool window_is_current(window &w)
{
    return w.owner != nullptr && w.owner->current_window == &w;
}
} // namespace detail

void mouse_move(window &w, f32 x, f32 y)
{
    context *c = w.owner;
    if (!c) return;
    c->mouse_global_x = w.client.x + x;
    c->mouse_global_y = w.client.y + y;
    if (detail::window_is_current(w))
    {
        c->mouse_x = x;
        c->mouse_y = y;
    }
}

void mouse_button(window &w, bool down)
{
    context *c = w.owner;
    if (!c) return;
    const bool current = detail::window_is_current(w);
    if (down && !c->mouse_down)
    {
        if (current)
            c->mouse_pressed = true;
        else
            w.in.mouse_pressed = true;
    }
    if (!down && c->mouse_down)
    {
        if (current)
            c->mouse_released = true;
        else
            w.in.mouse_released = true;
    }
    c->mouse_down = down;
}

void mouse_wheel(window &w, f32 dx, f32 dy)
{
    context *c = w.owner;
    if (!c) return;
    if (detail::window_is_current(w))
    {
        c->wheel_x += dx;
        c->wheel_y += dy;
    }
    else
    {
        w.in.wheel_x += dx;
        w.in.wheel_y += dy;
    }
}

void mouse_wheel(context *c, f32 dx, f32 dy)
{
    if (!c) return;
    window *w = c->current_window ? c->current_window : detail::ensure_primary(c);
    if (w) mouse_wheel(*w, dx, dy);
}

void mouse_button(window &w, pointer_button b, bool down)
{
    context *c = w.owner;
    if (!c) return;
    if (b == pointer_button::LEFT)
    {
        mouse_button(w, down);
        return;
    }
    if (b != pointer_button::RIGHT) return; // middle button is not wired yet
    const bool current = detail::window_is_current(w);
    if (down && !c->right_down)
    {
        if (current)
            c->right_pressed = true;
        else
            w.in.right_pressed = true;
    }
    c->right_down = down;
}

void key_event(window &w, key k, bool down)
{
    context *c = w.owner;
    const i32 i = static_cast<i32>(k);
    if (i < 0 || i >= KEY_COUNT) return;
    if (c && detail::window_is_current(w))
    {
        if (down && !c->key_down[i]) c->key_pressed[i] = true;
        if (!down && c->key_down[i]) c->key_released[i] = true;
        c->key_down[i] = down;
        return;
    }
    if (down && !w.in.key_down[i]) w.in.key_pressed[i] = true;
    if (!down && w.in.key_down[i]) w.in.key_released[i] = true;
    w.in.key_down[i] = down;
}

void text_input_event(window &w, const char *utf8)
{
    context *c = w.owner;
    char *buf = nullptr;
    i32 *len = nullptr;
    if (c && detail::window_is_current(w))
    {
        c->ime_preedit_len = 0;
        c->ime_preedit[0] = '\0'; // committing text ends any composition
        buf = c->text_input;
        len = &c->text_len;
    }
    else
    {
        w.in.ime_preedit_len = 0;
        w.in.ime_preedit[0] = '\0';
        buf = w.in.text_input;
        len = &w.in.text_len;
    }
    if (!utf8) return;
    // Never silently truncated: an event that will not fit reports the
    // overflow once, then the part that fits is kept (graceful, but loud).
    usize in_len = 0;
    while (utf8[in_len] != '\0') ++in_len;
    const i32 room = static_cast<i32>(sizeof(w.in.text_input)) - 1 - *len;
    if (static_cast<i32>(in_len) > room)
    {
        PUFFERUI_CHECK(
            VIOL_INPUT_OVERFLOW, false,
            "text input did not fit the per-frame input buffer (IME commit or paste too long)");
    }
    for (const char *p = utf8; *p && *len < static_cast<i32>(sizeof(w.in.text_input)) - 1; ++p)
        buf[(*len)++] = *p;
    buf[*len] = '\0';
}

void ime_event(window &w, const char *preedit, i32 cursor)
{
    context *c = w.owner;
    char *buf = nullptr;
    i32 *len = nullptr;
    if (c && detail::window_is_current(w))
    {
        c->ime_preedit_len = 0;
        buf = c->ime_preedit;
        len = &c->ime_preedit_len;
        c->ime_cursor = cursor;
    }
    else
    {
        w.in.ime_preedit_len = 0;
        buf = w.in.ime_preedit;
        len = &w.in.ime_preedit_len;
        w.in.ime_cursor = cursor;
    }
    if (preedit)
    {
        for (const char *p = preedit; *p && *len < static_cast<i32>(sizeof(w.in.ime_preedit)) - 1;
             ++p)
            buf[(*len)++] = *p;
    }
    buf[*len] = '\0';
}

void mods_event(window &w, bool shift, bool ctrl)
{
    context *c = w.owner;
    if (c && detail::window_is_current(w))
    {
        c->shift_down = shift;
        c->ctrl_down = ctrl;
        return;
    }
    w.in.shift_down = shift;
    w.in.ctrl_down = ctrl;
}

void mouse_move(context *c, f32 x, f32 y)
{
    window *w = detail::ensure_primary(c);
    if (w) mouse_move(*w, x, y);
}

void mouse_button(context *c, bool down)
{
    window *w = detail::ensure_primary(c);
    if (w) mouse_button(*w, down);
}

void mouse_button(context *c, pointer_button b, bool down)
{
    window *w = detail::ensure_primary(c);
    if (w) mouse_button(*w, b, down);
}

void key_event(context *c, key k, bool down)
{
    window *w = c->focused_win ? c->focused_win : detail::ensure_primary(c);
    if (w) key_event(*w, k, down);
}

void text_input_event(context *c, const char *utf8)
{
    window *w = c->focused_win ? c->focused_win : detail::ensure_primary(c);
    if (w) text_input_event(*w, utf8);
}

void ime_event(context *c, const char *preedit, i32 cursor)
{
    window *w = c->focused_win ? c->focused_win : detail::ensure_primary(c);
    if (w) ime_event(*w, preedit, cursor);
}

void mods_event(context *c, bool shift, bool ctrl)
{
    window *w = c->focused_win ? c->focused_win : detail::ensure_primary(c);
    if (w) mods_event(*w, shift, ctrl);
}

void set_clipboard(context *c, clipboard *clip)
{
    c->clip = clip;
}

i32 violation_count(context *c)
{
    return c->violations;
}

const char *violation_last(context *c)
{
    return c ? c->vlast : "";
}

violation_code violation_last_code(context *c)
{
    return c ? c->vlast_code : VIOL_NONE;
}

void set_break_on_violation(context *c, bool enabled)
{
    if (c) c->break_on_violation = enabled;
}

const char *violation_code_name(violation_code code)
{
    static const char *const names[] = {
        "none",
        "invalid_slice",
        "invalid_rect",
        "invalid_area",
        "dup_region",
        "window_limit",
        "shared_device",
        "remove_while_open",
        "destroy_while_open",
        "frame_already_open",
        "window_not_registered",
        "end_without_begin",
        "clip_unbalanced",
        "scope_unbalanced",
        "popup_overflow",
        "combo_empty",
        "layout_clamped",
        "no_font",
        "input_overflow",
        "dup_widget_id",
        "frame_ids_overflow",
    };
    return (code >= 0 && code < VIOL_COUNT) ? names[code] : "unknown";
}

// ---- animation ----
namespace detail
{

inline anim_store *ensure_anim(context *c)
{
    if (!c->anim) c->anim = new anim_store();
    return static_cast<anim_store *>(c->anim);
}

inline uiid anim_scoped_id(context *c, uiid key, bool global)
{
    return global ? id_child(0x676C6F62616C0000ull, key) : id_child(c->current_region, key);
}

inline f32 tween_value(context *c, anim_store::entry &e, f32 target, const tween &spec)
{
    if (!e.initialized || e.kind != 0 || e.target != target)
    {
        if (e.initialized && e.kind == 0 && e.duration > 0.0f)
        {
            // Re-evaluate the old tween at `now` before retargeting.
            const f32 told = clampf(static_cast<f32>((c->now - e.t0) / e.duration), 0.0f, 1.0f);
            e.current = e.start + (e.target - e.start) * ease(e.curve, told);
        }
        e.kind = 0;
        e.start = e.current;
        e.target = target;
        e.t0 = c->now;
        e.duration = spec.duration;
        e.curve = spec.curve;
        e.initialized = true;
    }
    if (c->reduced_motion)
    {
        e.current = target;
        e.settled = true;
        return e.current;
    }
    const f32 t = spec.duration > 0.0f
                      ? clampf(static_cast<f32>((c->now - e.t0) / spec.duration), 0.0f, 1.0f)
                      : 1.0f;
    e.current = e.start + (e.target - e.start) * ease(spec.curve, t);
    e.settled = (t >= 1.0f) || (e.start == e.target);
    return e.current;
}

inline f32 spring_value(context *c, anim_store::entry &e, f32 target, const spring &spec)
{
    if (!e.initialized || e.kind != 1)
    {
        e.kind = 1;
        e.velocity = 0.0f;
        e.initialized = true;
    }
    e.target = target; // retargets keep current + velocity
    if (c->reduced_motion)
    {
        e.current = target;
        e.velocity = 0.0f;
        e.settled = true;
        return e.current;
    }
    const f32 dt = static_cast<f32>(c->dt);
    const f32 m = max2(spec.mass, 0.001f);
    const f32 d = 2.0f * spec.damping_ratio * std::sqrt(spec.stiffness * m);
    const f32 accel = (spec.stiffness * (target - e.current) - d * e.velocity) / m;
    e.velocity += accel * dt;
    e.current += e.velocity * dt;
    if (std::fabs(target - e.current) < 0.001f && std::fabs(e.velocity) < 0.001f)
    {
        e.current = target;
        e.velocity = 0.0f;
        e.settled = true;
    }
    else
    {
        e.settled = false;
    }
    return e.current;
}

inline f32 smooth_value(context *c, anim_store::entry &e, f32 target, f32 half_life)
{
    if (!e.initialized || e.kind != 2)
    {
        e.kind = 2;
        e.current = target; // start at the target: no jump
        e.initialized = true;
    }
    e.target = target;
    if (c->reduced_motion)
    {
        e.current = target;
        e.settled = true;
        return e.current;
    }
    const f32 dt = static_cast<f32>(c->dt);
    const f32 factor = (half_life > 0.0f) ? std::pow(0.5f, dt / half_life) : 0.0f;
    e.current = target + (e.current - target) * factor;
    e.settled = std::fabs(target - e.current) < 0.001f;
    if (e.settled) e.current = target;
    return e.current;
}

} // namespace detail

f32 ui::animate(uiid key, f32 target, const tween &spec)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, false)];
    e.last_used = ctx->now;
    return detail::tween_value(ctx, e, target, spec);
}

f32 ui::animate(uiid key, f32 target, const spring &spec)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, false)];
    e.last_used = ctx->now;
    return detail::spring_value(ctx, e, target, spec);
}

f32 ui::animate_global(uiid key, f32 target, const tween &spec)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, true)];
    e.last_used = ctx->now;
    return detail::tween_value(ctx, e, target, spec);
}

f32 ui::animate_global(uiid key, f32 target, const spring &spec)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, true)];
    e.last_used = ctx->now;
    return detail::spring_value(ctx, e, target, spec);
}

f32 ui::smooth(uiid key, f32 target, f32 half_life)
{
    anim_store *st = detail::ensure_anim(ctx);
    anim_store::entry &e = st->map[detail::anim_scoped_id(ctx, key, false)];
    e.last_used = ctx->now;
    return detail::smooth_value(ctx, e, target, half_life);
}

f32 ui::appear(uiid key, f32 duration, easing curve)
{
    return animate(key, 1.0f, tween{duration, curve});
}

color ui::animate_color(uiid key, color target, const tween &spec)
{
    color out;
    out.r = static_cast<u8>(
        clampf(animate(id_child(key, 1), static_cast<f32>(target.r), spec) + 0.5f, 0.0f, 255.0f));
    out.g = static_cast<u8>(
        clampf(animate(id_child(key, 2), static_cast<f32>(target.g), spec) + 0.5f, 0.0f, 255.0f));
    out.b = static_cast<u8>(
        clampf(animate(id_child(key, 3), static_cast<f32>(target.b), spec) + 0.5f, 0.0f, 255.0f));
    out.a = static_cast<u8>(
        clampf(animate(id_child(key, 4), static_cast<f32>(target.a), spec) + 0.5f, 0.0f, 255.0f));
    return out;
}

bool ui::animations_active() const
{
    if (!ctx->anim) return false;
    const anim_store *st = static_cast<const anim_store *>(ctx->anim);
    for (const auto &kv : st->map)
        if (!kv.second.settled) return true;
    return false;
}

// ---- region ----
region::region(ui &u, uiid key, rect area, bool push_clip)
{
    u_ = &u;
    context *c = u.ctx;

    parent_ = c->current_region;
    id_ = id_child(parent_, key);
    area_ = area;
    content_ = area;

    rect parent_clip =
        (c->clip_depth > 0) ? c->clip_stack[c->clip_depth - 1] : rect{0.0f, 0.0f, 1.0e9f, 1.0e9f};
    clip_ = rect::intersect(area, parent_clip);

    PUFFERUI_CHECK(VIOL_INVALID_AREA, area.is_valid(), "region area is invalid");

    c->current_region = id_;

    if (push_clip && c->clip_depth < MAX_CLIP_DEPTH)
    {
        c->clip_stack[c->clip_depth++] = clip_;
        pushed_clip_ = true;
    }

    // Duplicate region ids are an O(1) set membership test (per-frame
    // flat_map, cleared in begin_frame) — complete at any frame scale, no
    // cap to silently outgrow.
    if (flat_map<u8> *ids = static_cast<flat_map<u8> *>(c->frame_id_set))
    {
        if (ids->find(id_))
        {
            PUFFERUI_CHECK(VIOL_DUP_REGION_ID, false, "duplicate region id among siblings");
        }
        else
        {
            ids->at(id_) = 1;
        }
    }
}

region::~region()
{
    if (!u_ || !u_->ctx) return;
    context *c = u_->ctx;
    c->current_region = parent_;
    if (pushed_clip_ && c->clip_depth > 0) --c->clip_depth;
}

region ui::region(rect area, uiid key, bool push_clip)
{
    return pui::region(*this, key, area, push_clip);
}

id_scope::id_scope(ui &u, uiid key)
{
    u_ = &u;
    context *c = u.ctx;
    parent_ = c->current_region;
    c->current_region = id_child(parent_, key);
    // Two scopes with the same key in one frame is the same bug the region
    // check catches — same set, same report.
    if (flat_map<u8> *ids = static_cast<flat_map<u8> *>(c->frame_id_set))
    {
        if (ids->find(c->current_region))
            PUFFERUI_CHECK(VIOL_DUP_REGION_ID, false, "duplicate region id among siblings");
        else
            ids->at(c->current_region) = 1;
    }
}

id_scope::~id_scope()
{
    if (!u_ || !u_->ctx) return;
    u_->ctx->current_region = parent_;
}

rect region::corner(i32 n, f32 w, f32 h, f32 margin) const
{
    n = (n % 4 + 4) % 4; // tolerate any winding; -1 -> BL, 4 -> TL, ...
    w = clampf(w, 0.0f, max2(0.0f, content_.w - margin * 2.0f));
    h = clampf(h, 0.0f, max2(0.0f, content_.h - margin * 2.0f));
    rect r = content_;
    switch (n)
    {
    case 0: // top-left
        r = rect::make(content_.x + margin, content_.y + margin, w, h);
        break;
    case 1: // top-right
        r = rect::make(content_.right() - margin - w, content_.y + margin, w, h);
        break;
    case 2: // bottom-right
        r = rect::make(content_.right() - margin - w, content_.bottom() - margin - h, w, h);
        break;
    default: // bottom-left
        r = rect::make(content_.x + margin, content_.bottom() - margin - h, w, h);
        break;
    }
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, r.is_valid(), "region corner produced an invalid rect");
    return r;
}

// ---- interaction ----
interaction ui::interact(uiid id, rect area, bool enabled, bool focusable)
{
    context *c = ctx;
    interaction in;
    in.disabled = !enabled;

    if (!enabled) return in;

    // A popup opened last frame captures all input. Base UI is blocked outright;
    // only popup content inside the top popup's rect may interact. "Click
    // outside" is recorded only for a click truly outside the popup.
    if (c->prev_popup_depth > 0)
    {
        const popup_entry &top = c->prev_popups[c->prev_popup_depth - 1];
        const bool inside = top.area.contains(c->mouse_x, c->mouse_y);
        if (!inside)
        {
            if (c->mouse_pressed)
            {
                c->popup_click_outside = true;
                c->popup_click_id = top.id;
            }
            in.blocked = true;
            return in;
        }
        if (!c->in_popup)
        {
            in.blocked = true;
            return in; // base UI under the popup: blocked, no close
        }
    }

    // A panel opened last frame blocks base UI under it. Popup content above it
    // was already allowed by the popup check, so only base UI reaches here.
    if (c->prev_panel_depth > 0)
    {
        const panel_entry *under = nullptr;
        for (i32 i = c->prev_panel_depth - 1; i >= 0; --i)
        {
            if (c->prev_panels[i].area.contains(c->mouse_x, c->mouse_y))
            {
                under = &c->prev_panels[i];
                break;
            }
        }
        if (under)
        {
            const bool inside =
                c->panel_stack_depth > 0 && c->panel_stack[c->panel_stack_depth - 1] == under->id;
            if (!inside)
            {
                in.blocked = true;
                return in; // blocked by the floating panel
            }
        }
    }

    rect clip =
        (c->clip_depth > 0) ? c->clip_stack[c->clip_depth - 1] : rect{0.0f, 0.0f, 1.0e9f, 1.0e9f};
    rect visible = rect::intersect(area, clip);
    bool over = visible.w > 0.0f && visible.h > 0.0f && visible.contains(c->mouse_x, c->mouse_y);
    in.hovered = over;

    if (over && c->active == 0) c->hot = id;

    if (over && c->mouse_pressed)
    {
        // Topmost wins: the last submitted enabled rect containing the press
        // position owns it. A press claimed earlier this frame by an
        // overlapping rect is superseded here (that call already reported
        // `activated`; the release lands its click on the topmost, so the
        // lower rect never acts).
        const rect claim_hit = rect::intersect(c->press_claim_rect, area);
        const bool supersedes = (c->active != 0 && c->active == c->press_claim &&
                                 claim_hit.w > 0.0f && claim_hit.h > 0.0f);
        if (c->active == 0 || supersedes)
        {
            c->active = id;
            in.activated = true;
            c->press_claim = id;
            c->press_claim_rect = area;
        }
    }

    // Right-press edge over the widget (context menus open on right-press).
    if (over && c->right_pressed) in.right_clicked = true;

    if (c->active == id)
    {
        in.captured = true;
        if (c->mouse_down) in.held = true;
        if (c->mouse_released)
        {
            if (over) in.clicked = true;
            c->active = 0;
        }
    }

    if (in.clicked)
    {
        if (c->last_click_id == id && (c->now - c->last_click_time) < 0.35)
            in.double_clicked = true;
        c->last_click_id = id;
        c->last_click_time = c->now;
    }

    in.pressed = (c->active == id) && c->mouse_down;
    in.focused = (c->focus == id);
    // Every enabled interact joins the keyboard focus ring, in submission
    // order — the same order widgets paint. Disabled widgets yield early and
    // never take focus; blocked widgets return before this point.
    if (enabled && focusable)
    {
        if (!c->focus_store) c->focus_store = new focus_store();
        focus_store *fs = static_cast<focus_store *>(c->focus_store);
        if (fs->current.empty() || fs->current.back() != id) fs->current.push_back(id);
#if !defined(NDEBUG)
        // Debug duplicate-widget detection: the same id interacted twice in
        // one frame silently shares press/focus/animation state (only region
        // ids are dup-checked). Identical rects are tolerated (a widget
        // re-submitting itself in place); different rects are the bug.
        if (flat_map<rect> *seen = static_cast<flat_map<rect> *>(c->widget_id_set))
        {
            if (rect *first = seen->find(id))
            {
                if (!(first->x == area.x && first->y == area.y))
                    PUFFERUI_CHECK(VIOL_DUP_WIDGET_ID, false,
                                   "duplicate widget id among siblings (the same id interacted at "
                                   "a different rect)");
            }
            else
            {
                seen->at(id) = area;
            }
        }
#endif
    }
    return in;
}

// ---- cursors ----
namespace detail
{
// Non-fatal truncation note (see set_report_layout_overflow). Called only when
// the requested slice does not fit the remaining space.
inline void report_slice_clamp(context *c, const char *what, f32 requested, f32 granted)
{
    if (!c || !c->report_layout_overflow || requested <= granted + 0.001f) return;
    c->overflow_count += 1;
    char message[160];
    std::snprintf(message, sizeof(message),
                  "%s slice clamped: requested %.0f px > remaining %.0f px", what,
                  static_cast<double>(requested), static_cast<double>(granted));
    report_violation(VIOL_LAYOUT_CLAMPED, "clamped", message, "", 0, false);
}
} // namespace detail

rect column::next(f32 height)
{
    f32 h = height > 0.0f ? height : 28.0f;
    const f32 avail = max2(0.0f, bounds_.h);
    detail::report_slice_clamp("column", h, avail);
    if (h > avail) h = avail; // clamp: a column never overflows
    rect r{bounds_.x, bounds_.y, bounds_.w, h};
    bounds_.y += h + gap_;
    bounds_.h = max2(0.0f, bounds_.h - (h + gap_));
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, r.is_valid(), "column slice is invalid");
    return r;
}

rect row::next(f32 width)
{
    f32 w = width > 0.0f ? width : 80.0f;
    const f32 avail = max2(0.0f, bounds_.w);
    detail::report_slice_clamp("row", w, avail);
    if (w > avail) w = avail; // clamp: a row never overflows
    rect r{bounds_.x, bounds_.y, w, bounds_.h};
    bounds_.x += w + gap_;
    bounds_.w = max2(0.0f, bounds_.w - (w + gap_));
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, r.is_valid(), "row slice is invalid");
    return r;
}

void column::space(f32 amount)
{
    bounds_.y += amount;
    bounds_.h = max2(0.0f, bounds_.h - amount);
}
void row::space(f32 amount)
{
    bounds_.x += amount;
    bounds_.w = max2(0.0f, bounds_.w - amount);
}

// ---- tracks & grids --------------------------------------------------------
i32 resolve_track_sizes(f32 available, std::span<const track_size> tracks, f32 *out, i32 max_out)
{
    i32 limit = max_out;
    if (limit < 0) limit = 0;
    const i32 track_count = static_cast<i32>(tracks.size());
    const i32 n = (track_count < limit) ? track_count : limit;
    if (n <= 0) return 0;

    f32 assigned[MAX_TRACKS]{};
    f32 consumed = 0.0f;

    for (i32 i = 0; i < n; ++i)
    {
        const track_size &t = tracks[static_cast<usize>(i)];
        f32 v = 0.0f;
        switch (t.type)
        {
        case track_type::FIXED:
            v = t.value;
            break;
        case track_type::RATIO:
            v = available * t.value;
            break;
        case track_type::FIT_CONTENT:
            // Intrinsic size is unknown to a pure-geometry solver, so the track
            // falls back to its `min_size` (the caller's content estimate).
            v = t.min_size;
            break;
        case track_type::FLEX:
            continue;
        }
        v = clampf(v, t.min_size, t.max_size);
        v = clampf(v, 0.0f, max2(0.0f, available - consumed));
        assigned[i] = v;
        consumed += v;
    }

    f32 pool = max2(0.0f, available - consumed);
    bool open[MAX_TRACKS]{};
    f32 open_weight = 0.0f;
    for (i32 i = 0; i < n; ++i)
    {
        if (tracks[static_cast<usize>(i)].type == track_type::FLEX)
        {
            open[i] = true;
            open_weight += max2(0.001f, tracks[static_cast<usize>(i)].value);
        }
    }

    bool changed = open_weight > 0.0f;
    while (changed)
    {
        changed = false;
        for (i32 i = 0; i < n; ++i)
        {
            if (!open[i]) continue;
            const track_size &t = tracks[static_cast<usize>(i)];
            const f32 w = max2(0.001f, t.value);
            const f32 share = (open_weight > 0.0f) ? (pool * (w / open_weight)) : 0.0f;
            if (share < t.min_size)
            {
                assigned[i] = t.min_size;
                pool = max2(0.0f, pool - t.min_size);
                open_weight = max2(0.0f, open_weight - w);
                open[i] = false;
                changed = true;
            }
            else if (share > t.max_size)
            {
                assigned[i] = t.max_size;
                pool = max2(0.0f, pool - t.max_size);
                open_weight = max2(0.0f, open_weight - w);
                open[i] = false;
                changed = true;
            }
        }
    }

    if (open_weight > 0.0f)
    {
        for (i32 i = 0; i < n; ++i)
        {
            if (!open[i]) continue;
            const track_size &t = tracks[static_cast<usize>(i)];
            assigned[i] =
                clampf((pool / open_weight) * max2(0.001f, t.value), t.min_size, t.max_size);
        }
    }

    for (i32 i = 0; i < n; ++i) out[i] = assigned[i];
    return n;
}

track_row::track_row(rect area, std::span<const track_size> tracks, f32 gap)
    : bounds_(area), gap_(max2(0.0f, gap))
{
    const f32 total_gap = gap_ * static_cast<f32>(tracks.size() > 1 ? tracks.size() - 1 : 0);
    count_ = resolve_track_sizes(max2(0.0f, area.w - total_gap), tracks, sizes_, MAX_TRACKS);
}

rect track_row::next(f32 height)
{
    if (index_ >= count_) return {};
    const f32 w = clampf(sizes_[index_], 0.0f, max2(0.0f, bounds_.w));
    const f32 h = (height > 0.0f) ? min2(height, bounds_.h) : bounds_.h;
    rect r{bounds_.x, bounds_.y, w, h};
    bounds_.x += w + gap_;
    bounds_.w = max2(0.0f, bounds_.w - (w + gap_));
    ++index_;
    return r;
}

rect track_row::remaining() const
{
    f32 w = 0.0f;
    for (i32 i = index_; i < count_; ++i) w += sizes_[i] + (i > index_ ? gap_ : 0.0f);
    return rect{bounds_.x, bounds_.y, min2(w, bounds_.w), bounds_.h};
}

track_column::track_column(rect area, std::span<const track_size> tracks, f32 gap)
    : bounds_(area), gap_(max2(0.0f, gap))
{
    const f32 total_gap = gap_ * static_cast<f32>(tracks.size() > 1 ? tracks.size() - 1 : 0);
    count_ = resolve_track_sizes(max2(0.0f, area.h - total_gap), tracks, sizes_, MAX_TRACKS);
}

rect track_column::next(f32 width)
{
    if (index_ >= count_) return {};
    const f32 h = clampf(sizes_[index_], 0.0f, max2(0.0f, bounds_.h));
    const f32 w = (width > 0.0f) ? min2(width, bounds_.w) : bounds_.w;
    rect r{bounds_.x, bounds_.y, w, h};
    bounds_.y += h + gap_;
    bounds_.h = max2(0.0f, bounds_.h - (h + gap_));
    ++index_;
    return r;
}

rect track_column::remaining() const
{
    f32 h = 0.0f;
    for (i32 i = index_; i < count_; ++i) h += sizes_[i] + (i > index_ ? gap_ : 0.0f);
    return rect{bounds_.x, bounds_.y, bounds_.w, min2(h, bounds_.h)};
}

grid_cursor auto_fit_grid(rect container, i32 total_items, f32 min_item_w, f32 item_h, f32 gap)
{
    grid_cursor g;
    g.bounds_ = container;
    g.gap_ = gap;
    if (total_items <= 0 || container.w <= 0.0f) return g;
    if (!(min_item_w > 0.0f)) min_item_w = 1.0f; // guards divide-by-zero / NaN
    if (!(gap >= 0.0f) || gap >= min_item_w) gap = 0.0f;
    g.gap_ = gap;

    i32 cols = static_cast<i32>((container.w + gap) / (min_item_w + gap));
    if (cols < 1) cols = 1;
    if (cols > total_items) cols = total_items;
    g.columns_ = cols;
    g.rows_ = (total_items + cols - 1) / cols;
    g.item_w_ = (container.w - gap * static_cast<f32>(cols - 1)) / static_cast<f32>(cols);
    g.item_h_ = item_h;
    g.count_ = total_items;
    return g;
}

rect grid_cursor::cell(i32 i) const
{
    if (i < 0 || i >= count_ || columns_ <= 0) return {};
    const i32 col = i % columns_;
    const i32 row = i / columns_;
    return rect::make(bounds_.x + static_cast<f32>(col) * (item_w_ + gap_),
                      bounds_.y + static_cast<f32>(row) * (item_h_ + gap_), item_w_, item_h_);
}

rect grid_cursor::next()
{
    if (index_ >= count_) return {};
    return cell(index_++);
}

[[nodiscard]] rect column::cut_top(f32 height) &
{
    return next(height);
}

[[nodiscard]] rect column::cut_bottom(f32 height) &
{
    const f32 h = clampf(height, 0.0f, max2(0.0f, bounds_.h));
    detail::report_slice_clamp("column cut_bottom", height, max2(0.0f, bounds_.h));
    rect r = bounds_.cut_bottom(h);
    bounds_.cut_bottom(min2(gap_, bounds_.h));
    return r;
}

[[nodiscard]] rect row::cut_left(f32 width) &
{
    const f32 w = clampf(width, 0.0f, max2(0.0f, bounds_.w));
    detail::report_slice_clamp("row cut_left", width, max2(0.0f, bounds_.w));
    rect r = bounds_.cut_left(w);
    bounds_.cut_left(min2(gap_, bounds_.w));
    return r;
}

[[nodiscard]] rect row::cut_right(f32 width) &
{
    const f32 w = clampf(width, 0.0f, max2(0.0f, bounds_.w));
    detail::report_slice_clamp("row cut_right", width, max2(0.0f, bounds_.w));
    rect r = bounds_.cut_right(w);
    bounds_.cut_right(min2(gap_, bounds_.w));
    return r;
}

void set_report_layout_overflow(context *c, bool enabled)
{
    if (!c) return;
    c->report_layout_overflow = enabled;
    if (enabled) c->overflow_count = 0;
}

i32 layout_overflow_count(context *c)
{
    return c ? c->overflow_count : 0;
}

void draw_violation_overlay(ui &u, rect r)
{
    context *c = u.ctx;
    if (!c || c->violations == 0) return;
    const theme &t = u.th();
    u.draw_rect(r, color{20, 24, 30, 230});
    u.draw_line(r.x, r.y, r.right(), r.y, t.accent, 2.0f);
    char head[96];
    std::snprintf(head, sizeof(head), "PufferUI: %d violation(s) [%s]", c->violations,
                  violation_code_name(c->vlast_code));
    rect body = r.pad(8.0f, 4.0f);
    {
        text_scope ts = u.text_style(12.0f);
        u.text(body.cut_top(16.0f), head, t.accent, ALIGN_LEFT);
        // the rest shows the last message, trimmed to fit
        u.text_ellipsis(body, c->vlast, t.text, ALIGN_LEFT);
    }
}

// ---- text ----
font_handle load_font(context *c, const char *path)
{
    if (!c->ts) c->ts = new text_store();
    text_store *ts = c->ts;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return FONT_INVALID;
    std::streamsize size = file.tellg();
    if (size <= 0) return FONT_INVALID;
    file.seekg(0, std::ios::beg);

    font_data fd;
    fd.buffer.resize(static_cast<usize>(size));
    if (!file.read(reinterpret_cast<char *>(fd.buffer.data()), size)) return FONT_INVALID;
    if (!stbtt_InitFont(&fd.info, fd.buffer.data(), 0)) return FONT_INVALID;

    fd.ok = true;
    ts->fonts.push_back(std::move(fd));
    return static_cast<font_handle>(ts->fonts.size() - 1);
}

namespace detail
{

inline bool ensure_atlas(context *c, text_store *ts)
{
    if (!ts->atlases.empty()) return true;
    if (!c->device) return false;
    text_store::atlas_page page;
    page.tex = c->device->create_texture(1024, 1024, nullptr);
    if (!page.tex) return false;
    ts->atlases.push_back(page);
    ts->atlas_w = 1024;
    ts->atlas_h = 1024;
    // Reserve the (0,0) texel as pure white: solid geometry samples it (see
    // draw_triangles' UV remap), so glyph packing starts at (1,1) and never
    // touches it.
    const u8 white[4] = {255, 255, 255, 255};
    c->device->update_texture(page.tex, 0, 0, 1, 1, white);
    ts->atlases.back().shelf_x = 1;
    ts->atlases.back().shelf_y = 1;
    return true;
}

inline glyph get_glyph(context *c, i32 font_index, f32 size, u32 cp)
{
    text_store *ts = c->ts;
    text_store::glyph_key key{font_index, static_cast<i32>(size * 4.0f), cp};
    auto it = ts->glyphs.find(key);
    if (it != ts->glyphs.end()) return it->second;

    glyph g{};
    font_data &fd = ts->fonts[static_cast<usize>(font_index)];
    f32 scale = stbtt_ScaleForPixelHeight(&fd.info, size);
    i32 advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&fd.info, static_cast<i32>(cp), &advance, &lsb);
    g.advance = static_cast<f32>(advance) * scale;

    i32 gw = 0, gh = 0, xo = 0, yo = 0;
    unsigned char *bitmap =
        stbtt_GetCodepointBitmap(&fd.info, scale, scale, static_cast<i32>(cp), &gw, &gh, &xo, &yo);
    g.xoff = static_cast<f32>(xo);
    g.yoff = static_cast<f32>(yo);

    if (bitmap && gw > 0 && gh > 0 && ensure_atlas(c, ts))
    {
        // Pack into the current page; spill to a new page when full.
        if (ts->atlases.back().shelf_x + gw + 1 > ts->atlas_w)
        {
            ts->atlases.back().shelf_y += ts->atlases.back().shelf_h;
            ts->atlases.back().shelf_h = 0;
            ts->atlases.back().shelf_x = 0;
        }
        if (gh > ts->atlases.back().shelf_h)
        {
            ts->atlases.back().shelf_y += ts->atlases.back().shelf_h;
            ts->atlases.back().shelf_h = gh;
            ts->atlases.back().shelf_x = 0;
        }
        if (ts->atlases.back().shelf_y + gh > ts->atlas_h)
        {
            // Page full: a fresh atlas. The glyph is never silently dropped.
            text_store::atlas_page page;
            page.tex = c->device->create_texture(ts->atlas_w, ts->atlas_h, nullptr);
            ts->atlases.push_back(page);
        }
        text_store::atlas_page &page = ts->atlases.back();
        const i32 px = page.shelf_x, py = page.shelf_y;
        page.shelf_x += gw + 1;
        std::vector<u8> rgba(static_cast<usize>(gw) * gh * 4);
        for (i32 n = 0; n < gw * gh; ++n)
        {
            u8 a = bitmap[n];
            rgba[n * 4 + 0] = 255;
            rgba[n * 4 + 1] = 255;
            rgba[n * 4 + 2] = 255;
            rgba[n * 4 + 3] = a;
        }
        c->device->update_texture(page.tex, px, py, gw, gh, rgba.data());
        g.tex = page.tex;
        g.u0 = static_cast<f32>(px) / ts->atlas_w;
        g.v0 = static_cast<f32>(py) / ts->atlas_h;
        g.u1 = static_cast<f32>(px + gw) / ts->atlas_w;
        g.v1 = static_cast<f32>(py + gh) / ts->atlas_h;
    }
    if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);

    ts->glyphs[key] = g;
    return g;
}

// Kerning between two codepoints in the active font, in pixels.
inline f32 kern_advance(context *c, font_handle fh, f32 size, u32 a, u32 b)
{
    if (!a || !b || !c->ts || fh < 0 || fh >= static_cast<i32>(c->ts->fonts.size())) return 0.0f;
    font_data &fd = c->ts->fonts[static_cast<usize>(fh)];
    if (!fd.ok) return 0.0f;
    const f32 scale = stbtt_ScaleForPixelHeight(&fd.info, size);
    return static_cast<f32>(
               stbtt_GetCodepointKernAdvance(&fd.info, static_cast<i32>(a), static_cast<i32>(b))) *
           scale;
}

} // namespace detail

bool ui::has_glyph(u32 cp)
{
    context *c = ctx;
    if (!c->ts) return false;
    font_handle fh = c->active_theme.font;
    if (fh < 0 || fh >= static_cast<i32>(c->ts->fonts.size())) return false;
    font_data &fd = c->ts->fonts[static_cast<usize>(fh)];
    if (!fd.ok) return false;
    return stbtt_FindGlyphIndex(&fd.info, static_cast<i32>(cp)) != 0;
}

bool ui::is_visible(rect r) const
{
    if (ctx->clip_depth == 0) return true;
    const rect x = rect::intersect(r, ctx->clip_stack[ctx->clip_depth - 1]);
    return x.w > 0.0f && x.h > 0.0f;
}

vec2 ui::button_size(std::string_view label)
{
    const button_style &s = ctx->active_theme.button;
    return vec2{text_width(label) + s.pad_x * 2.0f, ctx->active_theme.control_h};
}

vec2 ui::text_size(std::string_view s)
{
    return vec2{text_width(s), line_height()};
}

f32 ui::text_width(std::string_view s)
{
    context *c = ctx;
    if (!c->ts) return 0.0f;
    font_handle fh = c->active_theme.font;
    if (fh < 0 || fh >= static_cast<i32>(c->ts->fonts.size()) ||
        !c->ts->fonts[static_cast<usize>(fh)].ok)
        return 0.0f;
    if (s.empty()) return 0.0f;

    // The layout cache keys on (font, size, string hash); advances never
    // change for a loaded font, so entries cannot go stale. Building the
    // layout here is fine: a measure almost always precedes a draw of the
    // same string, and the built glyphs serve the emit path next call.
    const u64 key = (static_cast<u64>(static_cast<u32>(fh)) << 48) ^
                    (static_cast<u64>(static_cast<u32>(
                         static_cast<i32>(c->active_theme.text_size * 4.0f) & 0xFFFF))
                     << 32) ^
                    hash(s);
    if (text_store::text_layout *lay = c->ts->layout_cache.find(key)) return lay->width;

    text_store::text_layout built;
    usize i = 0;
    u32 prev = 0;
    while (i < s.size())
    {
        u32 cp = utf8_decode(s, i);
        if (cp == UTF8_END) break;
        text_store::cached_glyph e;
        e.kern_in = detail::kern_advance(c, fh, c->active_theme.text_size, prev, cp);
        e.g = detail::get_glyph(c, fh, c->active_theme.text_size, cp);
        // Two separate additions: bit-identical to the old accumulation order
        // (float associativity would drift the pen by ULPs — visible as edge
        // coverage changes in the golden scenes).
        built.width += e.kern_in;
        built.width += e.g.advance;
        built.gs.push_back(e);
        prev = cp;
    }
    c->ts->layout_cache.put(key, built);
    if (text_store::text_layout *lay = c->ts->layout_cache.find(key)) return lay->width;
    return 0.0f;
}

void ui::text(rect r, std::string_view s, color c, align a)
{
    context *cc = ctx;
    if (s.empty() || !cc->ts) return;
    font_handle fh = cc->active_theme.font;
    if (fh < 0 || fh >= static_cast<i32>(cc->ts->fonts.size()) ||
        !cc->ts->fonts[static_cast<usize>(fh)].ok)
    {
        // Text with no working font draws nothing at all: an authoring
        // mistake the overlay and a debugger should surface.
        report_violation(VIOL_NO_FONT, "text with no font",
                         "text drawn while the theme has no loaded font "
                         "(load_font + set_theme)",
                         "", 0, false);
        return;
    }

    const f32 size = cc->active_theme.text_size;
    // The layout cache is shared with text_width (same key): a hit emits
    // from cached glyph copies — no decode, no glyph-map lookups, no stb
    // calls.
    const u64 key =
        (static_cast<u64>(static_cast<u32>(fh)) << 48) ^
        (static_cast<u64>(static_cast<u32>(static_cast<i32>(size * 4.0f) & 0xFFFF)) << 32) ^
        hash(s);
    text_store::text_layout *lay = cc->ts->layout_cache.find(key);
    if (!lay)
    {
        text_store::text_layout built;
        usize i = 0;
        u32 prev = 0;
        while (i < s.size())
        {
            u32 cp = utf8_decode(s, i);
            if (cp == UTF8_END) break;
            text_store::cached_glyph e;
            e.kern_in = detail::kern_advance(cc, fh, size, prev, cp);
            e.g = detail::get_glyph(cc, fh, size, cp);
            built.width += e.kern_in; // two additions: same order as before
            built.width += e.g.advance;
            built.gs.push_back(e);
            prev = cp;
        }
        cc->ts->layout_cache.put(key, built);
        lay = cc->ts->layout_cache.find(key);
        if (!lay) return; // cache full? rebuild path guarantees a hit; paranoia
    }

    const f32 width = lay->width;
    f32 pen = (a == ALIGN_CENTER)  ? r.x + (r.w - width) * 0.5f
              : (a == ALIGN_RIGHT) ? r.right() - width
                                   : r.x;

    i32 ascent = 0, descent = 0, line_gap = 0;
    font_data &fd = cc->ts->fonts[static_cast<usize>(fh)];
    stbtt_GetFontVMetrics(&fd.info, &ascent, &descent, &line_gap);
    const f32 scale = stbtt_ScaleForPixelHeight(&fd.info, size);
    const f32 baseline = r.y + (r.h - size) * 0.5f + static_cast<f32>(ascent) * scale;

    const rect *clip = (cc->clip_depth > 0) ? &cc->clip_stack[cc->clip_depth - 1] : nullptr;

    for (const text_store::cached_glyph &e : lay->gs)
    {
        const glyph &g = e.g;
        if (g.tex)
        {
            detail::dl_prepare(cc, g.tex, clip);
            const f32 gx = pen + g.xoff;
            const f32 gy = baseline + g.yoff;
            const f32 gw = (g.u1 - g.u0) * cc->ts->atlas_w;
            const f32 gh = (g.v1 - g.v0) * cc->ts->atlas_h;
            vertex v[4] = {
                {gx, gy, g.u0, g.v0, c},
                {gx + gw, gy, g.u1, g.v0, c},
                {gx + gw, gy + gh, g.u1, g.v1, c},
                {gx, gy + gh, g.u0, g.v1, c},
            };
            i32 idx[6] = {0, 1, 2, 0, 2, 3};
            detail::dl_add(cc, v, 4, idx, 6);
        }
        pen += e.kern_in; // two additions, exactly the old accumulation order
        pen += g.advance;
    }
}

// Truncates with a trailing ellipsis (U+2026) when the line does not fit.
void ui::text_ellipsis(rect r, std::string_view s, color c, align a)
{
    if (s.empty()) return;
    if (text_width(s) <= r.w)
    {
        text(r, s, c, a);
        return;
    }
    static constexpr char ELLIPSIS[] = "\xE2\x80\xA6";
    const f32 ell_w = text_width(ELLIPSIS);
    // One forward pass accumulating the same per-glyph advances text_width
    // would sum (kern pairs included): stop at the last codepoint boundary
    // whose prefix still fits next to the ellipsis. No prefix re-measures.
    context *cc = ctx;
    font_handle fh = cc->active_theme.font;
    usize i = 0, cut = 0;
    f32 x = 0.0f;
    u32 prev = 0;
    while (i < s.size())
    {
        u32 cp = utf8_decode(s, i);
        if (cp == UTF8_END) break;
        x += detail::kern_advance(cc, fh, cc->active_theme.text_size, prev, cp);
        x += detail::get_glyph(cc, fh, cc->active_theme.text_size, cp).advance;
        if (x + ell_w <= r.w) cut = i; // i is past this codepoint now
        prev = cp;
    }
    std::string out(s.substr(0, cut));
    out += ELLIPSIS;
    text(r, out, c, a);
}

text_scope::text_scope(ui &u, f32 size, font_handle font) : u_(&u), size_(size), font_(font)
{
    old_size_ = u.ctx->active_theme.text_size;
    old_font_ = u.ctx->active_theme.font;
    u.ctx->active_theme.text_size = size;
    if (font != FONT_INVALID) u.ctx->active_theme.font = font;
}

text_scope::~text_scope()
{
    if (!u_ || !u_->ctx) return;
    u_->ctx->active_theme.text_size = old_size_;
    u_->ctx->active_theme.font = old_font_;
}

text_scope ui::text_style(f32 size, font_handle font)
{
    return text_scope(*this, size, font);
}

measure_size ui::measure_text(std::string_view s, f32 available_width)
{
    const f32 line_h = line_height();
    if (s.empty()) return {0.0f, line_h};
    if (available_width <= 0.0f) available_width = 1.0e9f;

    const f32 space_w = text_width(" ");
    f32 line_w = 0.0f, max_w = 0.0f;
    i32 lines = 1;
    std::string word;

    auto flush = [&]()
    {
        if (word.empty()) return;
        const f32 ww = text_width(word);
        if (line_w > 0.0f && line_w + space_w + ww > available_width)
        {
            max_w = max2(max_w, line_w);
            line_w = ww;
            lines += 1;
        }
        else
        {
            line_w += (line_w > 0.0f ? space_w : 0.0f) + ww;
        }
        word.clear();
    };

    for (char ch : s)
    {
        if (ch == ' ' || ch == '\n')
        {
            flush();
            if (ch == '\n')
            {
                max_w = max2(max_w, line_w);
                line_w = 0.0f;
                lines += 1;
            }
        }
        else
        {
            word.push_back(ch);
        }
    }
    flush();
    max_w = max2(max_w, line_w);
    return {min2(max_w, available_width), static_cast<f32>(lines) * line_h};
}

measure_size ui::measure(function_ref<measure_size(f32)> fn, f32 available_width)
{
    if (!fn) return {};
    return fn(available_width);
}

void ui::text_wrapped(rect r, std::string_view s, color c)
{
    if (s.empty()) return;
    const f32 line_h = line_height();
    const f32 space_w = text_width(" ");
    f32 x = r.x, y = r.y, line_w = 0.0f;
    std::string word;

    auto flush = [&]()
    {
        if (word.empty()) return;
        const f32 ww = text_width(word);
        if (line_w > 0.0f && line_w + space_w + ww > r.w)
        {
            y += line_h;
            x = r.x;
            line_w = 0.0f;
        }
        if (y + line_h <= r.bottom() + 0.5f)
        {
            text(rect::make(x, y, ww, line_h), word, c, ALIGN_LEFT);
            x += ww + space_w;
            line_w += (line_w > 0.0f ? space_w : 0.0f) + ww;
        }
        word.clear();
    };

    for (char ch : s)
    {
        if (ch == ' ' || ch == '\n')
        {
            flush();
            if (ch == '\n')
            {
                y += line_h;
                x = r.x;
                line_w = 0.0f;
            }
        }
        else
        {
            word.push_back(ch);
        }
    }
    flush();
}

// ---- dock layout persistence -----------------------------------------------
static void dock_save_node(const dock_node &n, i32 depth, std::string &out)
{
    char line[512];
    if (n.kind == DOCK_SPLIT_H || n.kind == DOCK_SPLIT_V)
    {
        std::snprintf(line, sizeof(line), "d%d split %c %.4f\n", depth,
                      n.kind == DOCK_SPLIT_H ? 'h' : 'v', static_cast<double>(n.ratio));
        out += line;
        if (n.a) dock_save_node(*n.a, depth + 1, out);
        if (n.b) dock_save_node(*n.b, depth + 1, out);
        return;
    }
    // leaf: "d<depth> leaf <active> id|name;id|name"
    i32 at = static_cast<i32>(std::snprintf(line, sizeof(line), "d%d leaf %d ", depth, n.active));
    for (i32 i = 0; i < n.panel_count; ++i)
    {
        const char *name = n.panel_names[i] ? n.panel_names[i] : "panel";
        at += static_cast<i32>(std::snprintf(line + at, sizeof(line) - static_cast<usize>(at),
                                             "%s%u|%s", i > 0 ? ";" : "",
                                             static_cast<unsigned>(n.panels[i]), name));
    }
    if (at < 0) at = 0;
    if (at > static_cast<i32>(sizeof(line)) - 2) at = static_cast<i32>(sizeof(line)) - 2;
    line[at] = '\n';
    line[at + 1] = '\0';
    out += line;
}

std::string dock_save_tree(const dock_node &root)
{
    std::string out;
    dock_save_node(root, 0, out);
    return out;
}

bool dock_restore_tree(std::string_view text, dock_node *nodes, i32 node_cap,
                       std::string &name_storage, dock_node *&out_root)
{
    out_root = nullptr;
    i32 used = 0;
    // The name pointers handed out point into `name_storage`; reserve once so
    // no reallocation ever invalidates them mid-parse.
    name_storage.reserve(name_storage.size() + text.size());
    // The parse stack: (depth, node) — pop on a shallower depth, attach as the
    // first free child of the parent.
    struct frame
    {
        i32 depth;
        dock_node *n;
    };
    frame stack[MAX_DOCK_DEPTH + 1]{};
    i32 stack_top = 0;
    usize pos = 0;
    while (pos < text.size())
    {
        usize end = pos;
        while (end < text.size() && text[end] != '\n') ++end;
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (line.empty()) continue;
        if (line[0] != 'd') return false; // every line starts with its depth
        usize p = 1;
        i32 depth = 0;
        while (p < line.size() && line[p] >= '0' && line[p] <= '9')
        {
            depth = depth * 10 + (line[p] - '0');
            ++p;
        }
        if (depth > MAX_DOCK_DEPTH) return false;
        if (used >= node_cap) return false;
        while (stack_top > 0 && stack[stack_top - 1].depth >= depth) --stack_top;
        dock_node &n = nodes[used++];
        n = dock_node{};
        if (stack_top > 0)
        {
            dock_node *parent = stack[stack_top - 1].n;
            if (parent->a == nullptr)
                parent->a = &n;
            else if (parent->b == nullptr)
                parent->b = &n;
            else
                return false; // a split with three children: malformed
        }
        else
        {
            if (out_root != nullptr) return false; // two roots
            out_root = &n;
        }
        std::string_view args = line.substr(p);
        while (!args.empty() && args[0] == ' ') args.remove_prefix(1);
        if (args.size() >= 5 && args.compare(0, 5, "split") == 0)
        {
            n.kind = (args.size() > 6 && args[6] == 'v') ? DOCK_SPLIT_V : DOCK_SPLIT_H;
            const f32 v = static_cast<f32>(std::atof(std::string(args.substr(8)).c_str()));
            n.ratio = clampf(v, 0.01f, 0.99f);
            stack[stack_top].depth = depth;
            stack[stack_top].n = &n;
            ++stack_top;
            continue;
        }
        if (args.size() >= 4 && args.compare(0, 4, "leaf") == 0)
        {
            n.kind = DOCK_LEAF;
            // "<active> id|name;id|name..."
            usize q = 4;
            while (q < args.size() && args[q] == ' ') ++q;
            n.active = 0;
            while (q < args.size() && args[q] >= '0' && args[q] <= '9')
            {
                n.active = n.active * 10 + (args[q] - '0');
                ++q;
            }
            while (q < args.size() && args[q] == ' ') ++q;
            while (q < args.size())
            {
                u32 id = 0;
                while (q < args.size() && args[q] >= '0' && args[q] <= '9')
                {
                    id = id * 10 + (args[q] - '0');
                    ++q;
                }
                if (q >= args.size() || args[q] != '|') return false;
                ++q;
                usize name_start = name_storage.size();
                while (q < args.size() && args[q] != ';' && args[q] != ' ')
                {
                    name_storage.push_back(args[q]);
                    ++q;
                }
                name_storage.push_back('\0');
                if (n.panel_count < MAX_DOCK_PANELS)
                {
                    n.panels[n.panel_count] = static_cast<uiid>(id);
                    n.panel_names[n.panel_count] = name_storage.c_str() + name_start;
                    n.panel_count += 1;
                }
                if (q < args.size() && args[q] == ';') ++q;
            }
            if (n.panel_count == 0) return false;
            if (n.active >= n.panel_count) n.active = n.panel_count - 1;
            continue;
        }
        return false;
    }
    return true;
}

f32 ui::text_wrapped_height(f32 width, std::string_view s)
{
    if (s.empty() || width <= 0.0f) return 0.0f;
    const f32 line_h = line_height();
    const f32 space_w = text_width(" ");
    f32 x = 0.0f, y = 0.0f, line_w = 0.0f, height = 0.0f;
    std::string word;

    auto flush = [&]()
    {
        if (word.empty()) return;
        const f32 ww = text_width(word);
        if (line_w > 0.0f && line_w + space_w + ww > width)
        {
            y += line_h;
            x = 0.0f;
            line_w = 0.0f;
        }
        height = (y + line_h) > height ? (y + line_h) : height;
        x += ww + space_w;
        line_w += (line_w > 0.0f ? space_w : 0.0f) + ww;
        word.clear();
    };

    for (char ch : s)
    {
        if (ch == ' ' || ch == '\n')
        {
            flush();
            if (ch == '\n')
            {
                y += line_h;
                x = 0.0f;
                line_w = 0.0f;
                height = (y + line_h) > height ? (y + line_h) : height;
            }
        }
        else
        {
            word.push_back(ch);
        }
    }
    flush();
    return height;
}

f32 ui::text_fit(rect r, std::string_view s, color c, align a)
{
    if (s.empty()) return 0.0f;
    if (text_width(s) <= r.w)
    { // fits on one line
        text(r, s, c, a);
        return line_height();
    }
    const f32 h = measure_text(s, r.w).height; // wrap within the rect
    text_wrapped(rect::make(r.x, r.y, r.w, h), s, c);
    return h;
}

// ---- style cascade ----
button_style ui::resolve_button_style(uiid role_id, const button_override &ov) const
{
    button_style s = ctx->active_theme.button;
    if (role_id)
    {
        for (i32 i = 0; i < ctx->active_theme.button_role_count; ++i)
        {
            if (ctx->active_theme.button_roles[i].id == role_id)
            {
                s = merge_style(s, ctx->active_theme.button_roles[i].ov);
                break;
            }
        }
    }
    for (i32 i = 0; i < ctx->button_scope_depth; ++i) s = merge_style(s, ctx->button_scopes[i]);
    s = merge_style(s, ov);
    return s;
}

style_scope::style_scope(ui &u, button_override ov)
{
    u_ = &u;
    context *c = u.ctx;
    if (c->button_scope_depth < MAX_STYLE_SCOPES)
    {
        c->button_scopes[c->button_scope_depth++] = ov;
        pushed_ = true;
    }
    else
    {
        PUFFERUI_CHECK(VIOL_SCOPE_UNBALANCED, false, "button style scope overflow");
    }
}

style_scope::~style_scope()
{
    if (!u_ || !pushed_) return;
    context *c = u_->ctx;
    if (c->button_scope_depth > 0) --c->button_scope_depth;
}

// ---- popups ----
namespace detail
{
// Removes a popup pushed *this* frame from the live table, for widgets that
// close within the same frame they would have opened (a combo pick, an Escape
// close). Without it the entry would persist through end_frame and the next
// frame's `prev_popups` would still report the menu as open for one frame —
// enough to block a click and, for toggle-style widgets, to flash reopen.
inline void popup_retract(context *c, uiid id)
{
    for (i32 i = c->popup_depth - 1; i >= 0; --i)
    {
        if (c->popups[i].id == id)
        {
            for (i32 j = i; j < c->popup_depth - 1; ++j) c->popups[j] = c->popups[j + 1];
            c->popup_depth -= 1;
            return;
        }
    }
}
} // namespace detail

popup_scope::popup_scope(ui &u, uiid id, rect area, popup_flags flags)
{
    u_ = &u;
    id_ = id;
    area_ = area;
    context *c = u.ctx;
    if (has_flag(flags, POPUP_CLOSE_ON_CLICK_OUTSIDE))
    {
        if (c->popup_click_outside && c->popup_click_id == id) close_requested = true;
    }
    if (has_flag(flags, POPUP_CLOSE_ON_ESCAPE))
    {
        if (c->key_pressed[static_cast<i32>(key::ESCAPE)]) close_requested = true;
    }
    detail::ensure_popup_capacity(c, c->popup_depth + 1);
    c->popups[c->popup_depth++] =
        popup_entry{id, area, c->current_window ? c->current_window->index : 0};

    c->popup_layer_depth += 1;
    c->in_popup = true;
}

popup_scope::~popup_scope()
{
    // The popup entry persists for the rest of the frame so the *next* frame can
    // use it for input capture (§19.3); the stack resets in begin_frame. Only the
    // "currently drawing popup content" marker is restored here.
    if (u_)
    {
        context *c = u_->ctx;
        if (c->popup_layer_depth > 0) c->popup_layer_depth -= 1;
        c->in_popup = c->popup_layer_depth > 0;
    }
}

popup_scope ui::popup(uiid id, rect area, popup_flags flags)
{
    return popup_scope(*this, id, area, flags);
}

// ---- built-in widget ----
namespace detail
{
// A rounded-rect outline drawn as 4 edge strips + 4 corner annuli. Filling the
// whole bounds with the border color and then inset-filling the background (the
// classic two-rect trick) makes translucent backgrounds impossible: the border
// color would show through them instead of the backdrop.
// A straight band whose two long edges carry per-vertex alpha, matching
// draw_sector's 1px arc feathers: in a rounded_ring the straight and arced
// segments then shade identically instead of meeting in a crisp step.
inline void ring_hband(ui &u, f32 x0, f32 x1, f32 y0, f32 y1, color ca, color cb)
{
    vertex v[4] = {
        {x0, y0, 0.0f, 0.0f, ca},
        {x1, y0, 1.0f, 0.0f, ca},
        {x1, y1, 1.0f, 1.0f, cb},
        {x0, y1, 0.0f, 1.0f, cb},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    u.draw_triangles(nullptr, v, 4, idx, 6);
}

inline void ring_vband(ui &u, f32 x0, f32 x1, f32 y0, f32 y1, color ca, color cb)
{
    vertex v[4] = {
        {x0, y0, 0.0f, 0.0f, ca},
        {x1, y0, 1.0f, 0.0f, cb},
        {x1, y1, 1.0f, 1.0f, cb},
        {x0, y1, 0.0f, 1.0f, ca},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    u.draw_triangles(nullptr, v, 4, idx, 6);
}

inline void rounded_ring(ui &u, rect r, color c, f32 radius, f32 thickness)
{
    if (c.a == 0 || thickness <= 0.0f) return;
    const f32 t = min2(thickness, min2(r.w, r.h) * 0.5f);
    if (t <= 0.0f) return;
    radius = clampf(radius, 0.0f, min2(r.w, r.h) * 0.5f);

    if (radius <= 0.5f)
    {
        u.draw_rect({r.x, r.y, r.w, t}, c);
        u.draw_rect({r.x, r.bottom() - t, r.w, t}, c);
        u.draw_rect({r.x, r.y + t, t, r.h - 2.0f * t}, c);
        u.draw_rect({r.right() - t, r.y + t, t, r.h - 2.0f * t}, c);
        return;
    }

    // Straight bands with the same 1px feathers the corner arcs get from
    // draw_sector (transparent 1px outside the outer edge and, when the hole
    // is wide enough, 1px inside the inner edge). Without them the hard strip
    // edges met the soft arc edges in a crisp step toward the inside.
    const f32 r_in = max2(0.0f, radius - t);
    const bool feather_in = r_in > 1.0f; // must match draw_sector's ring rule
    color fade = c;
    fade.a = 0;

    // top / bottom
    ring_hband(u, r.x + radius, r.right() - radius, r.y - 1.0f, r.y, fade, c);
    ring_hband(u, r.x + radius, r.right() - radius, r.y, r.y + t, c, c);
    if (feather_in)
        ring_hband(u, r.x + radius, r.right() - radius, r.y + t, r.y + t + 1.0f, c, fade);
    ring_hband(u, r.x + radius, r.right() - radius, r.bottom(), r.bottom() + 1.0f, c, fade);
    ring_hband(u, r.x + radius, r.right() - radius, r.bottom() - t, r.bottom(), c, c);
    if (feather_in)
        ring_hband(u, r.x + radius, r.right() - radius, r.bottom() - t - 1.0f, r.bottom() - t, fade,
                   c);

    // left / right
    ring_vband(u, r.x - 1.0f, r.x, r.y + radius, r.bottom() - radius, fade, c);
    ring_vband(u, r.x, r.x + t, r.y + radius, r.bottom() - radius, c, c);
    if (feather_in)
        ring_vband(u, r.x + t, r.x + t + 1.0f, r.y + radius, r.bottom() - radius, c, fade);
    ring_vband(u, r.right(), r.right() + 1.0f, r.y + radius, r.bottom() - radius, c, fade);
    ring_vband(u, r.right() - t, r.right(), r.y + radius, r.bottom() - radius, c, c);
    if (feather_in)
        ring_vband(u, r.right() - t - 1.0f, r.right() - t, r.y + radius, r.bottom() - radius, fade,
                   c);

    u.draw_sector(vec2{r.x + radius, r.y + radius}, r_in, radius, PI, PI * 1.5f, c);
    u.draw_sector(vec2{r.right() - radius, r.y + radius}, r_in, radius, PI * 1.5f, PI * 2.0f, c);
    u.draw_sector(vec2{r.right() - radius, r.bottom() - radius}, r_in, radius, 0.0f, PI * 0.5f, c);
    u.draw_sector(vec2{r.x + radius, r.bottom() - radius}, r_in, radius, PI * 0.5f, PI, c);
}

// The keyboard-focus outline drawn on the widget that owns `c->focus`.
inline void focus_ring(ui &u, rect r, f32 radius)
{
    const theme &t = u.th();
    if (t.focus_border_thickness <= 0.0f) return;
    rounded_ring(u, r, t.focus_border, radius, t.focus_border_thickness);
}
} // namespace detail

bool ui::button(rect r, std::string_view label, uiid id, uiid role_id, button_override ov)
{
    interaction in = interact(id, r);
    if (in.hovered) ctx->want_cursor = ctx->active_theme.button_cursor;
    const button_style s = resolve_button_style(role_id, ov);
    color bg = in.held ? s.active_bg : in.hovered ? s.hover_bg : s.bg;
    if (s.transition.duration > 0.0f)
        bg = animate_color(id_child(id, "bg"_id), bg,
                           tween{s.transition.duration, s.transition.curve});
    // Opaque backgrounds keep the classic two-rect outline (a single crisp AA
    // edge); translucent or empty backgrounds need the ring path, otherwise the
    // border color shows through them (outline buttons, frosted glass). A fully
    // transparent border is treated as "no border" so the fill stays full-size
    // instead of being inset by the invisible ring.
    if (s.border_thickness > 0.0f && s.border.a > 0 && bg.a >= 250)
    {
        draw_rounded_rect(r, s.border, s.radius);
        draw_rounded_rect(r.pad(s.border_thickness), bg, max2(0.0f, s.radius - s.border_thickness));
    }
    else
    {
        if (bg.a > 0) draw_rounded_rect(r, bg, s.radius);
        detail::rounded_ring(*this, r, s.border, s.radius, s.border_thickness);
    }
    text(r, label, s.text, ALIGN_CENTER);
    if (in.focused)
    {
        detail::focus_ring(*this, r, s.radius);
        // Keyboard activation: Enter or Space clicks the focused button.
        if (ctx->key_pressed[static_cast<i32>(key::ENTER)] ||
            ctx->key_pressed[static_cast<i32>(key::SPACE)])
        {
            ctx->key_pressed[static_cast<i32>(key::ENTER)] = false;
            ctx->key_pressed[static_cast<i32>(key::SPACE)] = false;
            return true;
        }
    }
    return in.clicked;
}

bool ui::button(rect r, std::string_view label, uiid id, const button_opts &opts)
{
    return button(r, label, id, opts.role, opts.style);
}

// ---- component library (pui::comp) ------------------------------------------

namespace detail
{
// defined later with the clip helpers (RAII scopes in the components below)
bool clip_push(context *c, rect r);
void clip_pop(context *c);
} // namespace detail

namespace comp
{

switch_result switch_toggle(ui &u, rect area, std::string_view label, bool &value,
                            const switch_props &p)
{
    switch_result out{};
    switch_style s = u.th().switch_ctrl;
    if (p.style.track_off.set) s.track_off = p.style.track_off.value;
    if (p.style.track_on.set) s.track_on = p.style.track_on.value;
    if (p.style.knob.set) s.knob = p.style.knob.value;
    if (p.style.width.set) s.width = p.style.width.value;
    if (p.style.height.set) s.height = p.style.height.value;
    if (p.style.knob_pad.set) s.knob_pad = p.style.knob_pad.value;

    out.in = u.interact(p.id, area, p.enabled);
    if (out.in.hovered && p.enabled) u.set_cursor(u.th().button_cursor);
    if (out.in.focused && p.enabled && (u.key_pressed(key::ENTER) || u.key_pressed(key::SPACE)))
    {
        u.ctx->key_pressed[static_cast<i32>(key::ENTER)] = false;
        u.ctx->key_pressed[static_cast<i32>(key::SPACE)] = false;
        value = !value;
        out.changed = true;
    }
    if (out.in.clicked && p.enabled)
    {
        value = !value;
        out.changed = true;
    }

    const rect track{area.x, area.y + (area.h - s.height) * 0.5f, s.width, s.height};
    const tween tw{s.anim.duration, s.anim.curve};
    const color bg = s.animate ? u.animate_color(id_child(p.id, "track"_id),
                                                 value ? s.track_on : s.track_off, tw)
                               : (value ? s.track_on : s.track_off);
    const f32 t = s.animate ? u.animate(id_child(p.id, "knob"_id), value ? 1.0f : 0.0f, tw)
                            : (value ? 1.0f : 0.0f);
    u.draw_rounded_rect(track, bg, s.height * 0.5f);
    const f32 knob_d = s.height - s.knob_pad * 2.0f;
    const f32 travel = max2(0.0f, s.width - knob_d - s.knob_pad * 2.0f);
    const rect knob{track.x + s.knob_pad + travel * t, track.y + s.knob_pad, knob_d, knob_d};
    u.draw_rounded_rect(knob, s.knob, knob_d * 0.5f);
    if (out.in.focused) detail::focus_ring(u, track, s.height * 0.5f);
    if (!label.empty())
        u.text(rect::make(track.right() + u.spacing(), area.y,
                          max2(0.0f, area.right() - track.right() - u.spacing()), area.h),
               label, u.th().text, ALIGN_LEFT);
    return out;
}

vec2 switch_size(ui &u, std::string_view label)
{
    const switch_style &s = u.th().switch_ctrl;
    const f32 label_w = label.empty() ? 0.0f : u.text_size(label).x + u.spacing();
    return vec2{s.width + label_w, s.height};
}

radio_result radio_group(ui &u, rect area, std::span<const char *const> labels, i32 &selected,
                         const radio_props &p)
{
    radio_result out{};
    radio_style st = u.th().radio;
    if (p.style.ring.set) st.ring = p.style.ring.value;
    if (p.style.fill.set) st.fill = p.style.fill.value;
    if (p.style.size.set) st.size = p.style.size.value;
    if (p.style.gap.set) st.gap = p.style.gap.value;
    if (p.style.ring_w.set) st.ring_w = p.style.ring_w.value;

    const f32 row_h = p.row_h > 0.0f ? p.row_h : u.control_h();
    const i32 n = static_cast<i32>(labels.size());
    column col(area, 0.0f);
    for (i32 i = 0; i < n; ++i)
    {
        const rect row = col.next(row_h);
        if (row.h <= 0.0f) break;
        const uiid item_id = id_child(p.id, static_cast<uiid>(i));
        const interaction in = u.interact(item_id, row, p.enabled);
        if (in.hovered && p.enabled) u.set_cursor(u.th().button_cursor);

        bool keyboard = false;
        if (in.focused && p.enabled && (u.key_pressed(key::ENTER) || u.key_pressed(key::SPACE)))
        {
            u.ctx->key_pressed[static_cast<i32>(key::ENTER)] = false;
            u.ctx->key_pressed[static_cast<i32>(key::SPACE)] = false;
            keyboard = true;
        }
        if ((in.clicked || keyboard) && p.enabled)
        {
            if (selected != i)
            {
                selected = i;
                out.changed = true;
            }
        }
        out.selected = selected;

        const f32 d = st.size;
        const rect dot{row.x, row.y + (row.h - d) * 0.5f, d, d};
        const f32 radius = d * 0.5f;
        const bool on = (selected == i);
        // A ring, not a fill-then-inset (translucent backgrounds stay real).
        detail::rounded_ring(u, dot, on ? st.fill : st.ring, radius, st.ring_w);
        if (on) u.draw_rounded_rect(dot.pad(st.ring_w + 2.0f), st.fill, radius - st.ring_w - 2.0f);
        if (in.focused) detail::focus_ring(u, dot.pad(-2.0f), radius + 2.0f);
        u.text(rect::make(dot.right() + st.gap, row.y, max2(0.0f, row.w - d - st.gap), row.h),
               labels[i] ? labels[i] : "", u.th().text, ALIGN_LEFT);
    }
    return out;
}

segmented_result segmented(ui &u, rect area, std::span<const char *const> labels, i32 &selected,
                           const segmented_props &p)
{
    segmented_result out{};
    segmented_style st = u.th().segmented;
    if (p.style.bg.set) st.bg = p.style.bg.value;
    if (p.style.selected.set) st.selected = p.style.selected.value;
    if (p.style.text.set) st.text = p.style.text.value;
    if (p.style.text_selected.set) st.text_selected = p.style.text_selected.value;
    if (p.style.radius.set) st.radius = p.style.radius.value;
    if (p.style.pad.set) st.pad = p.style.pad.value;

    const i32 n = static_cast<i32>(labels.size());
    if (n <= 0) return out;
    u.draw_rounded_rect(area, st.bg, st.radius);
    const rect inner = area.pad(st.pad);
    const f32 seg_w = inner.w / static_cast<f32>(n);
    for (i32 i = 0; i < n; ++i)
    {
        const f32 x = inner.x + seg_w * static_cast<f32>(i);
        const f32 w = (i == n - 1) ? (inner.right() - x) : seg_w;
        const rect seg = rect::make(x, inner.y, w, inner.h);
        const uiid item_id = id_child(p.id, static_cast<uiid>(i));
        const interaction in = u.interact(item_id, seg, p.enabled);
        if (in.hovered && p.enabled) u.set_cursor(u.th().button_cursor);

        bool keyboard = false;
        if (in.focused && p.enabled && (u.key_pressed(key::ENTER) || u.key_pressed(key::SPACE)))
        {
            u.ctx->key_pressed[static_cast<i32>(key::ENTER)] = false;
            u.ctx->key_pressed[static_cast<i32>(key::SPACE)] = false;
            keyboard = true;
        }
        if ((in.clicked || keyboard) && p.enabled)
        {
            if (selected != i)
            {
                selected = i;
                out.changed = true;
            }
        }
        out.selected = selected;

        const bool on = (selected == i);
        const color fill = on ? u.animate_color(id_child(item_id, "bg"_id), st.selected,
                                                tween{st.anim.duration, st.anim.curve})
                              : color{0, 0, 0, 0};
        if (on) u.draw_rounded_rect(seg, fill, max2(0.0f, st.radius - st.pad));
        if (in.focused) detail::focus_ring(u, seg, max2(0.0f, st.radius - st.pad));
        u.text(seg, labels[i] ? labels[i] : "", on ? st.text_selected : st.text, ALIGN_CENTER);
    }
    return out;
}

tabs_result tab_bar(ui &u, rect area, std::span<const char *const> labels, i32 &active,
                    const tabs_props &p)
{
    tabs_result out{};
    tabs_style st = u.th().tabs;
    if (p.style.text.set) st.text = p.style.text.value;
    if (p.style.text_active.set) st.text_active = p.style.text_active.value;
    if (p.style.underline.set) st.underline = p.style.underline.value;
    if (p.style.hover_bg.set) st.hover_bg = p.style.hover_bg.value;
    if (p.style.radius.set) st.radius = p.style.radius.value;
    if (p.style.underline_h.set) st.underline_h = p.style.underline_h.value;
    if (p.style.gap.set) st.gap = p.style.gap.value;

    const i32 n = static_cast<i32>(labels.size());
    row tabs(area, st.gap);
    for (i32 i = 0; i < n; ++i)
    {
        const uiid item_id = id_child(p.id, static_cast<uiid>(i));
        const f32 w = u.text_size(labels[i] ? labels[i] : "").x + u.padding();
        const rect tab = tabs.next(w);
        if (tab.w <= 0.0f) break;
        const interaction in = u.interact(item_id, tab, p.enabled);
        if (in.hovered && p.enabled) u.set_cursor(u.th().button_cursor);
        bool keyboard = false;
        if (in.focused && p.enabled && (u.key_pressed(key::ENTER) || u.key_pressed(key::SPACE)))
        {
            u.ctx->key_pressed[static_cast<i32>(key::ENTER)] = false;
            u.ctx->key_pressed[static_cast<i32>(key::SPACE)] = false;
            keyboard = true;
        }
        if ((in.clicked || keyboard) && p.enabled && active != i)
        {
            active = i;
            out.changed = true;
        }
        out.active = active;
        out.in = in;

        const bool on = (active == i);
        if (in.hovered && !on) u.draw_rounded_rect(tab, st.hover_bg, st.radius);
        u.text(tab, labels[i] ? labels[i] : "", on ? st.text_active : st.text, ALIGN_CENTER);
        const color ul =
            u.animate_color(id_child(item_id, "ul"_id), on ? st.underline : color{0, 0, 0, 0},
                            tween{st.anim.duration, st.anim.curve});
        u.draw_rect(rect::make(tab.x, tab.bottom() - st.underline_h, tab.w, st.underline_h), ul);
        if (in.focused) detail::focus_ring(u, tab, st.radius);
    }
    return out;
}

accordion_scope::accordion_scope(ui &u, rect area, std::string_view title, bool &open,
                                 const accordion_props &p)
{
    u_ = &u;
    id_ = p.id;
    open_ = open;
    context *c = u.ctx;
    accordion_style st = c->active_theme.accordion;
    if (p.style.header_bg.set) st.header_bg = p.style.header_bg.value;
    if (p.style.header_hover.set) st.header_hover = p.style.header_hover.value;
    if (p.style.text.set) st.text = p.style.text.value;
    if (p.style.chevron.set) st.chevron = p.style.chevron.value;
    if (p.style.radius.set) st.radius = p.style.radius.value;
    if (p.style.header_h.set) st.header_h = p.style.header_h.value;
    const f32 header_h = p.header_h > 0.0f ? p.header_h : st.header_h;

    rect body = area;
    const rect header = body.cut_top(header_h);
    const interaction in = u.interact(id_, header, p.enabled);
    if (in.hovered && p.enabled) u.set_cursor(c->active_theme.button_cursor);
    bool keyboard = false;
    if (in.focused && p.enabled && (u.key_pressed(key::ENTER) || u.key_pressed(key::SPACE)))
    {
        c->key_pressed[static_cast<i32>(key::ENTER)] = false;
        c->key_pressed[static_cast<i32>(key::SPACE)] = false;
        keyboard = true;
    }
    if ((in.clicked || keyboard) && p.enabled)
    {
        open = !open;
        open_ = open;
        toggled_ = true;
    }
    u.draw_rounded_rect(header, in.hovered ? st.header_hover : st.header_bg, st.radius);
    u.text(rect::make(header.x + u.padding(), header.y, max2(0.0f, header.w - u.padding() * 2.0f),
                      header.h),
           title, st.text, ALIGN_LEFT);
    // chevron: down when open, right when closed (rotates with the animation)
    const f32 t = u.animate(id_child(p.id, "t"_id), open ? 1.0f : 0.0f,
                            tween{st.anim.duration, st.anim.curve});
    {
        const f32 cx = header.right() - u.padding();
        const f32 cy = header.center_y();
        const vec2 a{cx - 5.0f, cy - 2.0f + 3.0f * t};
        const vec2 b{cx, cy + 3.0f - 3.0f * t};
        const vec2 dd{cx + 5.0f, cy - 2.0f + 3.0f * t};
        const vec2 tri[3] = {a, b, dd};
        u.draw_polygon(std::span<const vec2>(tri, 3), st.chevron);
    }
    if (in.focused) detail::focus_ring(u, header, st.radius);

    // the content area animates its height and clips
    const f32 ch = p.content_h * t;
    content_ = rect::make(body.x, header.bottom(), body.w, ch);
    if (ch > 0.0f && detail::clip_push(c, rect::intersect(content_, header))) pushed_clip_ = true;
}

accordion_scope::~accordion_scope()
{
    if (pushed_clip_ && u_ && u_->ctx) detail::clip_pop(u_->ctx);
}

drawer_scope::drawer_scope(ui &u, rect host, bool &open, const drawer_props &p)
{
    u_ = &u;
    id_ = p.id;
    open_ = open;
    context *c = u.ctx;
    drawer_style st = c->active_theme.drawer;
    if (p.style.bg.set) st.bg = p.style.bg.value;
    if (p.style.border.set) st.border = p.style.border.value;
    if (p.style.scrim.set) st.scrim = p.style.scrim.value;
    if (p.style.radius.set) st.radius = p.style.radius.value;

    const f32 t = u.animate(id_child(p.id, "t"_id), open ? 1.0f : 0.0f,
                            tween{st.anim.duration, st.anim.curve});
    // the scrim captures clicks behind the drawer (never in the Tab ring)
    if (t > 0.0f && p.scrim)
    {
        const interaction scrim = u.interact(id_child(p.id, "scrim"_id), host, true, false);
        u.draw_rect(host,
                    color{st.scrim.r, st.scrim.g, st.scrim.b, static_cast<u8>(st.scrim.a * t)});
        if (scrim.clicked && p.close_on_scrim_click && open)
        {
            open = false;
            open_ = false;
            toggled_ = true;
        }
    }

    const f32 w = p.width;
    const f32 slide = (p.edge == drawer_edge::LEFT) ? -w * (1.0f - t) : w * (1.0f - t);
    const f32 x = (p.edge == drawer_edge::LEFT) ? host.x + slide : host.right() - w + slide;
    const rect panel = rect::make(x, host.y, w, host.h);
    u.draw_rounded_rect(panel, st.bg, st.radius);
    if (st.border.a > 0) detail::rounded_ring(u, panel, st.border, st.radius, 1.0f);
    content_ = panel.pad(c->active_theme.padding);
    if (t > 0.0f && detail::clip_push(c, rect::intersect(content_, host))) pushed_clip_ = true;
}

drawer_scope::~drawer_scope()
{
    if (pushed_clip_ && u_ && u_->ctx) detail::clip_pop(u_->ctx);
}

// ---- toast host -------------------------------------------------------------

void toast_host::push(const char *text, u32 kind)
{
    // Reuse a free slot; if full, drop the OLDEST (smallest serial).
    toast_item *slot = nullptr;
    for (toast_item &it : items)
        if (!it.used)
        {
            slot = &it;
            break;
        }
    if (!slot)
    {
        slot = &items[0];
        for (toast_item &it : items)
            if (it.serial < slot->serial) slot = &it;
    }
    std::snprintf(slot->text, sizeof(slot->text), "%s", text ? text : "");
    slot->kind = kind;
    slot->serial = next_serial++;
    slot->born = 0.0;
    slot->stamped = false;
    slot->used = true;
}

i32 toast_host::alive() const
{
    i32 n = 0;
    for (const toast_item &it : items)
        if (it.used) ++n;
    return n;
}

void toast_draw(ui &u, rect anchor, toast_host &host, const toast_props &p)
{
    toast_style st = u.th().toast;
    if (p.style.bg.set) st.bg = p.style.bg.value;
    if (p.style.info.set) st.info = p.style.info.value;
    if (p.style.success.set) st.success = p.style.success.value;
    if (p.style.danger.set) st.danger = p.style.danger.value;
    if (p.style.text.set) st.text = p.style.text.value;
    if (p.style.width.set) st.width = p.style.width.value;
    if (p.style.height.set) st.height = p.style.height.value;
    if (p.style.gap.set) st.gap = p.style.gap.value;
    if (p.style.radius.set) st.radius = p.style.radius.value;
    if (p.style.lifetime.set) st.lifetime = p.style.lifetime.value;
    if (p.style.fade.set) st.fade = p.style.fade.value;

    const f64 now = u.ctx->now;
    f32 y = anchor.y;
    // newest first: walk descending serials, each item once
    u64 after = ~static_cast<u64>(0);
    for (i32 n = 0; n < toast_host::MAX_TOASTS; ++n)
    {
        toast_item *newest = nullptr;
        for (toast_item &it : host.items)
            if (it.used && it.serial < after && (newest == nullptr || it.serial > newest->serial))
                newest = &it;
        if (!newest) break;
        after = newest->serial;
        if (!newest->stamped)
        {
            newest->born = now;
            newest->stamped = true;
        }
        const f64 age = now - newest->born;
        if (age > static_cast<f64>(st.lifetime))
        {
            newest->used = false;
            continue;
        }
        const f32 t_in = clampf(static_cast<f32>(age) / st.anim.duration, 0.0f, 1.0f);
        const f32 remaining = static_cast<f32>(static_cast<f64>(st.lifetime) - age);
        const f32 t_out = clampf(remaining / max2(0.01f, st.fade), 0.0f, 1.0f);
        const f32 vis = min2(t_in, t_out);

        const rect box =
            rect::make(anchor.right() - st.width + (1.0f - t_in) * 24.0f, y, st.width, st.height);
        const f32 a = vis;
        u.draw_rounded_rect(box, color{st.bg.r, st.bg.g, st.bg.b, static_cast<u8>(st.bg.a * a)},
                            st.radius);
        const color stripe = newest->kind == 1   ? st.success
                             : newest->kind == 2 ? st.danger
                                                 : st.info;
        u.draw_rounded_rect(rect::make(box.x, box.y, 3.0f, box.h),
                            color{stripe.r, stripe.g, stripe.b, static_cast<u8>(stripe.a * a)},
                            st.radius);
        u.text(
            rect::make(box.x + u.padding(), box.y, max2(0.0f, box.w - u.padding() * 2.0f), box.h),
            newest->text, color{st.text.r, st.text.g, st.text.b, static_cast<u8>(st.text.a * a)},
            ALIGN_LEFT);
        y += st.height + st.gap;
    }
}

// ---- table ------------------------------------------------------------------

table_result table(ui &u, rect area, std::span<const char *const> headers, i32 row_count,
                   function_ref<void(ui &, rect, i32, i32)> cell_draw, const table_props &p)
{
    table_result out{};
    table_style st = u.th().table;
    if (p.style.header_bg.set) st.header_bg = p.style.header_bg.value;
    if (p.style.row_bg.set) st.row_bg = p.style.row_bg.value;
    if (p.style.row_alt.set) st.row_alt = p.style.row_alt.value;
    if (p.style.row_hover.set) st.row_hover = p.style.row_hover.value;
    if (p.style.header_text.set) st.header_text = p.style.header_text.value;
    if (p.style.text.set) st.text = p.style.text.value;
    if (p.style.border.set) st.border = p.style.border.value;
    if (p.style.row_h.set) st.row_h = p.style.row_h.value;
    if (p.style.header_h.set) st.header_h = p.style.header_h.value;
    if (p.style.radius.set) st.radius = p.style.radius.value;

    const f32 row_h = p.row_h > 0.0f ? p.row_h : st.row_h;
    const f32 header_h = p.header_h > 0.0f ? p.header_h : st.header_h;
    const i32 cols = static_cast<i32>(headers.size());
    const f32 col_w = cols > 0 ? area.w / static_cast<f32>(cols) : area.w;

    rect body = area;
    const rect head = body.cut_top(header_h);
    u.draw_rect(head, st.header_bg);
    for (i32 c = 0; c < cols; ++c)
    {
        const rect cell = rect::make(head.x + col_w * static_cast<f32>(c), head.y, col_w, head.h);
        u.text(rect::make(cell.x + 8.0f, cell.y, max2(0.0f, cell.w - 12.0f), cell.h),
               headers[c] ? headers[c] : "", st.header_text, ALIGN_LEFT);
    }
    if (st.border.a > 0)
        u.draw_rect(rect::make(head.x, head.bottom() - 1.0f, head.w, 1.0f), st.border);

    scroll_view sv =
        u.scroll(body, id_child(p.id, "body"_id), scroll_options{0.0f, SCROLL_OVERLAY});
    sv.virtual_list(
        row_count, row_h,
        [&](ui &uu, i32 row, rect row_rect)
        {
            const uiid row_id = id_child(p.id, static_cast<uiid>(row));
            const interaction in = uu.interact(row_id, row_rect, p.enabled);
            if (in.hovered && p.enabled) uu.set_cursor(uu.th().button_cursor);
            bool keyboard = false;
            if (in.focused && p.enabled &&
                (uu.key_pressed(key::ENTER) || uu.key_pressed(key::SPACE)))
            {
                uu.ctx->key_pressed[static_cast<i32>(key::ENTER)] = false;
                uu.ctx->key_pressed[static_cast<i32>(key::SPACE)] = false;
                keyboard = true;
            }
            if ((in.clicked || keyboard) && p.enabled)
            {
                out.clicked_row = row;
                // the column comes from the click position (keyboard: the first)
                out.clicked_col =
                    in.clicked
                        ? static_cast<i32>((uu.ctx->mouse_x - row_rect.x) / max2(1.0f, col_w))
                        : 0;
                out.clicked_col = out.clicked_col < 0       ? 0
                                  : out.clicked_col >= cols ? cols - 1
                                                            : out.clicked_col;
            }
            if (in.focused) out.focused_row = row;

            const color bg = in.hovered ? st.row_hover : (row % 2 ? st.row_alt : st.row_bg);
            uu.draw_rect(row_rect, bg);
            for (i32 c = 0; c < cols; ++c)
            {
                const rect cell = rect::make(row_rect.x + col_w * static_cast<f32>(c), row_rect.y,
                                             col_w, row_rect.h);
                if (cell_draw)
                    cell_draw(uu,
                              rect::make(cell.x + 8.0f, cell.y, max2(0.0f, cell.w - 12.0f), cell.h),
                              row, c);
            }
        });
    sv.set_content_height(static_cast<f32>(row_count) * row_h);
    return out;
}

// ---- command palette --------------------------------------------------------

static inline bool palette_match(const std::string &query, const char *name)
{
    if (query.empty() || !name) return true;
    // case-insensitive substring
    const usize n = query.size();
    if (n == 0) return true;
    for (const char *p = name; *p; ++p)
    {
        usize i = 0;
        while (i < n && p[i])
        {
            const char a =
                query[i] >= 'A' && query[i] <= 'Z' ? static_cast<char>(query[i] + 32) : query[i];
            const char b = p[i] >= 'A' && p[i] <= 'Z' ? static_cast<char>(p[i] + 32) : p[i];
            if (a != b) break;
            ++i;
        }
        if (i == n) return true;
    }
    return false;
}

palette_result command_palette(ui &u, rect screen, bool &open, palette_state &st,
                               std::span<const palette_command> commands, const palette_props &p)
{
    palette_result out{};
    if (!open) return out;
    palette_style sty = u.th().palette;
    if (p.style.scrim.set) sty.scrim = p.style.scrim.value;
    if (p.style.bg.set) sty.bg = p.style.bg.value;
    if (p.style.border.set) sty.border = p.style.border.value;
    if (p.style.text.set) sty.text = p.style.text.value;
    if (p.style.hint.set) sty.hint = p.style.hint.value;
    if (p.style.selected.set) sty.selected = p.style.selected.value;
    if (p.style.accent.set) sty.accent = p.style.accent.value;
    if (p.style.width.set) sty.width = p.style.width.value;
    if (p.style.item_h.set) sty.item_h = p.style.item_h.value;
    if (p.style.radius.set) sty.radius = p.style.radius.value;

    // the scrim (never in the Tab ring) + the panel
    const interaction scrim = u.interact(id_child(p.id, "scrim"_id), screen, true, false);
    u.draw_rect(screen, sty.scrim);
    if (scrim.clicked) open = false;

    const i32 shown_max = 8;
    const f32 panel_h = 52.0f + sty.item_h * static_cast<f32>(shown_max) + 8.0f;
    const rect panel = rect::make(screen.center_x() - sty.width * 0.5f, screen.y + screen.h * 0.18f,
                                  sty.width, min2(panel_h, screen.h * 0.7f));
    u.draw_rounded_rect(panel, sty.bg, sty.radius);
    u.draw_rounded_rect(rect::make(panel.x, panel.y, 3.0f, panel.h), sty.accent, sty.radius);
    if (sty.border.a > 0) detail::rounded_ring(u, panel, sty.border, sty.radius, 1.0f);

    rect body = panel.pad(u.padding() * 0.75f);
    const rect field = body.cut_top(u.control_h());
    body.cut_top(6.0f);

    const uiid field_id = id_child(p.id, "query"_id);
    // keep the query field focused while the palette is open
    if (u.ctx->focus != field_id) u.ctx->focus_request = field_id;
    if (u.text_field(field, st.query, field_id))
    {
        st.active = 0; // typing resets the highlight
    }

    // Escape closes, Up/Down move, Enter chooses
    if (u.key_pressed(key::ESCAPE))
    {
        u.ctx->key_pressed[static_cast<i32>(key::ESCAPE)] = false;
        open = false;
    }
    const i32 total = static_cast<i32>(commands.size());
    i32 shown = 0;
    for (i32 i = 0; i < total; ++i)
        if (palette_match(st.query, commands[static_cast<usize>(i)].name)) ++shown;
    out.shown = shown;
    if (shown > 0)
    {
        if (u.key_pressed(key::DOWN))
        {
            u.ctx->key_pressed[static_cast<i32>(key::DOWN)] = false;
            st.active = (st.active + 1) % shown;
        }
        if (u.key_pressed(key::UP))
        {
            u.ctx->key_pressed[static_cast<i32>(key::UP)] = false;
            st.active = (st.active + shown - 1) % shown;
        }
    }
    else
    {
        st.active = 0;
    }

    // window of `shown_max` entries around the highlight
    i32 first = 0;
    if (shown > shown_max)
    {
        first = st.active - shown_max / 2;
        first = first < 0 ? 0 : first;
        if (first + shown_max > shown) first = shown - shown_max;
    }

    i32 rank = -1;
    i32 drawn = 0;
    for (i32 i = 0; i < total && drawn < shown_max; ++i)
    {
        const palette_command &cmd = commands[static_cast<usize>(i)];
        if (!palette_match(st.query, cmd.name)) continue;
        ++rank;
        if (rank < first) continue;
        const rect item = body.cut_top(sty.item_h);
        const uiid item_id = id_child(p.id, static_cast<uiid>(i));
        const interaction in = u.interact(item_id, item, true, false);
        if (in.hovered)
        {
            st.active = rank;
            u.set_cursor(u.th().button_cursor);
        }
        const bool hl = (rank == st.active);
        if (hl) u.draw_rounded_rect(item.pad(0.0f, 1.0f), sty.selected, sty.radius * 0.6f);
        u.text(item.pad(10.0f, 0.0f), cmd.name ? cmd.name : "", sty.text, ALIGN_LEFT);
        if (cmd.hint && cmd.hint[0]) u.text(item.pad(10.0f, 0.0f), cmd.hint, sty.hint, ALIGN_RIGHT);
        if (in.clicked)
        {
            out.chosen = i;
            open = false;
        }
        ++drawn;
    }
    if (u.key_pressed(key::ENTER) && shown > 0)
    {
        u.ctx->key_pressed[static_cast<i32>(key::ENTER)] = false;
        // the active entry's ORIGINAL index
        i32 rank2 = -1;
        for (i32 i = 0; i < total; ++i)
        {
            if (!palette_match(st.query, commands[static_cast<usize>(i)].name)) continue;
            ++rank2;
            if (rank2 == st.active)
            {
                out.chosen = i;
                open = false;
                break;
            }
        }
    }
    out.active = shown > 0 ? st.active : -1;
    return out;
}

} // namespace comp

void ui::card(rect r, card_override ov)
{
    const card_style s = merge_style(ctx->active_theme.card, ov);
    if (s.border_thickness > 0.0f && s.border.a > 0 && s.bg.a >= 250)
    {
        draw_rounded_rect(r, s.border, s.radius);
        draw_rounded_rect(r.pad(s.border_thickness), s.bg,
                          max2(0.0f, s.radius - s.border_thickness));
    }
    else
    {
        if (s.bg.a > 0) draw_rounded_rect(r, s.bg, s.radius);
        detail::rounded_ring(*this, r, s.border, s.radius, s.border_thickness);
    }
}

namespace detail
{
inline void thick_line(ui &u, vec2 a, vec2 b, f32 thickness, color c)
{
    vec2 d{b.x - a.x, b.y - a.y};
    const f32 l = max2(std::sqrt(d.x * d.x + d.y * d.y), 0.001f);
    const vec2 n{-d.y / l * thickness * 0.5f, d.x / l * thickness * 0.5f};
    const vec2 pts[4] = {{a.x + n.x, a.y + n.y},
                         {b.x + n.x, b.y + n.y},
                         {b.x - n.x, b.y - n.y},
                         {a.x - n.x, a.y - n.y}};
    u.draw_polygon(std::span<const vec2>(pts, 4), c);
}
} // namespace detail

bool ui::checkbox(rect r, std::string_view label, bool &value, uiid id)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    interaction in = interact(id, r);
    if (in.hovered) c->want_cursor = t.button_cursor;

    const f32 box = min2(18.0f, r.h);
    const rect box_r{r.x, r.y + (r.h - box) * 0.5f, box, box};
    const color bg = value ? t.accent : (in.hovered ? t.widget_hover : t.widget_bg);
    draw_rounded_rect(box_r, bg, max2(0.0f, t.radius * 0.75f));
    if (value)
    {
        detail::thick_line(*this, {box_r.x + box * 0.22f, box_r.y + box * 0.52f},
                           {box_r.x + box * 0.44f, box_r.y + box * 0.74f}, 2.0f, t.bg);
        detail::thick_line(*this, {box_r.x + box * 0.44f, box_r.y + box * 0.74f},
                           {box_r.x + box * 0.80f, box_r.y + box * 0.26f}, 2.0f, t.bg);
    }
    if (!label.empty())
        text(rect::make(box_r.right() + 8.0f, r.y, max2(0.0f, r.w - box - 8.0f), r.h), label,
             t.text, ALIGN_LEFT);

    // Keyboard activation: Enter or Space toggles the focused checkbox.
    bool keyboard_toggle = false;
    if (in.focused)
    {
        detail::focus_ring(*this, box_r, max2(0.0f, t.radius * 0.75f));
        if (ctx->key_pressed[static_cast<i32>(key::ENTER)] ||
            ctx->key_pressed[static_cast<i32>(key::SPACE)])
        {
            ctx->key_pressed[static_cast<i32>(key::ENTER)] = false;
            ctx->key_pressed[static_cast<i32>(key::SPACE)] = false;
            keyboard_toggle = true;
        }
    }

    if (in.clicked || keyboard_toggle)
    {
        value = !value;
        return true;
    }
    return false;
}

// ---- custom window chrome ---------------------------------------------------
namespace detail
{
inline constexpr f32 TITLEBAR_H = 34.0f;
inline constexpr f32 TITLE_BTN_W = 36.0f;
inline constexpr f32 CLOSE_BTN_W = 46.0f;

// Aero-style edge snap applied on titlebar-drag release: left/right = the
// work-area half, top = maximize. The zone is re-derived from the release
// point against the display the pointer is on, so a drag near a shared edge
// between two monitors snaps to the screen the user aimed at, not the one the
// window happens to be counted on. No-op without a host or a known work area.
// (Defined with the other snap-assistant helpers below, outside `detail`.)

// A flat glyph button on the titlebar (hover highlight + hand cursor).
inline bool title_button(ui &u, uiid id, rect r, u8 glyph, bool maximized)
{
    interaction in = u.interact(id, r);
    if (in.hovered) u.ctx->want_cursor = CURSOR_HAND;
    if (in.hovered) u.draw_rect(r, u.th().widget_hover);
    const theme &t = u.th();
    const color ic = in.hovered ? t.text : t.text_dim;
    const f32 cx = r.center_x(), cy = r.center_y();
    if (glyph == 2) // close: an X
    {
        detail::thick_line(u, {cx - 4.5f, cy - 4.5f}, {cx + 4.5f, cy + 4.5f}, 2.0f, ic);
        detail::thick_line(u, {cx + 4.5f, cy - 4.5f}, {cx - 4.5f, cy + 4.5f}, 2.0f, ic);
    }
    else if (glyph == 1) // maximize / restore
    {
        if (maximized)
        {
            const rect back{cx - 4.0f, cy - 1.5f, 9.0f, 6.5f};
            const rect front{cx - 2.5f, cy - 4.0f, 9.0f, 6.5f};
            detail::rounded_ring(u, back, ic, 0.0f, 1.0f);
            detail::rounded_ring(u, front, ic, 0.0f, 1.0f);
        }
        else
        {
            detail::rounded_ring(u, rect{cx - 4.5f, cy - 4.5f, 9.0f, 9.0f}, ic, 0.0f, 1.0f);
        }
    }
    else
    {
        u.draw_rect(rect{cx - 4.5f, cy + 3.0f, 9.0f, 1.5f}, ic);
    }
    return in.clicked;
}
} // namespace detail

titlebar_result ui::titlebar(window &w, rect &bounds, std::string_view title)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    titlebar_result out;
    const rect bar = bounds.cut_top(detail::TITLEBAR_H);

    // The drag strip is the bar minus the buttons: the strips never overlap
    // the buttons' rects (the stolen-press guardrail).
    const f32 buttons_w = detail::CLOSE_BTN_W + detail::TITLE_BTN_W * 2.0f;
    const rect drag = rect::make(bar.x, bar.y, max2(0.0f, bar.w - buttons_w), bar.h);
    const uiid root = w.root; // window-scoped ids: two windows never collide
    interaction in = interact(id_child(root, "titlebar"_id), drag);
    const bool maximized = w.maximized;

    draw_rect(bar, t.panel_bg);
    if (in.hovered) draw_rect(drag, t.widget_hover);
    draw_rect(rect::make(bar.x, bar.bottom() - 1.0f, bar.w, 1.0f), t.border);
    if (!title.empty())
    {
        const rect label = drag.pad(12.0f, 0.0f);
        text_scope ts = text_style(13.0f, t.font);
        text_ellipsis(label, title, in.hovered ? t.text : t.text_dim, ALIGN_LEFT);
    }

    // The three glyph buttons, flush with the bar's right edge.
    const rect close_r =
        rect::make(bar.right() - detail::CLOSE_BTN_W, bar.y, detail::CLOSE_BTN_W, bar.h);
    const rect max_r =
        rect::make(close_r.x - detail::TITLE_BTN_W, bar.y, detail::TITLE_BTN_W, bar.h);
    const rect min_r = rect::make(max_r.x - detail::TITLE_BTN_W, bar.y, detail::TITLE_BTN_W, bar.h);
    if (detail::title_button(*this, id_child(root, "tb_min"_id), min_r, 0, maximized))
    {
        out.minimize_clicked = true;
        if (c->host) c->host->minimize_window(w);
    }
    if (detail::title_button(*this, id_child(root, "tb_max"_id), max_r, 1, maximized))
    {
        out.maximize_clicked = true;
        if (c->host)
        {
            c->host->toggle_maximize(w);
        }
    }
    if (detail::title_button(*this, id_child(root, "tb_close"_id), close_r, 2, maximized))
    {
        out.close_clicked = true;
        w.close_requested = true;
    }
    return out;
}

bool ui::slider_float(rect r, std::string_view label, f32 &value, f32 min_value, f32 max_value,
                      uiid id, const char *fmt)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    interaction in = interact(id, r);
    if (in.hovered || in.held) c->want_cursor = CURSOR_HRESIZE;

    const f32 lo = min_value, hi = max_value;
    const f32 range = max2(hi - lo, 0.0001f);
    bool changed = false;
    // Keyboard: a focused slider adjusts with Left/Right (5% of the range per
    // press, Shift = 1%). Consumed so a later widget never sees the press.
    if (in.focused && (ctx->key_pressed[static_cast<i32>(key::LEFT)] ||
                       ctx->key_pressed[static_cast<i32>(key::RIGHT)]))
    {
        const f32 step = range * (c->shift_down ? 0.01f : 0.05f);
        const f32 dir = ctx->key_pressed[static_cast<i32>(key::RIGHT)] ? step : -step;
        ctx->key_pressed[static_cast<i32>(key::LEFT)] = false;
        ctx->key_pressed[static_cast<i32>(key::RIGHT)] = false;
        const f32 v = clampf(value + dir, lo, hi);
        if (v != value)
        {
            value = v;
            changed = true;
        }
    }
    // Jump on the press edge too: a fast click (press+release inside one frame)
    // clears `active` within interact, so testing `active` alone would silently
    // drop the click-to-jump (the drag-anchor guardrail).
    if (in.activated || (c->active == id && c->mouse_down))
    {
        const f32 v = clampf(lo + (c->mouse_x - r.x) / max2(r.w, 1.0f) * range, lo, hi);
        if (v != value)
        {
            value = v;
            changed = true;
        }
    }

    const f32 frac = clampf((value - lo) / range, 0.0f, 1.0f);
    draw_rounded_rect(r, t.widget_bg, t.radius);
    if (frac > 0.0f)
        draw_rounded_rect(rect::make(r.x, r.y, r.w * frac, r.h),
                          in.held ? t.accent_hover : t.accent, t.radius);

    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt ? fmt : "%.1f", static_cast<double>(value));
    const rect inner = r.pad(6.0f, 0.0f);
    if (label.empty())
        text(inner, buf, t.text, ALIGN_CENTER);
    else
    {
        text(inner, label, t.text, ALIGN_LEFT);
        text(inner, buf, t.text, ALIGN_RIGHT);
    }
    if (in.focused) detail::focus_ring(*this, r, t.radius);
    return changed;
}

void ui::progress_bar(rect r, f32 fraction, color fill, color bg)
{
    draw_rounded_rect(r, bg, ctx->active_theme.radius);
    const f32 f = clampf(fraction, 0.0f, 1.0f);
    if (f > 0.0f)
        draw_rounded_rect(rect::make(r.x, r.y, r.w * f, r.h), fill, ctx->active_theme.radius);
}

// ---- combo / tooltip / context menu ----------------------------------------
namespace detail
{
// The shared menu-drawing used by the combo dropdown and context menus: a
// rounded panel with one hit rect per item, hover highlight, and mouse hover
// moving the keyboard highlight. `hot` is advanced in place; returns the picked
// index or -1. Called while the enclosing popup layer is open, so its items are
// allowed to interact.
inline i32 menu_items(ui &u, uiid id, rect area, const char *const *labels, i32 count, i32 &hot,
                      const theme &t, bool fresh_open)
{
    const f32 pad = 6.0f;
    const f32 row_h = 26.0f;

    // The viewport clips: with the height clamped (a combo dropdown capped by
    // max_popup_height, or a menu near the screen edge), rows beyond the fold
    // are neither drawn nor hoverable. SCROLL_OVERLAY keeps the bar floating
    // over the clipped rows instead of re-reserving width, and the bar draws
    // after the rows so a row can never steal its press (hit-rect ordering).
    scroll_view sv = u.scroll(area.pad(pad), id, scroll_options{2.0f, SCROLL_OVERLAY});
    if (fresh_open) sv.scroll_to(0.0f); // reopening starts at the top
    // Pre-set the content height before laying out: the rows column then sees
    // the full list height on the very first frame (the r17 first-frame fix)
    // instead of clamping against an empty viewport.
    sv.set_content_height(static_cast<f32>(count) * row_h + static_cast<f32>(count - 1) * 2.0f);
    u.draw_rounded_rect(area, t.panel_bg, 6.0f);
    u.draw_rect(rect::make(area.x, area.y, area.w, 2.0f), t.accent);

    i32 picked = -1;
    column rows(sv.content(), 2.0f);
    for (i32 i = 0; i < count && picked < 0; ++i)
    {
        const uiid item_id = id_child(id, static_cast<uiid>(0xC0DE + static_cast<uiid>(i)));
        const rect row = rows.next(row_h);
        interaction in = u.interact(item_id, row);
        if (in.hovered) u.set_cursor(CURSOR_HAND);
        if (in.hovered) hot = i; // mouse and keyboard share one highlight
        if (hot == i && count > 1) u.draw_rounded_rect(row, t.widget_hover, 4.0f);
        u.text(row.pad(8.0f, 0.0f), labels[i], t.text, ALIGN_LEFT);
        if (in.clicked) picked = i;
    }
    return picked;
}
} // namespace detail

bool ui::combo(rect r, std::string_view label, const char *const *items, i32 item_count,
               i32 &selected, uiid id, f32 max_popup_height)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    const uiid drop_id = id_child(id, "dropdown"_id);

    // Was the dropdown open last frame? (popup entries carry over per window)
    bool was_open = false;
    for (i32 i = 0; i < c->prev_popup_depth; ++i)
        if (c->prev_popups[i].id == drop_id) was_open = true;

    interaction in = interact(id, r);
    if (in.hovered) c->want_cursor = t.button_cursor;

    // Closed state: the current item, left-aligned, chevron at the right.
    draw_rounded_rect(r,
                      in.held      ? t.widget_active
                      : in.hovered ? t.widget_hover
                                   : t.widget_bg,
                      t.radius);
    if (t.border_thickness > 0.0f) detail::rounded_ring(*this, r, t.border, t.radius, 1.0f);
    if (in.focused) detail::focus_ring(*this, r, t.radius);
    const char *shown = (selected >= 0 && selected < item_count) ? items[selected] : "";
    const f32 chev_w = 22.0f;
    text(rect::make(r.x + 10.0f, r.y, max2(0.0f, r.w - chev_w - 10.0f), r.h), shown, t.text,
         ALIGN_LEFT);
    if (!label.empty())
        text(rect::make(r.x, r.y, max2(0.0f, r.w - chev_w - 10.0f), r.h), label, t.text_dim,
             ALIGN_RIGHT);
    {
        // Hairpin chevron at the right edge.
        const f32 cx = r.right() - chev_w * 0.5f, cy = r.center_y();
        const color cc = in.hovered ? t.text : t.text_dim;
        detail::thick_line(*this, {cx - 4.0f, cy - 1.5f}, {cx, cy + 2.5f}, 3.0f, cc);
        detail::thick_line(*this, {cx, cy + 2.5f}, {cx + 4.0f, cy - 1.5f}, 3.0f, cc);
    }
    PUFFERUI_CHECK(VIOL_COMBO_EMPTY, item_count > 0, "combo needs at least one item");
    if (item_count <= 0) return false;
    if (selected < 0) selected = 0;
    if (selected >= item_count) selected = item_count - 1;

    // Open on header click — or on Enter when the header owns keyboard focus
    // (the dropdown's own Up/Down/Enter navigation takes over once open).
    // Closing happens elsewhere: a press on the header while open lands
    // *outside* the dropdown rect, so the click-outside rule marks it next
    // frame and the deferred pass closes it - no toggle branch needed here.
    // Keyboard events ring while the dropdown is open.
    bool keyboard_open = false;
    if (in.focused && ctx->key_pressed[static_cast<i32>(key::ENTER)])
    {
        ctx->key_pressed[static_cast<i32>(key::ENTER)] = false;
        keyboard_open = true;
    }
    bool open = was_open || in.clicked || keyboard_open;
    if (in.clicked || keyboard_open) c->combo_hot = selected; // keyboard starts at the current item

    // A pick resolved in the *previous* frame's deferred pass reports here
    // and lands in the model atomically with the `changed` report — no
    // write-back pointer into caller storage exists anymore.
    bool changed = (c->defer_result_id == drop_id);
    if (c->defer_result_id == drop_id)
    {
        selected = c->defer_result_pick;
        c->defer_result_id = 0; // consumed
    }

    // Keyboard on the dropdown: Up/Down move the *highlight* (Enter commits),
    // Escape closes. A header click that opened *this* frame skips this: the
    // same press must not double as a pick or a close.
    if (open && was_open)
    {
        if (c->key_pressed[static_cast<i32>(key::DOWN)])
            c->combo_hot = (c->combo_hot + 1) % item_count;
        if (c->key_pressed[static_cast<i32>(key::UP)])
            c->combo_hot = (c->combo_hot + item_count - 1) % item_count;
        if (c->key_pressed[static_cast<i32>(key::ENTER)])
        {
            if (c->combo_hot >= 0 && c->combo_hot < item_count && c->combo_hot != selected)
            {
                selected = c->combo_hot;
                changed = true;
            }
            open = false;
        }
        if (c->key_pressed[static_cast<i32>(key::ESCAPE)]) open = false;
        // An outside press (anywhere but the dropdown) closes.
        if (c->popup_click_outside && c->popup_click_id == drop_id) open = false;
    }

    // Dropdown pass: the surface is deferred to end_frame, where it draws above
    // every base widget and resolves mouse picks (immediate mode paints in call
    // order; a popup surface drawn here would sit under anything after it).
    if (open)
    {
        // Size from content: every item must fit inside the panel. Width is the
        // widest label (at least the header), height is the clamped list.
        const f32 pad = 6.0f;
        const f32 row_h = 26.0f;
        const f32 gap = 2.0f;
        f32 widest = text_width(shown) + chev_w; // never narrower than the header
        for (i32 i = 0; i < item_count; ++i) widest = max2(widest, text_width(items[i]) + 16.0f);
        const f32 list_h = item_count * row_h + static_cast<f32>(item_count - 1) * gap;
        const f32 h =
            clampf(list_h + pad * 2.0f, row_h + pad * 2.0f, max2(row_h, max_popup_height));
        const rect screen = c->screen;
        const f32 w = clampf(widest, r.w, screen.w);
        // Below the header by default; flip above when there is no room below.
        f32 x = clampf(r.x, screen.x, max2(screen.x, screen.right() - w));
        f32 y = r.bottom() + 4.0f;
        if (y + h > screen.bottom() && r.y - h - 4.0f >= screen.y)
            y = r.y - h - 4.0f; // above the header
        y = min2(y, max2(screen.y, screen.bottom() - h));

        // One menu surface at a time: taking the slot closes whatever held it.
        if (c->defer_menu_id != 0 && c->defer_menu_id != drop_id)
        {
            c->defer_menu_id = 0;
            c->defer_count = 0;
        }
        c->defer_menu_id = drop_id;
        c->defer_menu = rect::make(x, y, w, h);
        defer_set_labels(c, items, item_count);
        c->defer_fresh = !was_open;
    }
    return changed;
}

rect ui::tooltip(rect anchor, uiid id, std::string_view tip)
{
    context *c = ctx;
    if (c->hot != id) return rect{}; // only while the anchor widget is hovered

    // Delay queue: the first tooltip waits 0.5 s; another tooltip seen within
    // 1.2 s of the last one opens instantly (moving between toolbar buttons).
    const f64 now = c->now;
    static constexpr f64 TOOLTIP_DELAY = 0.5;
    const bool recent = (c->tip_last_shown >= 0.0) && (now - c->tip_last_shown) < 1.2;
    if (!recent && c->tip_last_id != id)
    {
        // Wait the delay out on first hover of this widget (stateless per
        // widget: the wait clock restarts whenever the queue turns over).
        if (c->tip_stuck_id != id)
        {
            c->tip_stuck_id = id;
            c->tip_wait_start = now;
        }
        if (now - c->tip_wait_start < TOOLTIP_DELAY) return rect{};
    }
    c->tip_last_id = id;
    c->tip_last_shown = now;

    const theme &t = c->active_theme;
    const f32 tw = text_width(tip) + 16.0f;
    const f32 th = line_height() + 10.0f;
    // Under the anchor by default; flip above when there is no room below.
    f32 x = anchor.center_x() - tw * 0.5f;
    f32 y = anchor.bottom() + 9.0f;
    const rect screen = c->screen;
    x = clampf(x, screen.x + 4.0f, max2(screen.x + 4.0f, screen.right() - tw - 4.0f));
    if (y + th > screen.bottom()) y = max2(screen.y + 4.0f, anchor.y - th - 9.0f);
    const rect tip_rect = rect::make(x, y, tw, th);
    if (!tip_rect.is_valid() || tip_rect.w < 8.0f) return rect{}; // degenerate anchor

    // The paint defers to end_frame: drawn after every base widget, a tooltip
    // can never be covered by later-drawn content (only one at a time; the
    // last call this frame wins).
    c->defer_tip = tip_rect;
    const usize copy = min2(tip.size(), sizeof(c->defer_tip_text) - 1);
    std::memcpy(c->defer_tip_text, tip.data(), copy);
    c->defer_tip_text[copy] = '\0';
    return tip_rect;
}

i32 ui::context_menu(uiid id, rect anchor, const char *const *item_labels, i32 item_count)
{
    context *c = ctx;
    const theme &t = c->active_theme;
    const uiid menu_id = id_child(id, "menu"_id);

    // Was the menu open last frame?
    bool open = false;
    for (i32 i = 0; i < c->prev_popup_depth; ++i)
        if (c->prev_popups[i].id == menu_id) open = true;
    const bool fresh_open = !open; // opening this frame: reset the item scroll

    // Open on right-press over the anchor. The press that opened must not also
    // be seen by the menu's items: menu_items only reads clicks, and the press/
    // release pair completes before the menu paints, so a same-frame open is
    // safe (the click needs press+release on the same item).
    if (!open && c->right_pressed)
    {
        // Right-press opens only when it lands over the anchor; test the
        // position directly so an in-progress drag is never disturbed.
        if (anchor.contains(c->mouse_x, c->mouse_y) && c->active == 0)
        {
            open = true;
            c->context_menu_x = c->mouse_x;
            c->context_menu_y = c->mouse_y;
        }
    }

    i32 picked = -1;
    // A pick resolved in the previous frame's deferred pass reports here.
    if (c->defer_result_id == menu_id)
    {
        picked = c->defer_result_pick;
        c->defer_result_id = 0; // consumed
    }
    if (open)
    {
        // Close on escape or outside press (recorded last frame).
        if (c->key_pressed[static_cast<i32>(key::ESCAPE)])
            open = false;
        else if (c->popup_click_outside && c->popup_click_id == menu_id)
            open = false;
    }

    if (open)
    {
        // Measure the menu: widest label, one row each; goes under the pointer.
        f32 tw = 0.0f;
        f32 rows_h = 0.0f;
        for (i32 i = 0; i < item_count; ++i)
        {
            tw = max2(tw, text_width(item_labels[i]));
            rows_h += 26.0f;
        }
        if (item_count > 1) rows_h += static_cast<f32>(item_count - 1) * 2.0f;
        const f32 pad = 6.0f;
        const f32 menu_w = tw + pad * 2.0f + 16.0f;
        const f32 menu_h = rows_h + pad * 2.0f;
        const rect screen = c->screen;
        f32 x = c->context_menu_x;
        f32 y = c->context_menu_y;
        x = clampf(x, screen.x + 2.0f, max2(screen.x + 2.0f, screen.right() - menu_w - 2.0f));
        y = clampf(y, screen.y + 2.0f, max2(screen.y + 2.0f, screen.bottom() - menu_h - 2.0f));

        // One menu surface at a time: taking the slot closes whatever held it.
        if (c->defer_menu_id != 0 && c->defer_menu_id != menu_id)
        {
            c->defer_menu_id = 0;
            c->defer_count = 0;
        }
        c->defer_menu_id = menu_id;
        c->defer_menu = rect::make(x, y, menu_w, menu_h);
        defer_set_labels(c, item_labels, item_count);
        c->defer_fresh = fresh_open;
    }
    return picked;
}

panel_scope ui::panel(std::string_view title, rect &bounds, const panel_opts &opts)
{
    return panel(title, bounds, opts.flags, opts.dock_panel, opts.dock_name, opts.style);
}

panel_scope ui::panel(std::string_view title, rect &bounds, u32 flags, uiid dock_panel,
                      const char *dock_name, panel_override ov)
{
    const panel_style s = merge_style(ctx->active_theme.panel, ov);
    const uiid id = hash(title);
    const bool no_titlebar = (flags & PANEL_NO_TITLEBAR) != 0;
    const bool no_controls = (flags & PANEL_NO_CONTROLS) != 0;
    const bool no_drag = (flags & PANEL_NO_DRAG) != 0;
    const bool no_shadow = (flags & PANEL_NO_SHADOW) != 0;
    const f32 titlebar_h = no_titlebar ? 0.0f : s.titlebar_h;

    // Enter this panel's input layer *before* any of its own widgets (titlebar,
    // close) are tested, so the panel does not block itself.
    bool layer_pushed = false;
    detail::ensure_panel_stack_capacity(ctx, ctx->panel_stack_depth + 1);
    ctx->panel_stack[ctx->panel_stack_depth++] = id;
    layer_pushed = true;

    // Control hit rect (hit-testing only) so the titlebar drag does not steal a
    // press that landed on the close dot. The titlebar keeps its full width.
    rect close_hit{};
    if (!no_controls && titlebar_h > 0.0f)
    {
        const rect inner0 = bounds.pad(s.border_thickness);
        const rect tb0 = {inner0.x, inner0.y, inner0.w, titlebar_h};
        close_hit = {tb0.right() - 10.0f - 12.0f, tb0.y + (tb0.h - 12.0f) * 0.5f, 12.0f, 12.0f};
    }
    const bool over_close =
        (!no_controls && titlebar_h > 0.0f) && close_hit.contains(ctx->mouse_x, ctx->mouse_y);

    // dragging (titlebar), clamped so a strip always stays reachable
    if (!no_drag && titlebar_h > 0.0f)
    {
        const rect title_rect = {bounds.x, bounds.y, bounds.w, titlebar_h};
        interaction inh = interact(id_child(id, 1), title_rect, !over_close);
        if (inh.hovered) ctx->want_cursor = CURSOR_HAND;
        if (ctx->dragging_panel == id)
        {
            if (ctx->mouse_down)
            {
                bounds.x = ctx->mouse_x - ctx->drag_dx;
                bounds.y = ctx->mouse_y - ctx->drag_dy;
                // Clamp to the virtual desktop (in this window's local space) so a
                // floating panel can be dragged from one window to another.
                const rect desk = detail::desktop_local(ctx);
                if (desk.w > 1.0f && desk.h > 1.0f)
                {
                    const f32 min_vis = min2(60.0f, bounds.w);
                    bounds.x =
                        clampf(bounds.x, desk.x - bounds.w + min_vis, desk.right() - min_vis);
                    bounds.y = clampf(bounds.y, desk.y, desk.bottom() - min_vis);
                }
                // A dockable floating panel participates in the dock drag, so it
                // can be dropped onto the dock space like a tab.
                if (dock_panel != 0)
                {
                    if (ctx->dock_panel == 0)
                    {
                        ctx->dock_panel = dock_panel;
                        ctx->dock_panel_name = detail::intern_dock_name(ctx, dock_name);
                        ctx->dock_press_x = ctx->mouse_x;
                        ctx->dock_press_y = ctx->mouse_y;
                        ctx->dock_dragging = false;
                    }
                    else
                    {
                        const f32 dx = ctx->mouse_x - ctx->dock_press_x;
                        const f32 dy = ctx->mouse_y - ctx->dock_press_y;
                        if (dx * dx + dy * dy > 16.0f) ctx->dock_dragging = true;
                    }
                }
            }
            else
            {
                ctx->dragging_panel = 0;
            }
        }
        else if (inh.activated)
        {
            ctx->dragging_panel = id;
            ctx->drag_dx = ctx->mouse_x - bounds.x;
            ctx->drag_dy = ctx->mouse_y - bounds.y;
            if (dock_panel != 0)
            {
                ctx->dock_panel = dock_panel;
                ctx->dock_panel_name = detail::intern_dock_name(ctx, dock_name);
                ctx->dock_press_x = ctx->mouse_x;
                ctx->dock_press_y = ctx->mouse_y;
                ctx->dock_dragging = false;
            }
        }
    }

    // Register this panel with the final bounds: next frame these rects block
    // interaction with base UI underneath (one-frame model, like popups).
    detail::ensure_panel_capacity(ctx, ctx->panel_depth + 1);
    ctx->panels[ctx->panel_depth++] =
        panel_entry{id, bounds, ctx->current_window ? ctx->current_window->index : 0};

    // Opaque backgrounds keep the classic two-rect outline; translucent ones use
    // the ring path so the backdrop shows through (frosted glass). A filled
    // shadow rect only reads as a shadow behind an opaque background.
    const bool opaque_bg = s.bg.a >= 250;
    if (s.shadow && !no_shadow && opaque_bg)
        draw_rounded_rect(rect::make(bounds.x + 3.0f, bounds.y + 3.0f, bounds.w, bounds.h),
                          color{0, 0, 0, 70}, s.radius);

    const rect inner = bounds.pad(s.border_thickness);
    if (s.border_thickness > 0.0f && s.border.a > 0 && opaque_bg)
    {
        draw_rounded_rect(bounds, s.border, s.radius);
        draw_rounded_rect(inner, s.bg, max2(0.0f, s.radius - s.border_thickness));
    }
    else
    {
        if (s.bg.a > 0) draw_rounded_rect(bounds, s.bg, s.radius);
        detail::rounded_ring(*this, bounds, s.border, s.radius, s.border_thickness);
    }

    bool close_clicked = false;
    if (titlebar_h > 0.0f)
    {
        const rect tb = {inner.x, inner.y, inner.w, titlebar_h};
        draw_rounded_rect(tb, s.titlebar_bg, max2(0.0f, s.radius - s.border_thickness));
        text(tb.pad(s.padding, 0.0f), title, s.titlebar_text, ALIGN_LEFT);

        if (!no_controls)
        {
            const rect close = {tb.right() - 10.0f - 12.0f, tb.y + (tb.h - 12.0f) * 0.5f, 12.0f,
                                12.0f};
            interaction inx = interact(id_child(id, 2), close);
            if (inx.hovered) ctx->want_cursor = CURSOR_HAND; // the close dot is clickable
            color cc = inx.hovered ? s.close : color{s.close.r, s.close.g, s.close.b, 200};
            draw_rounded_rect(close, cc, 6.0f);
            if (inx.clicked) close_clicked = true;
        }
    }

    const rect client = {inner.x, inner.y + titlebar_h, inner.w, inner.h - titlebar_h};
    const rect content = client.pad(s.padding);
    return panel_scope(*this, id, bounds, client, content, close_clicked, layer_pushed);
}

panel_scope::~panel_scope()
{
    if (!u_ || !layer_pushed) return;
    context *c = u_->ctx;
    if (c->panel_stack_depth > 0) c->panel_stack_depth -= 1;
}

// ---- scroll views ----------------------------------------------------------
namespace detail
{
// The layer checks `interact` performs (popups/panels swallowing input), minus
// side effects; used to decide whether wheel input reaches a scroll view.
inline bool pointer_blocked(context *c, f32 x, f32 y)
{
    if (c->prev_popup_depth > 0)
    {
        const popup_entry &top = c->prev_popups[c->prev_popup_depth - 1];
        if (!top.area.contains(x, y)) return true;
        if (!c->in_popup) return true;
    }
    if (c->prev_panel_depth > 0)
    {
        for (i32 i = c->prev_panel_depth - 1; i >= 0; --i)
        {
            if (c->prev_panels[i].area.contains(x, y))
            {
                const bool inside =
                    c->panel_stack_depth > 0 &&
                    c->panel_stack[c->panel_stack_depth - 1] == c->prev_panels[i].id;
                if (!inside) return true;
                break;
            }
        }
    }
    return false;
}
} // namespace detail

scroll_view ui::scroll(rect viewport, uiid id, scroll_options ov)
{
    return scroll_view(*this, id, viewport, ov);
}

scroll_view::scroll_view(ui &u, uiid id, rect viewport, scroll_options ov)
{
    u_ = &u;
    id_ = id;
    viewport_ = viewport;
    padding_ = ov.padding;
    flags_ = ov.flags;

    context *c = u.ctx;
    PUFFERUI_CHECK(VIOL_INVALID_AREA, viewport.is_valid(), "scroll viewport is invalid");

    scroll_store::entry &st = ensure_scrolls(c)->map[id];
    st.last_used = c->now;

    const scroll_style &sb = c->active_theme.scrollbar;
    const f32 gutter = sb.thickness + sb.margin * 2.0f;

    // Overflow comes from the previous frame's content height; the current one is
    // only known after the caller has laid out.
    overflows_ = st.content_h > viewport.h + 0.5f;
    const bool reserve =
        (flags_ & SCROLL_ALWAYS_RESERVE_BAR) != 0 || (!(flags_ & SCROLL_OVERLAY) && overflows_);
    const f32 gutter_w = reserve ? gutter : 0.0f;

    const f32 max_scroll = max2(0.0f, st.content_h - viewport.h);
    st.offset = clampf(st.offset, 0.0f, max_scroll);

    // Scrollbar geometry and the thumb press happen before content widgets, so an
    // overlay bar cannot be stolen by a widget underneath it.
    if (overflows_)
    {
        const f32 track_h = max2(0.0f, viewport.h - sb.margin * 2.0f);
        const f32 thumb_h = max2(sb.min_thumb, track_h * (viewport.h / max2(st.content_h, 1.0f)));
        const f32 travel = max2(0.0f, track_h - thumb_h);
        const f32 ratio = (max_scroll > 0.0f) ? (st.offset / max_scroll) : 0.0f;
        const f32 track_x = viewport.right() - sb.thickness - sb.margin;
        bar_track_ = rect::make(track_x, viewport.y + sb.margin, sb.thickness, track_h);
        bar_thumb_ = rect::make(track_x, bar_track_.y + ratio * travel, sb.thickness, thumb_h);

        const uiid thumb_id = id_child(id_, 0x5C);
        interaction in = u.interact(thumb_id, bar_thumb_);
        thumb_hovered_ = in.hovered || in.held;
        if (thumb_hovered_) c->want_cursor = CURSOR_HAND;
        if (in.activated)
        {
            st.dragging = true;
            st.drag_anchor = c->mouse_y;
            st.drag_offset = st.offset;
        }
        if (st.dragging && c->active == thumb_id && c->mouse_down)
        {
            const f32 delta = c->mouse_y - st.drag_anchor;
            st.offset =
                clampf(st.drag_offset + (travel > 0.0f ? (delta / travel) * max_scroll : 0.0f),
                       0.0f, max_scroll);
        }
        else if (!c->mouse_down)
        {
            st.dragging = false;
        }
    }
    else
    {
        st.dragging = false;
        bar_track_ = {};
        bar_thumb_ = {};
        thumb_hovered_ = false;
    }

    // Wheel claim: the innermost hovered view wins (constructors run outer->inner).
    if (viewport.contains(c->mouse_x, c->mouse_y) &&
        !detail::pointer_blocked(c, c->mouse_x, c->mouse_y))
        c->wheel_target = id_;

    if (!(flags_ & SCROLL_NO_CLIP) && c->clip_depth < MAX_CLIP_DEPTH)
    {
        const rect parent_clip = (c->clip_depth > 0) ? c->clip_stack[c->clip_depth - 1]
                                                     : rect{0.0f, 0.0f, 1.0e9f, 1.0e9f};
        c->clip_stack[c->clip_depth++] = rect::intersect(viewport_, parent_clip);
        clip_pushed_ = true;
    }

    content_h_ = st.content_h;
    content_ = rect::make(viewport.x + padding_, viewport.y - st.offset + padding_,
                          max2(0.0f, viewport.w - padding_ * 2.0f - gutter_w), content_h_);
}

scroll_view::~scroll_view()
{
    if (!u_) return;
    context *c = u_->ctx;
    scroll_store::entry &st = ensure_scrolls(c)->map[id_];
    st.last_used = c->now;

    // Wheel input lands here so nested views consume it inner-first.
    if (c->wheel_target == id_)
    {
        const f32 speed = 30.0f;
        st.offset =
            clampf(st.offset - c->wheel_y * speed, 0.0f, max2(0.0f, st.content_h - viewport_.h));
        c->wheel_target = 0;
    }

    if (overflows_)
    {
        const scroll_style &sb = c->active_theme.scrollbar;
        u_->draw_rounded_rect(bar_track_, sb.track, sb.radius);
        color tc = thumb_hovered_ ? sb.thumb_hover : sb.thumb;
        if (st.dragging) tc = sb.thumb_active;
        u_->draw_rounded_rect(bar_thumb_, tc, sb.radius);
    }

    if (clip_pushed_ && c->clip_depth > 0)
    {
        c->clip_depth -= 1;
        clip_pushed_ = false;
    }
}

f32 scroll_view::offset() const
{
    if (!u_ || !u_->ctx->scrolls) return 0.0f;
    scroll_store *ss = static_cast<scroll_store *>(u_->ctx->scrolls);
    auto it = ss->map.find(id_);
    return (it == ss->map.end()) ? 0.0f : it->second.offset;
}

void scroll_view::set_content_height(f32 height)
{
    content_h_ = max2(0.0f, height);
    if (!u_) return;
    context *c = u_->ctx;
    scroll_store::entry &st = ensure_scrolls(c)->map[id_];
    st.content_h = content_h_;
    st.viewport_h = viewport_.h;
    st.offset = clampf(st.offset, 0.0f, max2(0.0f, st.content_h - viewport_.h));
    content_.h = content_h_;
    content_.y = viewport_.y - st.offset + padding_;
}

void scroll_view::scroll_to(f32 offset)
{
    if (!u_) return;
    scroll_store::entry &st = ensure_scrolls(u_->ctx)->map[id_];
    st.offset = clampf(offset, 0.0f, max2(0.0f, st.content_h - viewport_.h));
    content_.y = viewport_.y - st.offset + padding_;
}

void scroll_view::scroll_by(f32 delta)
{
    scroll_to(offset() + delta);
}

void scroll_view::ensure_visible(rect r, f32 margin)
{
    if (!u_) return;
    const f32 off = offset();
    const f32 top = viewport_.y + margin;
    const f32 bottom = viewport_.bottom() - margin;
    if (r.y < top)
        scroll_to(off - (top - r.y));
    else if (r.bottom() > bottom)
        scroll_to(off + (r.bottom() - bottom));
}

void scroll_view::virtual_list(i32 count, f32 item_height, function_ref<void(ui &, i32, rect)> fn)
{
    if (!u_) return;
    PUFFERUI_CHECK(VIOL_INVALID_AREA, item_height > 0.0f,
                   "virtual_list item height must be positive");
    if (count <= 0 || item_height <= 0.0f)
    {
        set_content_height(0.0f);
        return;
    }

    // The content height is derived: the scroll extent and the gutter both
    // follow from the list, so callers never repeat count * item_height.
    set_content_height(static_cast<f32>(count) * item_height);

    // Visible slice on the item grid: the first row straddling the viewport
    // top (its top edge sits above it by off % item_height), then enough rows
    // to cover the viewport plus the bottom straddler. Clamped to the list —
    // no callback ever runs for an index past the end.
    const f32 off = offset();
    i32 first = (item_height > 0.0f) ? static_cast<i32>(off / item_height) : 0;
    if (first < 0) first = 0;
    if (first > count) first = count;
    i32 visible = static_cast<i32>(viewport_.h / item_height) + 2;
    if (first + visible > count) visible = count - first;
    if (visible < 0) visible = 0;

    for (i32 i = 0; i < visible; ++i)
    {
        const i32 index = first + i;
        const rect row = rect::make(content_.x, content_.y + static_cast<f32>(index) * item_height,
                                    content_.w, item_height);
        fn(*u_, index, row);
    }
}

// ---- interactive splitters ----
namespace detail
{

inline f32 &split_offset(context *c, uiid id)
{
    if (!c->splits) c->splits = new split_store();
    return static_cast<split_store *>(c->splits)->offsets[id];
}

} // namespace detail

std::pair<rect, rect> ui::split_horizontal_interactive(uiid id, rect bounds, f32 *position,
                                                       f32 min_left, f32 max_left, f32 thickness,
                                                       f32 gap)
{
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, bounds.is_valid(),
                   "split_horizontal_interactive invalid bounds");
    if (!position) return {bounds, rect{}};

    const f32 max_allowed =
        max2(min_left, min2(max_left, bounds.w - min_left - thickness - gap * 2.0f));
    *position = clampf(*position, min_left, max_allowed);

    const rect handle = {bounds.x + *position + gap, bounds.y, thickness, bounds.h};
    interaction in = interact(id, handle);
    if (in.hovered || in.held) ctx->want_cursor = CURSOR_HRESIZE;

    f32 &offset = detail::split_offset(ctx, id);
    if (in.activated) offset = ctx->mouse_x - (bounds.x + *position);
    if (ctx->active == id && ctx->mouse_down)
        *position = clampf(ctx->mouse_x - offset - bounds.x, min_left, max_allowed);

    const rect left = {bounds.x, bounds.y, *position, bounds.h};
    const f32 right_x = bounds.x + *position + gap + thickness + gap;
    const rect right = {right_x, bounds.y, max2(0.0f, bounds.right() - right_x), bounds.h};

    const color col = (ctx->active == id)
                          ? ctx->active_theme.accent
                          : (in.hovered ? ctx->active_theme.accent_hover : color{45, 53, 64, 160});
    draw_rounded_rect(handle, col, 2.0f);
    return {left, right};
}

std::pair<rect, rect> ui::split_vertical_interactive(uiid id, rect bounds, f32 *position,
                                                     f32 min_top, f32 max_top, f32 thickness,
                                                     f32 gap)
{
    PUFFERUI_CHECK(VIOL_INVALID_SLICE, bounds.is_valid(),
                   "split_vertical_interactive invalid bounds");
    if (!position) return {bounds, rect{}};

    const f32 max_allowed =
        max2(min_top, min2(max_top, bounds.h - min_top - thickness - gap * 2.0f));
    *position = clampf(*position, min_top, max_allowed);

    const rect handle = {bounds.x, bounds.y + *position + gap, bounds.w, thickness};
    interaction in = interact(id, handle);
    if (in.hovered || in.held) ctx->want_cursor = CURSOR_VRESIZE;

    f32 &offset = detail::split_offset(ctx, id);
    if (in.activated) offset = ctx->mouse_y - (bounds.y + *position);
    if (ctx->active == id && ctx->mouse_down)
        *position = clampf(ctx->mouse_y - offset - bounds.y, min_top, max_allowed);

    const rect top = {bounds.x, bounds.y, bounds.w, *position};
    const f32 bottom_y = bounds.y + *position + gap + thickness + gap;
    const rect bottom = {bounds.x, bottom_y, bounds.w, max2(0.0f, bounds.bottom() - bottom_y)};

    const color col = (ctx->active == id)
                          ? ctx->active_theme.accent
                          : (in.hovered ? ctx->active_theme.accent_hover : color{45, 53, 64, 160});
    draw_rounded_rect(handle, col, 2.0f);
    return {top, bottom};
}

// ---- docking ----
namespace detail
{

struct dock_area
{
    dock_node *node;
    rect area;
};

inline i32 dock_zone_for(const rect &area, f32 mx, f32 my)
{
    const f32 ex = area.w * 0.25f, ey = area.h * 0.25f;
    if (mx < area.x + ex) return DOCK_ZONE_LEFT;
    if (mx > area.right() - ex) return DOCK_ZONE_RIGHT;
    if (my < area.y + ey) return DOCK_ZONE_TOP;
    if (my > area.bottom() - ey) return DOCK_ZONE_BOTTOM;
    return DOCK_ZONE_CENTER;
}

inline void dock_draw_node(ui &u, uiid node_id, dock_node &n, rect area, i32 depth,
                           function_ref<void(uiid, rect, bool)> draw_panel, dock_area *areas,
                           i32 &area_count, context *c)
{
    if (depth > MAX_DOCK_DEPTH) return;
    if (area.w <= 1.0f || area.h <= 1.0f) return;

    if (n.kind == DOCK_SPLIT_H && n.a && n.b)
    {
        f32 pos = area.w * n.ratio;
        const auto halves =
            u.split_horizontal_interactive(node_id, area, &pos, 40.0f, area.w - 80.0f, 6.0f, 2.0f);
        n.ratio = (area.w > 0.0f) ? clampf(pos / area.w, 0.0f, 1.0f) : 0.5f;
        dock_draw_node(u, id_child(node_id, 1), *n.a, halves.first, depth + 1, draw_panel, areas,
                       area_count, c);
        dock_draw_node(u, id_child(node_id, 2), *n.b, halves.second, depth + 1, draw_panel, areas,
                       area_count, c);
        return;
    }
    if (n.kind == DOCK_SPLIT_V && n.a && n.b)
    {
        f32 pos = area.h * n.ratio;
        const auto halves =
            u.split_vertical_interactive(node_id, area, &pos, 40.0f, area.h - 80.0f, 6.0f, 2.0f);
        n.ratio = (area.h > 0.0f) ? clampf(pos / area.h, 0.0f, 1.0f) : 0.5f;
        dock_draw_node(u, id_child(node_id, 1), *n.a, halves.first, depth + 1, draw_panel, areas,
                       area_count, c);
        dock_draw_node(u, id_child(node_id, 2), *n.b, halves.second, depth + 1, draw_panel, areas,
                       area_count, c);
        return;
    }

    // leaf / tabs: tab strip + active panel content
    rect content = area;
    if (n.panel_count > 0)
    {
        const f32 tab_h = 26.0f;
        rect bar = content.cut_top(tab_h);
        u.draw_rect(bar, u.th().bg);

        // Clip the strip: tabs never draw outside the node, and a tab that would
        // not fit is simply not emitted (instead of overlapping its neighbours).
        {
            region bar_clip(u, id_child(node_id, 500ull), bar, true);
            row tabs(bar, 4.0f);
            for (i32 i = 0; i < n.panel_count; ++i)
            {
                const f32 avail = tabs.remaining().w;
                if (avail <= 8.0f) break;
                const f32 tw = min2(110.0f, avail);
                rect tr = tabs.next(tw);
                if (tr.w <= 8.0f) break;

                const uiid tid = id_child(node_id, 1000ull + static_cast<u64>(i));
                interaction in = u.interact(tid, tr);
                if (in.hovered || in.held) c->want_cursor = CURSOR_HAND; // draggable tab
                const bool sel = (i == n.active);
                const color col =
                    sel ? u.th().panel_bg : (in.hovered ? u.th().widget_hover : u.th().bg);
                u.draw_rect(tr, col);
                if (const char *nm = n.panel_names[i] ? n.panel_names[i] : "panel")
                    if (u.text_width(nm) <= tr.w - 8.0f) u.text(tr, nm, u.th().text, ALIGN_CENTER);

                if (in.clicked) n.active = i;
                if (in.activated)
                {
                    c->dock_panel = n.panels[i];
                    c->dock_source = &n;
                    c->dock_panel_name = n.panel_names[i];
                    c->dock_dragging = false;
                    c->dock_press_x = c->mouse_x;
                    c->dock_press_y = c->mouse_y;
                }
                if (c->dock_panel == n.panels[i] && c->mouse_down)
                {
                    const f32 dx = c->mouse_x - c->dock_press_x;
                    const f32 dy = c->mouse_y - c->dock_press_y;
                    if (dx * dx + dy * dy > 16.0f) c->dock_dragging = true;
                }
            }
        }
    }

    if (n.panel_count > 0)
    {
        if (n.active < 0 || n.active >= n.panel_count) n.active = 0;
        if (area_count < 32) areas[area_count++] = dock_area{&n, area};
        if (draw_panel)
        {
            // Clip panel content to its node so shrinking a split never lets
            // content spill into the neighbour.
            region content_clip(u, id_child(node_id, 700ull), content, true);
            draw_panel(n.panels[n.active], content, true);
        }
    }
}

} // namespace detail

dock_action ui::dock_space(uiid id, rect area, dock_node &root,
                           function_ref<void(uiid, rect, bool)> draw_panel)
{
    context *c = ctx;
    detail::dock_area areas[32];
    i32 area_count = 0;
    detail::dock_draw_node(*this, id, root, area, 0, draw_panel, areas, area_count, c);

    dock_action action;

    // highlight the drop zone while dragging a tab
    if (c->dock_dragging)
    {
        for (i32 i = area_count - 1; i >= 0; --i)
        {
            if (!areas[i].area.contains(c->mouse_x, c->mouse_y)) continue;
            const i32 zone = detail::dock_zone_for(areas[i].area, c->mouse_x, c->mouse_y);
            rect hl = areas[i].area;
            const f32 w4 = hl.w * 0.25f, h4 = hl.h * 0.25f;
            switch (zone)
            {
            case DOCK_ZONE_LEFT:
                hl.w = w4;
                break;
            case DOCK_ZONE_RIGHT:
                hl.x = hl.right() - w4;
                hl.w = w4;
                break;
            case DOCK_ZONE_TOP:
                hl.h = h4;
                break;
            case DOCK_ZONE_BOTTOM:
                hl.y = hl.bottom() - h4;
                hl.h = h4;
                break;
            default:
                break;
            }
            draw_rect(hl, color{60, 180, 255, 90});
            break;
        }

        // drag ghost so the drag is visible
        if (c->dock_panel_name)
        {
            rect ghost = {c->mouse_x + 14.0f, c->mouse_y + 10.0f, 110.0f, 22.0f};
            draw_rounded_rect(ghost, color{30, 40, 58, 220}, 4.0f);
            text(ghost, c->dock_panel_name, ctx->active_theme.text, ALIGN_CENTER);
        }
    }

    // release ends a drag: report the move; the app applies it
    if (c->mouse_released && c->dock_panel != 0)
    {
        if (c->dock_dragging)
        {
            action.active = true;
            action.panel = c->dock_panel;
            action.target = nullptr; // nullptr == undock/floating
            action.zone = DOCK_ZONE_CENTER;
            for (i32 i = area_count - 1; i >= 0; --i)
            {
                if (areas[i].area.contains(c->mouse_x, c->mouse_y))
                {
                    action.target = areas[i].node;
                    action.zone = detail::dock_zone_for(areas[i].area, c->mouse_x, c->mouse_y);
                    break;
                }
            }
        }
        c->dock_panel = 0;
        c->dock_source = nullptr;
        c->dock_panel_name = nullptr;
        c->dock_dragging = false;
    }

    return action;
}

// ---- typed inputs ----
namespace detail
{

inline usize prev_cp(const std::string &s, usize i)
{
    if (i == 0) return 0;
    usize j = i - 1;
    while (j > 0 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) --j;
    return j;
}
inline usize next_cp(const std::string &s, usize i)
{
    if (i >= s.size()) return s.size();
    usize j = i + 1;
    while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
    return j;
}

inline edit_state &edit_for(context *c, uiid id)
{
    if (!c->edits) c->edits = new edit_store();
    return static_cast<edit_store *>(c->edits)->map[id];
}

inline bool has_selection(const edit_state &st)
{
    return st.anchor != st.caret;
}
inline usize sel_lo(const edit_state &st)
{
    return st.anchor < st.caret ? st.anchor : st.caret;
}
inline usize sel_hi(const edit_state &st)
{
    return st.anchor < st.caret ? st.caret : st.anchor;
}

// Caret index nearest to `local_x` (text space). Mirrors `text()`: advances plus
// the kerning between consecutive codepoints, so a click lands exactly where the
// drawn caret (which measures prefixes with `text_width`) sits.
inline usize index_at_x(ui &u, const std::string &s, f32 local_x)
{
    context *c = u.ctx;
    const f32 size = c->active_theme.text_size;
    const font_handle fh = c->active_theme.font;
    f32 x = 0.0f;
    usize i = 0, best = 0;
    f32 best_d = 1.0e9f;
    u32 prev = 0;
    while (true)
    {
        const f32 d = std::fabs(x - local_x);
        if (d < best_d)
        {
            best_d = d;
            best = i;
        }
        if (i >= s.size()) break;
        const usize j = next_cp(s, i);
        usize k = i;
        const u32 cp = utf8_decode(s, k);
        x += kern_advance(c, fh, size, prev, cp);
        x += u.text_width(std::string_view(s.data() + i, j - i));
        prev = cp;
        i = j;
    }
    return best;
}

// Word policy: ASCII alphanumerics, '_' and every non-ASCII codepoint count as
// word characters, so Turkish/Greek words behave as single words. Punctuation and
// whitespace form their own runs.
inline bool word_cp(u32 cp)
{
    return cp != UTF8_END && (cp == '_' || cp >= 0x80 || (cp >= '0' && cp <= '9') ||
                              (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'));
}

inline u32 cp_at(const std::string &s, usize i)
{
    if (i >= s.size()) return UTF8_END;
    usize j = i;
    return utf8_decode(s, j);
}

// Next/previous word boundary: the positions Ctrl+Left/Right jump to.
inline usize next_word_boundary(const std::string &s, usize i)
{
    const usize n = s.size();
    if (i >= n) return n;
    const bool word = word_cp(cp_at(s, i));
    usize j = i;
    while (j < n)
    {
        const usize k = next_cp(s, j);
        if (k >= n || word_cp(cp_at(s, k)) != word) return k;
        j = k;
    }
    return n;
}

inline usize prev_word_boundary(const std::string &s, usize i)
{
    if (i == 0) return 0;
    if (i > s.size()) i = s.size();
    const usize p = prev_cp(s, i);
    const bool word = word_cp(cp_at(s, p));
    usize j = p;
    while (j > 0)
    {
        const usize k = prev_cp(s, j);
        if (word_cp(cp_at(s, k)) != word) return j;
        j = k;
    }
    return 0;
}

inline bool space_cp(u32 cp)
{
    return cp == ' ' || cp == '\t';
}

// Ctrl+Left/Right stops: whitespace is skipped so you land at the start/end of
// the next chunk ("foo bar" -> 3, 7 from 0), like browsers.
inline usize next_word_stop(const std::string &s, usize i)
{
    const usize n = s.size();
    usize j = i;
    while (j < n && space_cp(cp_at(s, j))) j = next_cp(s, j);
    return next_word_boundary(s, j);
}

inline usize prev_word_stop(const std::string &s, usize i)
{
    usize j = i;
    while (j > 0)
    {
        const usize p = prev_cp(s, j);
        if (!space_cp(cp_at(s, p))) break;
        j = p;
    }
    return prev_word_boundary(s, j);
}

// The maximal same-class run around `i` (double-click selection). Clicking just
// after a word selects that word, like browsers do.
inline void word_run_at(const std::string &s, usize i, usize &lo, usize &hi)
{
    const usize n = s.size();
    if (i > n) i = n;
    lo = hi = i;
    if (n == 0) return;
    const usize start = (i > 0) ? prev_cp(s, i) : i;
    const bool word = word_cp(cp_at(s, start));
    lo = hi = start;
    while (lo > 0)
    {
        const usize p = prev_cp(s, lo);
        if (word_cp(cp_at(s, p)) != word) break;
        lo = p;
    }
    while (hi < n)
    {
        if (word_cp(cp_at(s, hi)) != word) break;
        hi = next_cp(s, hi);
    }
}

inline void select_all(edit_state &st)
{
    st.anchor = 0;
    st.caret = st.buffer.size();
}

inline void select_word_at(edit_state &st, usize i)
{
    word_run_at(st.buffer, i, st.anchor, st.caret);
}

inline void erase_range(edit_state &st, usize lo, usize hi)
{
    if (hi <= lo) return;
    st.buffer.erase(lo, hi - lo);
    st.caret = lo;
    st.anchor = lo;
}

// Note: `edit_state::undo`/`redo` are only meaningful while the field is focused
// — `edit_buffer` clears them on every focus loss, because an unfocused field
// reloads its buffer from the model each frame (stale snapshots would otherwise
// be restored, and written back, by a later Ctrl+Z).

// Undo/redo: consecutive single-character edits coalesce into one step.
inline void push_undo(edit_state &st, f64 now, bool coalesce)
{
    const bool merge = coalesce && st.last_edit_time >= 0.0 && (now - st.last_edit_time) < 0.4;
    if (!merge)
    {
        st.undo.push_back(edit_state::snapshot{st.buffer, st.caret, st.anchor});
        if (st.undo.size() > 64) st.undo.erase(st.undo.begin());
    }
    st.redo.clear();
    st.last_edit_time = now;
}

inline void break_undo_coalescing(edit_state &st)
{
    st.last_edit_time = -1.0;
}

inline bool undo_edit(edit_state &st)
{
    if (st.undo.empty()) return false;
    st.redo.push_back(edit_state::snapshot{st.buffer, st.caret, st.anchor});
    const edit_state::snapshot s = st.undo.back();
    st.undo.pop_back();
    st.buffer = s.text;
    st.caret = s.caret;
    st.anchor = s.anchor;
    break_undo_coalescing(st);
    return true;
}

inline bool redo_edit(edit_state &st)
{
    if (st.redo.empty()) return false;
    st.undo.push_back(edit_state::snapshot{st.buffer, st.caret, st.anchor});
    if (st.undo.size() > 64) st.undo.erase(st.undo.begin());
    const edit_state::snapshot s = st.redo.back();
    st.redo.pop_back();
    st.buffer = s.text;
    st.caret = s.caret;
    st.anchor = s.anchor;
    break_undo_coalescing(st);
    return true;
}

// Scoped clip push/pop for field bodies (long values must not paint outside).
inline bool clip_push(context *c, rect r)
{
    if (c->clip_depth >= MAX_CLIP_DEPTH) return false;
    const rect parent =
        (c->clip_depth > 0) ? c->clip_stack[c->clip_depth - 1] : rect{0, 0, 1.0e9f, 1.0e9f};
    c->clip_stack[c->clip_depth++] = rect::intersect(r, parent);
    return true;
}

inline void clip_pop(context *c)
{
    if (c->clip_depth > 0) --c->clip_depth;
}

inline void register_focusable(context *c, uiid id)
{
    if (!c->focus_store) c->focus_store = new focus_store();
    focus_store *fs = static_cast<focus_store *>(c->focus_store);
    // edit_buffer runs right after the field's interact (which already joined
    // the ring): adjacent-dedup so a widget never occupies two slots.
    if (fs->current.empty() || fs->current.back() != id) fs->current.push_back(id);
}

inline void tab_move(context *c, uiid id, bool back)
{
    if (!c->focus_store) return;
    focus_store *fs = static_cast<focus_store *>(c->focus_store);
    const i32 n = static_cast<i32>(fs->prev.size());
    if (n <= 0) return;
    i32 idx = -1;
    for (i32 i = 0; i < n; ++i)
        if (fs->prev[static_cast<usize>(i)] == id)
        {
            idx = i;
            break;
        }
    const i32 next = (idx < 0) ? 0 : (back ? (idx - 1 + n) % n : (idx + 1) % n);
    c->focus_request = fs->prev[static_cast<usize>(next)];
    c->focus_request_selects_all = true;
}

inline bool caret_visible(context *c)
{
    return std::fmod(c->now, 1.0) < 0.5;
}

inline bool edit_buffer(ui &u, uiid id, rect r, const interaction &in, edit_state &st)
{
    context *c = u.ctx;

    bool changed = false;

    // Keep the context's focused-widget id in sync so `interaction.focused`
    // reflects text-field focus for built-in and custom widgets alike.
    // Focus changes reset the undo history: while unfocused the buffer reloads
    // from the model, so snapshots taken before a blur could otherwise restore
    // stale text (and write it back) on a later Ctrl+Z.
    auto set_focus = [&](bool focused)
    {
        st.focused = focused;
        if (focused)
        {
            c->focus = id;
        }
        else
        {
            if (c->focus == id) c->focus = 0;
            st.undo.clear();
            st.redo.clear();
            break_undo_coalescing(st);
        }
    };

    // A blocked field (under a popup/panel layer) neither joins the tab order nor
    // consumes input, so an open popup traps focus.
    if (in.blocked)
    {
        if (st.focused) set_focus(false);
        return false;
    }

    register_focusable(c, id);

    // Keyboard focus is one state: c->focus (shared with every other widget).
    // A field derives st.focused from it with edge detection — the rising edge
    // consumes the Tab select-all pending flag (Tab focus selects the value)
    // and resets the horizontal scroll; the falling edge clears the undo
    // history (see set_focus above).
    const bool now_focused = (c->focus == id);
    if (now_focused && !st.focused)
    {
        st.focused = true;
        st.scroll_x = 0.0f;
        if (c->focus_request_selects_all)
        {
            select_all(st);
            c->focus_request_selects_all = false;
        }
    }
    else if (!now_focused && st.focused)
    {
        set_focus(false);
    }

    const f32 pad = u.th().padding * 0.5f;
    const f32 local_x = c->mouse_x - (r.x + pad) + st.scroll_x;

    // Press: focus (when needed) and place the caret where you clicked, with
    // double/triple-click selection. Handling this on the press edge is what
    // stops the caret from jumping to the end on the first click.
    if (c->mouse_pressed && in.hovered)
    {
        if (!st.focused)
        {
            set_focus(true);
            st.undo.clear();
            st.redo.clear();
        }
        const usize idx = index_at_x(u, st.buffer, local_x);
        // Double/triple clicks must land on the same spot, not just soon after.
        const bool near = st.last_click_time >= 0.0 && (c->now - st.last_click_time) < 0.4 &&
                          std::fabs(c->mouse_x - st.last_click_x) <= 5.0f &&
                          std::fabs(c->mouse_y - st.last_click_y) <= 5.0f;
        st.click_streak = near ? st.click_streak + 1 : 1;
        st.last_click_time = c->now;
        st.last_click_x = c->mouse_x;
        st.last_click_y = c->mouse_y;
        st.press_x = c->mouse_x;
        st.press_y = c->mouse_y;
        st.dragging = true;
        st.drag_moved = false;
        st.pending_drag = false;
        if (st.click_streak >= 3)
            select_all(st);
        else if (st.click_streak == 2)
            select_word_at(st, idx);
        else
        {
            const bool inside_sel = has_selection(st) && idx > sel_lo(st) && idx < sel_hi(st);
            if (inside_sel && !c->shift_down)
                st.pending_drag = true; // may become a move drag; else collapses on release
            else
            {
                st.caret = idx;
                st.anchor = c->shift_down ? st.anchor : st.caret;
            }
        }
        break_undo_coalescing(st);
    }

    // Drop an active text drag into this field (move; hold Ctrl to copy).
    if (c->text_drag.active && c->mouse_released && in.hovered)
    {
        const usize idx = index_at_x(u, st.buffer, local_x);
        const usize lo = c->text_drag.lo, hi = c->text_drag.hi;
        const bool from_self = c->text_drag.source_id == id;
        const bool copy = c->ctrl_down;

        // The source keeps accepting input while the drag is in flight, so the
        // recorded range may no longer hold the lifted text. A move verifies it
        // first and cancels instead of erasing the wrong characters; a copy only
        // inserts, so it needs no check.
        edit_state &src = edit_for(c, c->text_drag.source_id);
        const bool range_ok = hi >= lo && hi <= src.buffer.size() &&
                              src.buffer.compare(lo, hi - lo, c->text_drag.text) == 0;
        if (copy || range_ok)
        {
            push_undo(st, c->now, false);
            if (from_self && !copy)
            {
                usize at = idx;
                if (at > lo && at <= hi)
                    at = lo; // dropped onto itself: keep the text where it is
                else if (at > hi)
                    at -= (hi - lo);
                st.buffer.erase(lo, hi - lo);
                st.buffer.insert(at, c->text_drag.text);
                st.caret = at + c->text_drag.text.size();
            }
            else
            {
                st.buffer.insert(idx, c->text_drag.text);
                st.caret = idx + c->text_drag.text.size();
                if (!from_self && !copy)
                {
                    // Erase the lifted range from its source field; that field
                    // stays focused until it has flushed, then this one takes over.
                    src.buffer.erase(lo, hi - lo);
                    src.caret = src.anchor = lo;
                    break_undo_coalescing(src);
                    c->focus_request = id;
                    c->focus_request_selects_all = false;
                }
            }
            st.anchor = st.caret;
            changed = true;
        }
        c->text_drag.active = false;
        c->text_drag.text.clear();
    }

    if (!st.focused) return changed;

    if (c->mouse_pressed && !in.hovered)
    {
        set_focus(false);
        return false;
    }
    if (c->key_pressed[static_cast<i32>(key::ENTER)])
    {
        set_focus(false);
        return false;
    }
    if (c->key_pressed[static_cast<i32>(key::ESCAPE)])
    {
        if (c->text_drag.active)
        {
            c->text_drag.active = false; // cancel the drag, keep editing
            c->text_drag.text.clear();
            return changed;
        }
        set_focus(false);
        return false;
    }

    // clipboard + undo/redo
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::A)])
    {
        select_all(st);
    }
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::C)])
    {
        if (has_selection(st) && c->clip)
            c->clip->set(std::string_view(st.buffer).substr(sel_lo(st), sel_hi(st) - sel_lo(st)));
    }
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::X)] && has_selection(st))
    {
        if (c->clip)
            c->clip->set(std::string_view(st.buffer).substr(sel_lo(st), sel_hi(st) - sel_lo(st)));
        push_undo(st, c->now, false);
        erase_range(st, sel_lo(st), sel_hi(st));
        changed = true;
    }
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::V)] && c->clip)
    {
        std::string paste;
        if (c->clip->get(paste))
        {
            push_undo(st, c->now, false);
            if (has_selection(st)) erase_range(st, sel_lo(st), sel_hi(st));
            st.buffer.insert(st.caret, paste);
            st.caret += paste.size();
            st.anchor = st.caret;
            changed = true;
        }
    }
    if (c->ctrl_down && c->key_pressed[static_cast<i32>(key::Z)] && !c->shift_down)
    {
        if (undo_edit(st)) changed = true;
    }
    if (c->ctrl_down && (c->key_pressed[static_cast<i32>(key::Y)] ||
                         (c->shift_down && c->key_pressed[static_cast<i32>(key::Z)])))
    {
        if (redo_edit(st)) changed = true;
    }

    // Mouse: extend the selection, or lift it into a browser-like text drag.
    if (st.dragging && c->mouse_down)
    {
        const f32 ddx = c->mouse_x - st.press_x, ddy = c->mouse_y - st.press_y;
        if (st.pending_drag && (ddx * ddx + ddy * ddy) > 16.0f && has_selection(st))
        {
            c->text_drag.active = true;
            c->text_drag.source_id = id;
            c->text_drag.lo = sel_lo(st);
            c->text_drag.hi = sel_hi(st);
            c->text_drag.text =
                st.buffer.substr(c->text_drag.lo, c->text_drag.hi - c->text_drag.lo);
            st.pending_drag = false;
            st.drag_moved = true;
        }
        if (!st.pending_drag && !st.drag_moved)
        {
            const usize idx = index_at_x(u, st.buffer, local_x);
            if (st.click_streak >= 2)
            {
                usize lo = 0, hi = 0;
                word_run_at(st.buffer, idx, lo, hi);
                st.caret = (idx >= st.anchor) ? hi : lo;
            }
            else
            {
                st.caret = idx;
            }
        }
    }
    if (!c->mouse_down)
    {
        if (st.pending_drag)
        {
            // Pressed inside the selection without moving: collapse the caret there.
            st.caret = index_at_x(u, st.buffer, local_x);
            st.anchor = st.caret;
        }
        st.pending_drag = false;
        st.dragging = false;
        st.drag_moved = false;
    }

    // typing replaces selection
    if (c->text_len > 0)
    {
        push_undo(st, c->now, !has_selection(st));
        if (has_selection(st)) erase_range(st, sel_lo(st), sel_hi(st));
        st.buffer.insert(st.caret, c->text_input, static_cast<usize>(c->text_len));
        st.caret += static_cast<usize>(c->text_len);
        st.anchor = st.caret;
        changed = true;
    }

    // backspace / delete (Ctrl deletes a whole word)
    if (c->key_pressed[static_cast<i32>(key::BACKSPACE)])
    {
        if (has_selection(st))
        {
            push_undo(st, c->now, false);
            erase_range(st, sel_lo(st), sel_hi(st));
            changed = true;
        }
        else if (st.caret > 0)
        {
            const usize lo =
                c->ctrl_down ? prev_word_stop(st.buffer, st.caret) : prev_cp(st.buffer, st.caret);
            push_undo(st, c->now, !c->ctrl_down);
            erase_range(st, lo, st.caret);
            changed = true;
        }
    }
    if (c->key_pressed[static_cast<i32>(key::DEL)])
    {
        if (has_selection(st))
        {
            push_undo(st, c->now, false);
            erase_range(st, sel_lo(st), sel_hi(st));
            changed = true;
        }
        else if (st.caret < st.buffer.size())
        {
            const usize hi =
                c->ctrl_down ? next_word_stop(st.buffer, st.caret) : next_cp(st.buffer, st.caret);
            push_undo(st, c->now, !c->ctrl_down);
            st.buffer.erase(st.caret, hi - st.caret);
            changed = true;
        }
    }

    // navigation (Shift keeps the anchor -> selection; Ctrl moves by word)
    const bool word = c->ctrl_down;
    if (c->key_pressed[static_cast<i32>(key::LEFT)])
    {
        const usize ni =
            (has_selection(st) && !c->shift_down)
                ? sel_lo(st)
                : (word ? prev_word_stop(st.buffer, st.caret) : prev_cp(st.buffer, st.caret));
        st.caret = ni;
        if (!c->shift_down) st.anchor = ni;
        break_undo_coalescing(st);
    }
    if (c->key_pressed[static_cast<i32>(key::RIGHT)])
    {
        const usize ni =
            (has_selection(st) && !c->shift_down)
                ? sel_hi(st)
                : (word ? next_word_stop(st.buffer, st.caret) : next_cp(st.buffer, st.caret));
        st.caret = ni;
        if (!c->shift_down) st.anchor = ni;
        break_undo_coalescing(st);
    }
    if (c->key_pressed[static_cast<i32>(key::HOME)])
    {
        st.caret = 0;
        if (!c->shift_down) st.anchor = 0;
        break_undo_coalescing(st);
    }
    if (c->key_pressed[static_cast<i32>(key::END)])
    {
        st.caret = st.buffer.size();
        if (!c->shift_down) st.anchor = st.caret;
        break_undo_coalescing(st);
    }

    // Keep the caret inside the visible field (single-line horizontal scroll).
    {
        const f32 visible = max2(1.0f, r.w - pad * 2.0f);
        const f32 caret_x = u.text_width(std::string_view(st.buffer.data(), st.caret));
        if (caret_x - st.scroll_x > visible - 1.0f) st.scroll_x = caret_x - visible + 1.0f;
        if (caret_x - st.scroll_x < 0.0f) st.scroll_x = caret_x;
        const f32 total = u.text_width(st.buffer);
        st.scroll_x = clampf(st.scroll_x, 0.0f, max2(0.0f, total - visible + 1.0f));
    }

    return changed;
}

// Shared field rendering: background, selection, text, IME preedit and caret —
// scrolled horizontally and clipped to the field so long values never paint
// outside it. `drop_index >= 0` shows the drop position of an active text drag;
// `dim` fades the range that was lifted from this field.
inline void draw_edit_body(ui &u, rect r, color border, f32 border_thickness, const edit_state &st,
                           const std::string &shown, bool focused, i32 drop_index = -1,
                           bool dim = false, usize dim_lo = 0, usize dim_hi = 0)
{
    const theme &t = u.th();
    if (border_thickness > 0.0f)
    {
        u.draw_rounded_rect(r, border, t.radius);
        u.draw_rounded_rect(r.pad(border_thickness), t.widget_bg,
                            max2(0.0f, t.radius - border_thickness));
    }
    else
    {
        u.draw_rounded_rect(r, t.widget_bg, t.radius);
    }

    const f32 pad = t.padding * 0.5f;
    const rect inner = r.pad(pad, 0.0f);
    const f32 scroll = st.scroll_x;
    const f32 line_h = u.line_height();
    const f32 top = inner.y + max2(0.0f, (inner.h - line_h) * 0.5f);

    const bool clipped = clip_push(u.ctx, inner);
    auto x_at = [&](usize idx)
    { return inner.x - scroll + u.text_width(std::string_view(shown.data(), idx)); };

    const usize lo = sel_lo(st), hi = sel_hi(st);
    if (focused && hi > lo)
    {
        color sc = t.selection;
        if (dim) sc.a = static_cast<u8>(sc.a / 3);
        const f32 x0 = x_at(lo), x1 = x_at(hi);
        u.draw_rect(rect::make(x0, inner.y + 2.0f, max2(0.0f, x1 - x0), max2(0.0f, r.h - 4.0f)),
                    sc);
    }

    if (dim && dim_hi > dim_lo && dim_hi <= shown.size())
    {
        color faded = t.text;
        faded.a = 90;
        u.text(rect::make(inner.x - scroll, top, 1.0e6f, line_h),
               std::string_view(shown.data(), dim_lo), t.text, ALIGN_LEFT);
        u.text(rect::make(x_at(dim_lo), top, 1.0e6f, line_h),
               std::string_view(shown.data() + dim_lo, dim_hi - dim_lo), faded, ALIGN_LEFT);
        u.text(rect::make(x_at(dim_hi), top, 1.0e6f, line_h),
               std::string_view(shown.data() + dim_hi), t.text, ALIGN_LEFT);
    }
    else
    {
        u.text(rect::make(inner.x - scroll, top, 1.0e6f, line_h), shown, t.text, ALIGN_LEFT);
    }

    if (focused && u.ctx->ime_preedit_len > 0)
    {
        const f32 px = x_at(st.caret);
        const f32 pw = u.text_width(
            std::string_view(u.ctx->ime_preedit, static_cast<usize>(u.ctx->ime_preedit_len)));
        u.text(rect::make(px, top, pw + 4.0f, line_h), u.ctx->ime_preedit, t.text, ALIGN_LEFT);
        u.draw_rect(rect::make(px, inner.y + r.h - 3.0f, pw, 1.0f), t.accent);
    }

    if (focused && drop_index < 0 && caret_visible(u.ctx))
        u.draw_rect(rect::make(x_at(st.caret), r.y + 4.0f, 1.0f, max2(0.0f, r.h - 8.0f)), t.caret);
    if (drop_index >= 0)
        u.draw_rect(rect::make(x_at(static_cast<usize>(drop_index)), r.y + 3.0f, 2.0f,
                               max2(0.0f, r.h - 6.0f)),
                    t.accent);

    if (clipped) clip_pop(u.ctx);
}

inline bool parse_float(const std::string &s, f32 &out)
{
    std::string t;
    t.reserve(s.size());
    int seps = 0;
    for (char ch : s)
    {
        if (ch == ',' || ch == '.')
        {
            ++seps;
            t.push_back('.');
        }
        else
            t.push_back(ch);
    }
    if (seps > 1 || t.empty()) return false;

    char *end = nullptr;
    const f32 v = std::strtof(t.c_str(), &end);
    if (end == t.c_str()) return false;
    while (end && (*end == ' ' || *end == '\t')) ++end;
    if (end && *end != '\0') return false;
    out = v;
    return true;
}

} // namespace detail

bool ui::text_field(rect r, std::string &value, uiid id)
{
    const theme &t = ctx->active_theme;
    interaction in = interact(id, r);
    edit_state &st = detail::edit_for(ctx, id);

    const bool was_focused = st.focused;
    if (!st.focused)
    {
        st.buffer = value;
        st.caret = st.buffer.size();
        st.anchor = st.caret;
        st.scroll_x = 0.0f;
    }
    const bool edited = detail::edit_buffer(*this, id, r, in, st);
    bool changed = false;
    // Write back when focused, when focus was just lost, or when the framework
    // edited the buffer (e.g. a text drop into an unfocused field) — otherwise
    // the next reload would discard the edit.
    if ((st.focused || was_focused || edited) && value != st.buffer)
    {
        value = st.buffer;
        changed = true;
    }
    else if (edited)
        changed = true;

    if (in.hovered) ctx->want_cursor = t.text_cursor;

    const bool is_source = ctx->text_drag.active && ctx->text_drag.source_id == id;
    i32 drop_index = -1;
    if (ctx->text_drag.active && in.hovered && !is_source)
        drop_index = static_cast<i32>(detail::index_at_x(
            *this, st.buffer, ctx->mouse_x - (r.x + t.padding * 0.5f) + st.scroll_x));

    const f32 bt = st.focused ? t.focus_border_thickness : t.border_thickness;
    const color bc = st.focused ? t.focus_border : t.border;
    detail::draw_edit_body(*this, r, bc, bt, st, st.focused ? st.buffer : value, st.focused,
                           drop_index, is_source, ctx->text_drag.lo, ctx->text_drag.hi);
    return changed;
}

bool ui::number_field(rect r, f32 &value, uiid id, const char *fmt)
{
    const theme &t = ctx->active_theme;
    interaction in = interact(id, r);
    edit_state &st = detail::edit_for(ctx, id);

    if (!st.focused)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), fmt ? fmt : "%.2f", static_cast<double>(value));
        st.buffer = buf;
        if (t.decimal_separator != '.')
        {
            for (char &ch : st.buffer)
                if (ch == '.') ch = t.decimal_separator;
        }
        st.caret = st.buffer.size();
        st.anchor = st.caret;
    }

    const bool edited = detail::edit_buffer(*this, id, r, in, st);
    bool changed = false;
    bool valid = true;
    // Parse while focused, and also after a framework edit (a text drop) so an
    // unfocused target does not lose the inserted text on its next reload.
    if (st.focused || edited)
    {
        f32 parsed = value;
        valid = detail::parse_float(st.buffer, parsed);
        if (valid && parsed != value)
        {
            value = parsed;
            changed = true;
        }
    }

    const bool strong = st.focused || !valid;
    const f32 bt = strong ? max2(t.border_thickness, t.focus_border_thickness) : t.border_thickness;
    const color bc = !valid ? color{220, 70, 70, 255} : (st.focused ? t.focus_border : t.border);
    if (in.hovered) ctx->want_cursor = t.text_cursor;

    const bool is_source = ctx->text_drag.active && ctx->text_drag.source_id == id;
    i32 drop_index = -1;
    if (ctx->text_drag.active && in.hovered && !is_source)
        drop_index = static_cast<i32>(detail::index_at_x(
            *this, st.buffer, ctx->mouse_x - (r.x + t.padding * 0.5f) + st.scroll_x));

    detail::draw_edit_body(*this, r, bc, bt, st, st.buffer, st.focused, drop_index, is_source,
                           ctx->text_drag.lo, ctx->text_drag.hi);
    return changed;
}

// ---- draw primitives ----
void ui::draw_triangles(texture_handle tex, const vertex *vertices, i32 vertex_count,
                        const i32 *indices, i32 index_count)
{
    if (vertex_count <= 0 || index_count <= 0) return;
    // Cull whole batches that cannot paint: when a clip is active, a batch
    // whose bounding box misses the clip is dropped here — the renderer never
    // sees it (this is what keeps clipped-out scroll/panel content cheap).
    if (ctx->clip_depth > 0)
    {
        // Cull only against a real clip: a degenerate one (w/h 0 — possible
        // when a sibling clipped scope is still alive) renders "nothing" on
        // GPU scissors but passes through on the software renderer's quirk;
        // either way that behavior belongs to the renderer, not to us.
        const rect clip = ctx->clip_stack[ctx->clip_depth - 1];
        if (clip.w > 0.0f && clip.h > 0.0f)
        {
            f32 minx = vertices[0].x, maxx = minx, miny = vertices[0].y, maxy = miny;
            for (i32 i = 1; i < vertex_count; ++i)
            {
                minx = min2(minx, vertices[i].x);
                maxx = max2(maxx, vertices[i].x);
                miny = min2(miny, vertices[i].y);
                maxy = max2(maxy, vertices[i].y);
            }
            const rect aabb{minx, miny, maxx - minx, maxy - miny};
            const rect isect = rect::intersect(aabb, clip);
            if (isect.w <= 0.0f || isect.h <= 0.0f) return;
        }
    }
    // Solid geometry (tex == nullptr) joins the glyph batch when an atlas
    // exists: the atlas's reserved (0,0) texel is pure white, and dl_add
    // remaps the copied verts' UVs to it. Without an atlas (no font loaded)
    // the old nullptr-batch behavior stands.
    if (ctx->dl && tex == nullptr && ctx->ts && !ctx->ts->atlases.empty())
    {
        tex = ctx->ts->atlases.front().tex;
        ctx->dl->solid_uv_remap = true;
    }
    const rect *clip = (ctx->clip_depth > 0) ? &ctx->clip_stack[ctx->clip_depth - 1] : nullptr;
    detail::dl_prepare(ctx, tex, clip);
    detail::dl_add(ctx, vertices, vertex_count, indices, index_count);
    if (ctx->dl) ctx->dl->solid_uv_remap = false;
}

void ui::draw_rect(rect r, color c)
{
    PUFFERUI_CHECK(VIOL_INVALID_RECT, r.is_valid(), "draw_rect with invalid rect");
    vertex v[4] = {
        {r.x, r.y, 0.0f, 0.0f, c},
        {r.right(), r.y, 1.0f, 0.0f, c},
        {r.right(), r.bottom(), 1.0f, 1.0f, c},
        {r.x, r.bottom(), 0.0f, 1.0f, c},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    draw_triangles(nullptr, v, 4, idx, 6);
}

void ui::draw_line(f32 x0, f32 y0, f32 x1, f32 y1, color c, f32 thickness)
{
    // Axis-aligned lines stay flat rects; anything else (a diagonal, a dot)
    // goes through detail::thick_line's half-plane split, so a diagonal segment
    // is a real drawn line instead of silently drawing nothing.
    const f32 dx = x1 - x0, dy = y1 - y0;
    if (dx == 0.0f && dy == 0.0f)
    {
        // A dot: a small square centered on the point.
        const f32 h = thickness * 0.5f;
        draw_rect({x0 - h, y0 - h, thickness, thickness}, c);
        return;
    }
    if (y0 == y1)
    {
        draw_rect({min2(x0, x1), y0 - thickness * 0.5f, (dx > 0.0f ? dx : -dx), thickness}, c);
        return;
    }
    if (x0 == x1)
    {
        draw_rect({x0 - thickness * 0.5f, min2(y0, y1), thickness, (dy > 0.0f ? dy : -dy)}, c);
        return;
    }
    detail::thick_line(*this, {x0, y0}, {x1, y1}, thickness, c);
}

void ui::draw_rounded_rect(rect r, color c, f32 radius)
{
    PUFFERUI_CHECK(VIOL_INVALID_RECT, r.is_valid(), "draw_rounded_rect with invalid rect");
    if (radius <= 0.5f)
    {
        draw_rect(r, c);
        return;
    }

    f32 max_r = min2(r.w, r.h) * 0.5f;
    if (radius > max_r) radius = max_r;

    const i32 SEG = 8;
    const f32 aa = 1.0f;
    const f32 r_in = max2(0.0f, radius - aa * 0.5f);
    const f32 r_out = radius + aa * 0.5f;

    struct corner
    {
        f32 cx, cy;
        i32 dx, dy;
    };
    const corner corners[4] = {
        {r.right() - radius, r.bottom() - radius, 1, 0},
        {r.x + radius, r.bottom() - radius, 0, 1},
        {r.x + radius, r.y + radius, -1, 0},
        {r.right() - radius, r.y + radius, 0, -1},
    };

    const i32 N = 4 * (SEG + 1);
    std::vector<vertex> verts;
    std::vector<i32> idx;
    verts.reserve(static_cast<usize>(1 + 2 * N));
    idx.reserve(static_cast<usize>(9 * N));

    color c_out = c;
    c_out.a = 0;
    verts.push_back({r.center_x(), r.center_y(), 0.0f, 0.0f, c});
    const i32 ring0 = static_cast<i32>(verts.size());

    for (i32 ring = 0; ring < 2; ++ring)
    {
        const f32 rad = ring == 0 ? r_in : r_out;
        const color col = ring == 0 ? c : c_out;
        for (i32 ci = 0; ci < 4; ++ci)
        {
            for (i32 s = 0; s <= SEG; ++s)
            {
                const f32 ang = (PI * 0.5f) * (static_cast<f32>(s) / static_cast<f32>(SEG));
                const f32 cs = cosf(ang), sn = sinf(ang);
                const f32 dx = cs * corners[ci].dx - sn * corners[ci].dy;
                const f32 dy = sn * corners[ci].dx + cs * corners[ci].dy;
                verts.push_back(
                    {corners[ci].cx + dx * rad, corners[ci].cy + dy * rad, 0.0f, 0.0f, col});
            }
        }
    }
    const i32 ring1 = ring0 + N;

    for (i32 i = 0; i < N; ++i)
    {
        const i32 nxt = (i + 1) % N;
        idx.push_back(0);
        idx.push_back(ring0 + i);
        idx.push_back(ring0 + nxt);
    }
    for (i32 i = 0; i < N; ++i)
    {
        const i32 nxt = (i + 1) % N;
        const i32 in1 = ring0 + i, in2 = ring0 + nxt;
        const i32 out1 = ring1 + i, out2 = ring1 + nxt;
        idx.push_back(in1);
        idx.push_back(out1);
        idx.push_back(out2);
        idx.push_back(in1);
        idx.push_back(out2);
        idx.push_back(in2);
    }

    draw_triangles(nullptr, verts.data(), static_cast<i32>(verts.size()), idx.data(),
                   static_cast<i32>(idx.size()));
}

void ui::draw_image(const skin_image &img, rect dst)
{
    if (!img.valid() || dst.w <= 0.0f || dst.h <= 0.0f) return;
    PUFFERUI_CHECK(VIOL_INVALID_RECT, dst.is_valid(), "draw_image with invalid rect");
    vertex v[4] = {
        {dst.x, dst.y, img.uv.x, img.uv.y, color::white()},
        {dst.right(), dst.y, img.uv.right(), img.uv.y, color::white()},
        {dst.right(), dst.bottom(), img.uv.right(), img.uv.bottom(), color::white()},
        {dst.x, dst.bottom(), img.uv.x, img.uv.bottom(), color::white()},
    };
    i32 idx[6] = {0, 1, 2, 0, 2, 3};
    draw_triangles(img.tex, v, 4, idx, 6);
}

void ui::draw_nine_slice(const skin_image &img, rect dst)
{
    if (!img.valid() || dst.w <= 0.0f || dst.h <= 0.0f) return;
    if (img.src_w <= 0.0f || img.src_h <= 0.0f) return;
    PUFFERUI_CHECK(VIOL_INVALID_RECT, dst.is_valid(), "draw_nine_slice with invalid rect");

    // Source slices (pixels), clamped so the border insets never overlap.
    const f32 l = clampf(img.inset_l, 0.0f, img.src_w);
    const f32 r = clampf(img.inset_r, 0.0f, img.src_w - l);
    const f32 t = clampf(img.inset_t, 0.0f, img.src_h);
    const f32 b = clampf(img.inset_b, 0.0f, img.src_h - t);
    const f32 cw = img.src_w - l - r;
    const f32 ch = img.src_h - t - b;

    // Destination corner sizes: 1:1 with the source, clamped into the rect.
    const f32 dl = min2(l, dst.w * 0.5f);
    const f32 dr = min2(r, dst.w * 0.5f);
    const f32 dt = min2(t, dst.h * 0.5f);
    const f32 db = min2(b, dst.h * 0.5f);
    const f32 mid_w = max2(0.0f, dst.w - dl - dr);
    const f32 mid_h = max2(0.0f, dst.h - dt - db);

    const f32 us = img.uv.w / img.src_w; // uv per source pixel
    const f32 vs = img.uv.h / img.src_h;

    auto quad = [&](f32 x, f32 y, f32 w, f32 h, f32 su, f32 sv, f32 sw, f32 sh)
    {
        if (w <= 0.0f || h <= 0.0f || sw <= 0.0f || sh <= 0.0f) return;
        const f32 ua = img.uv.x + su * us, ub = img.uv.x + (su + sw) * us;
        const f32 va = img.uv.y + sv * vs, vb = img.uv.y + (sv + sh) * vs;
        vertex v[4] = {
            {x, y, ua, va, color::white()},
            {x + w, y, ub, va, color::white()},
            {x + w, y + h, ub, vb, color::white()},
            {x, y + h, ua, vb, color::white()},
        };
        i32 idx[6] = {0, 1, 2, 0, 2, 3};
        draw_triangles(img.tex, v, 4, idx, 6);
    };

    // Tile/stretch one source span along one axis: fn(dst_off, dst_len, src_off, src_len).
    auto spans = [&](f32 dst_total, f32 src_total, auto &&fn)
    {
        if (dst_total <= 0.0f || src_total <= 0.0f) return;
        if (img.center_mode == SKIN_CENTER_STRETCH)
        {
            fn(0.0f, dst_total, 0.0f, src_total);
        }
        else if (img.center_mode == SKIN_CENTER_NONE)
        {
            const f32 len = min2(dst_total, src_total);
            fn(0.0f, len, 0.0f, len);
        }
        else
        { // SKIN_CENTER_TILE
            for (f32 x = 0.0f; x < dst_total; x += src_total)
            {
                const f32 len = min2(src_total, dst_total - x);
                fn(x, len, 0.0f, len);
            }
        }
    };

    // 4 corners (never scaled)
    quad(dst.x, dst.y, dl, dt, 0.0f, 0.0f, l, t);
    quad(dst.right() - dr, dst.y, dr, dt, l + cw, 0.0f, r, t);
    quad(dst.x, dst.bottom() - db, dl, db, 0.0f, t + ch, l, b);
    quad(dst.right() - dr, dst.bottom() - db, dr, db, l + cw, t + ch, r, b);

    // 4 edges (tile/stretch along their varying axis)
    spans(mid_h, ch, [&](f32 oy, f32 oh, f32 soy, f32 soh)
          { quad(dst.x, dst.y + dt + oy, dl, oh, 0.0f, t + soy, l, soh); });
    spans(mid_h, ch, [&](f32 oy, f32 oh, f32 soy, f32 soh)
          { quad(dst.right() - dr, dst.y + dt + oy, dr, oh, l + cw, t + soy, r, soh); });
    spans(mid_w, cw, [&](f32 ox, f32 ow, f32 sox, f32 sow)
          { quad(dst.x + dl + ox, dst.y, ow, dt, l + sox, 0.0f, sow, t); });
    spans(mid_w, cw, [&](f32 ox, f32 ow, f32 sox, f32 sow)
          { quad(dst.x + dl + ox, dst.bottom() - db, ow, db, l + sox, t + ch, sow, b); });

    // center (tile/stretch on both axes)
    spans(mid_w, cw,
          [&](f32 ox, f32 ow, f32 sox, f32 sow)
          {
              spans(
                  mid_h, ch, [&](f32 oy, f32 oh, f32 soy, f32 soh)
                  { quad(dst.x + dl + ox, dst.y + dt + oy, ow, oh, l + sox, t + soy, sow, soh); });
          });
}

void ui::draw_polygon(std::span<const vec2> points, color c)
{
    if (points.size() < 3) return;
    const usize n = points.size();
    vec2 ctr{0.0f, 0.0f};
    for (const vec2 &p : points)
    {
        ctr.x += p.x;
        ctr.y += p.y;
    }
    ctr.x /= static_cast<f32>(n);
    ctr.y /= static_cast<f32>(n);

    color c_out = c;
    c_out.a = 0;

    // center + inner ring (the polygon) + a 1px feathered outer ring
    if (!ctx->dl) return;
    std::vector<vertex> &verts = ctx->dl->scratch_v;
    std::vector<i32> &idx = ctx->dl->scratch_i;
    verts.clear();
    idx.clear();
    verts.reserve(2 * n + 1);
    verts.push_back({ctr.x, ctr.y, 0.0f, 0.0f, c});
    for (const vec2 &p : points) verts.push_back({p.x, p.y, 0.0f, 0.0f, c});
    for (usize i = 0; i < n; ++i)
    {
        const vec2 &a = points[(i + n - 1) % n];
        const vec2 &b = points[i];
        const vec2 &d = points[(i + 1) % n];
        const vec2 e1{b.x - a.x, b.y - a.y};
        const vec2 e2{d.x - b.x, d.y - b.y};
        const f32 l1 = max2(std::sqrt(e1.x * e1.x + e1.y * e1.y), 0.001f);
        const f32 l2 = max2(std::sqrt(e2.x * e2.x + e2.y * e2.y), 0.001f);
        vec2 nrm{e1.y / l1 + e2.y / l2, -e1.x / l1 - e2.x / l2};
        const f32 ln = max2(std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y), 0.001f);
        nrm.x /= ln;
        nrm.y /= ln;
        // point away from the centroid (assumes a convex polygon)
        if (nrm.x * (b.x - ctr.x) + nrm.y * (b.y - ctr.y) < 0.0f)
        {
            nrm.x = -nrm.x;
            nrm.y = -nrm.y;
        }
        verts.push_back({b.x + nrm.x, b.y + nrm.y, 0.0f, 0.0f, c_out});
    }

    idx.reserve(n * 6 + n * 6);
    const i32 inner0 = 1, outer0 = 1 + static_cast<i32>(n);
    for (usize i = 0; i < n; ++i)
    {
        const i32 in0 = inner0 + static_cast<i32>(i);
        const i32 in1 = inner0 + static_cast<i32>((i + 1) % n);
        idx.push_back(0);
        idx.push_back(in0);
        idx.push_back(in1);
    }
    for (usize i = 0; i < n; ++i)
    {
        const i32 in0 = inner0 + static_cast<i32>(i);
        const i32 in1 = inner0 + static_cast<i32>((i + 1) % n);
        const i32 out0 = outer0 + static_cast<i32>(i);
        const i32 out1 = outer0 + static_cast<i32>((i + 1) % n);
        idx.push_back(in0);
        idx.push_back(out0);
        idx.push_back(out1);
        idx.push_back(in0);
        idx.push_back(out1);
        idx.push_back(in1);
    }
    draw_triangles(nullptr, verts.data(), static_cast<i32>(verts.size()), idx.data(),
                   static_cast<i32>(idx.size()));
}

void ui::draw_sector(vec2 center, f32 r_in, f32 r_out, f32 a0, f32 a1, color c)
{
    if (a1 < a0)
    {
        const f32 t = a0;
        a0 = a1;
        a1 = t;
    }
    if (a1 - a0 <= 0.0f || r_out <= 0.0f || r_out <= r_in) return; // degenerate: nothing to fill
    const f32 mid = (r_in + r_out) * 0.5f;
    const i32 seg = static_cast<i32>(max2(3.0f, std::ceil((a1 - a0) * max2(2.0f, mid) * 0.25f)));

    color c_out = c;
    c_out.a = 0;

    // rings: inner feather (optional), inner edge, outer edge, outer feather
    struct ring
    {
        f32 r;
        color col;
    };
    ring rings[4];
    i32 rc = 0;
    if (r_in > 1.0f) rings[rc++] = {r_in - 1.0f, c_out};
    rings[rc++] = {r_in, c};
    rings[rc++] = {r_out, c};
    rings[rc++] = {r_out + 1.0f, c_out};

    std::vector<vertex> &verts = ctx->dl->scratch_v;
    std::vector<i32> &idx = ctx->dl->scratch_i;
    verts.clear();
    idx.clear();
    verts.reserve(static_cast<usize>(rc) * static_cast<usize>(seg + 1));

    // Rotation recurrence: two trig evaluations per CALL instead of two per
    // vertex — each step rotates the radius vector by the fixed step angle.
    // Accumulated drift for UI-sized arcs is ~1e-4 px (subpixel).
    const f32 step = (a1 - a0) / static_cast<f32>(seg);
    const f32 cs = std::cos(step), sn = std::sin(step);
    for (i32 ri = 0; ri < rc; ++ri)
    {
        f32 x = std::cos(a0) * rings[ri].r, y = std::sin(a0) * rings[ri].r;
        for (i32 s = 0; s <= seg; ++s)
        {
            verts.push_back({center.x + x, center.y + y, 0.0f, 0.0f, rings[ri].col});
            const f32 nx = x * cs - y * sn;
            y = x * sn + y * cs;
            x = nx;
        }
    }

    idx.reserve(static_cast<usize>(rc - 1) * static_cast<usize>(seg) * 6);
    for (i32 ri = 0; ri < rc - 1; ++ri)
    {
        const i32 r0 = ri * (seg + 1), r1 = (ri + 1) * (seg + 1);
        for (i32 s = 0; s < seg; ++s)
        {
            const i32 i0 = r0 + s, i1 = r0 + s + 1, o0 = r1 + s, o1 = r1 + s + 1;
            idx.push_back(i0);
            idx.push_back(o0);
            idx.push_back(o1);
            idx.push_back(i0);
            idx.push_back(o1);
            idx.push_back(i1);
        }
    }
    draw_triangles(nullptr, verts.data(), static_cast<i32>(verts.size()), idx.data(),
                   static_cast<i32>(idx.size()));
}

void ui::draw_arc(vec2 center, f32 radius, f32 thickness, f32 a0, f32 a1, color c)
{
    draw_sector(center, max2(0.0f, radius - thickness * 0.5f), radius + thickness * 0.5f, a0, a1,
                c);
}

namespace detail
{

inline blur_store *ensure_blur(context *c, i32 ow, i32 oh)
{
    if (!c->device) return nullptr;
    if (!c->blur) c->blur = new blur_store();
    blur_store *bs = static_cast<blur_store *>(c->blur);
    if (bs->w == ow && bs->h == oh && bs->half && bs->quarter) return bs;

    if (bs->half) c->device->destroy_target(bs->half);
    if (bs->quarter) c->device->destroy_target(bs->quarter);
    if (bs->eighth) c->device->destroy_target(bs->eighth);
    if (bs->sixteenth) c->device->destroy_target(bs->sixteenth);

    const i32 hw = (ow + 1) / 2, hh = (oh + 1) / 2;
    const i32 qw = (hw + 1) / 2, qh = (hh + 1) / 2;
    const i32 ew = (qw + 1) / 2, eh = (qh + 1) / 2;
    const i32 sw = (ew + 1) / 2, sh = (eh + 1) / 2;

    bs->half = c->device->create_target(hw, hh);
    bs->quarter = c->device->create_target(qw, qh);
    bs->eighth = c->device->create_target(ew, eh);
    bs->sixteenth = c->device->create_target(sw, sh);
    bs->w = ow;
    bs->h = oh;
    return (bs->half && bs->quarter) ? bs : nullptr;
}

} // namespace detail

void ui::blur(rect r, f32 blur_radius, f32 corner_radius, f32 alpha)
{
    context *c = ctx;
    if (blur_radius < 0.5f || alpha <= 0.0f) return;
    render_surface *surf = c->current_window ? c->current_window->surface : c->surface;
    if (!c->device || !surf) return;
    PUFFERUI_CHECK(VIOL_INVALID_RECT, r.is_valid(), "blur with invalid rect");

    // No render targets: defined fallback -> solid translucent tint.
    if (!has_cap(c->device->caps(), backend_caps::RENDER_TARGETS))
    {
        draw_rounded_rect(r, color{20, 24, 30, static_cast<u8>(alpha * 255.0f)}, corner_radius);
        return;
    }

    texture_handle scene = surf->scene_target();
    if (!scene) return;

    i32 ow = 0, oh = 0;
    surf->output_size(ow, oh);
    if (ow <= 2 || oh <= 2) return;

    detail::dl_flush(c); // everything drawn so far must land in the scene

    const i32 margin = static_cast<i32>(max2(0.0f, min2(blur_radius, 16.0f)));
    i32 rx = static_cast<i32>(std::floor(r.x)) - margin;
    i32 ry = static_cast<i32>(std::floor(r.y)) - margin;
    i32 rw = static_cast<i32>(std::ceil(r.right())) + margin - rx;
    i32 rh = static_cast<i32>(std::ceil(r.bottom())) + margin - ry;
    if (rx < 0)
    {
        rw += rx;
        rx = 0;
    }
    if (ry < 0)
    {
        rh += ry;
        ry = 0;
    }
    if (rx + rw > ow) rw = ow - rx;
    if (ry + rh > oh) rh = oh - ry;
    if (rw <= 2 || rh <= 2) return;

    blur_store *bs = detail::ensure_blur(c, ow, oh);
    if (!bs) return;

    const f32 fw = static_cast<f32>(rw), fh = static_cast<f32>(rh);
    const rect src{static_cast<f32>(rx), static_cast<f32>(ry), fw, fh};
    const rect half_dst{0.0f, 0.0f, fw * 0.5f, fh * 0.5f};
    const rect quarter_dst{0.0f, 0.0f, fw * 0.25f, fh * 0.25f};
    const rect eighth_dst{0.0f, 0.0f, fw * 0.125f, fh * 0.125f};
    const rect sixteenth_dst{0.0f, 0.0f, fw * 0.0625f, fh * 0.0625f};

    // A scaled blit is the only sampling primitive a backend must provide, so the
    // requested radius is approximated by how deep the pyramid goes (each level
    // roughly doubles the smoothing) plus extra down/up round trips at that level
    // for finer steps. The response is monotonic but stepwise, not linear.
    const i32 level = (blur_radius > 20.0f && bs->sixteenth) ? 4
                      : (blur_radius > 4.0f && bs->eighth)   ? 3
                                                             : 2;
    i32 extra = 0;
    if (level == 2)
        extra = static_cast<i32>(blur_radius - 2.0f);
    else if (level == 3)
        extra = static_cast<i32>((blur_radius - 6.0f) / 4.0f);
    else
        extra = static_cast<i32>((blur_radius - 20.0f) / 8.0f);
    if (extra < 0) extra = 0;
    if (extra > 4) extra = 4;

    c->device->blit(scene, &src, bs->half, &half_dst);
    c->device->blit(bs->half, &half_dst, bs->quarter, &quarter_dst);
    if (level >= 3) c->device->blit(bs->quarter, &quarter_dst, bs->eighth, &eighth_dst);
    if (level >= 4) c->device->blit(bs->eighth, &eighth_dst, bs->sixteenth, &sixteenth_dst);

    for (i32 i = 0; i < extra; ++i)
    {
        if (level >= 4)
        {
            c->device->blit(bs->sixteenth, &sixteenth_dst, bs->eighth, &eighth_dst);
            c->device->blit(bs->eighth, &eighth_dst, bs->sixteenth, &sixteenth_dst);
        }
        else if (level >= 3)
        {
            c->device->blit(bs->eighth, &eighth_dst, bs->quarter, &quarter_dst);
            c->device->blit(bs->quarter, &quarter_dst, bs->eighth, &eighth_dst);
        }
        else
        {
            c->device->blit(bs->quarter, &quarter_dst, bs->half, &half_dst);
            c->device->blit(bs->half, &half_dst, bs->quarter, &quarter_dst);
        }
    }

    // Upsample back to the quarter, which the composite samples.
    if (level >= 4) c->device->blit(bs->sixteenth, &sixteenth_dst, bs->eighth, &eighth_dst);
    if (level >= 3) c->device->blit(bs->eighth, &eighth_dst, bs->quarter, &quarter_dst);

    // composite the blurred quarter back over the scene
    c->device->set_target(scene);
    const i32 qw = (ow + 3) / 4, qh = (oh + 3) / 4;
    // The quarter texture's origin is the *source* rect, which sits `margin`
    // pixels up/left of the user rect (sampling must reach outside for a correct
    // blur). UVs therefore come from each vertex's scene position - using 0..1/pts
    // would shift the blurred image by `margin` and pull outside content in.
    const f32 du = 0.25f / static_cast<f32>(qw);
    const f32 dv = 0.25f / static_cast<f32>(qh);
    const f32 ox = static_cast<f32>(rx), oy = static_cast<f32>(ry);
    const color col{255, 255, 255, static_cast<u8>(clampf(alpha * 255.0f, 0.0f, 255.0f))};

    const f32 cr = clampf(corner_radius, 0.0f, min2(r.w, r.h) * 0.5f);
    if (cr <= 0.5f)
    {
        vertex v[4] = {
            {r.x, r.y, (r.x - ox) * du, (r.y - oy) * dv, col},
            {r.right(), r.y, (r.right() - ox) * du, (r.y - oy) * dv, col},
            {r.right(), r.bottom(), (r.right() - ox) * du, (r.bottom() - oy) * dv, col},
            {r.x, r.bottom(), (r.x - ox) * du, (r.bottom() - oy) * dv, col},
        };
        i32 idx[6] = {0, 1, 2, 0, 2, 3};
        c->device->draw(bs->quarter, v, 4, idx, 6);
        return;
    }

    // Rounded composite: the same fan + 1px feather ring draw_rounded_rect uses,
    // with UVs taken from each vertex's scene position.
    const i32 SEG = 8;
    const f32 r_in = max2(0.0f, cr - 0.5f);
    const f32 r_out = cr + 0.5f;
    struct corner
    {
        f32 cx, cy;
        i32 dx, dy;
    };
    const corner corners[4] = {
        {r.right() - cr, r.bottom() - cr, 1, 0},
        {r.x + cr, r.bottom() - cr, 0, 1},
        {r.x + cr, r.y + cr, -1, 0},
        {r.right() - cr, r.y + cr, 0, -1},
    };
    const i32 N = 4 * (SEG + 1);
    std::vector<vertex> verts;
    std::vector<i32> idx;
    verts.reserve(static_cast<usize>(1 + 2 * N));
    idx.reserve(static_cast<usize>(9 * N));

    auto uv_at = [&](f32 x, f32 y, f32 &out_u, f32 &out_v)
    {
        out_u = (x - ox) * du;
        out_v = (y - oy) * dv;
    };

    f32 cu = 0.0f, cv = 0.0f;
    uv_at(r.center_x(), r.center_y(), cu, cv);
    verts.push_back({r.center_x(), r.center_y(), cu, cv, col});
    const i32 ring0 = static_cast<i32>(verts.size());

    color c_out = col;
    c_out.a = 0;
    for (i32 ring = 0; ring < 2; ++ring)
    {
        const f32 rad = ring == 0 ? r_in : r_out;
        const color cc = ring == 0 ? col : c_out;
        for (i32 ci = 0; ci < 4; ++ci)
        {
            for (i32 s = 0; s <= SEG; ++s)
            {
                const f32 ang = (PI * 0.5f) * (static_cast<f32>(s) / static_cast<f32>(SEG));
                const f32 cs = cosf(ang), sn = sinf(ang);
                const f32 dx = cs * corners[ci].dx - sn * corners[ci].dy;
                const f32 dy = sn * corners[ci].dx + cs * corners[ci].dy;
                const f32 px = corners[ci].cx + dx * rad;
                const f32 py = corners[ci].cy + dy * rad;
                f32 uu = 0.0f, vv = 0.0f;
                uv_at(px, py, uu, vv);
                verts.push_back({px, py, uu, vv, cc});
            }
        }
    }
    const i32 ring1 = ring0 + N;

    for (i32 i = 0; i < N; ++i)
    {
        const i32 nxt = (i + 1) % N;
        idx.push_back(0);
        idx.push_back(ring0 + i);
        idx.push_back(ring0 + nxt);
    }
    for (i32 i = 0; i < N; ++i)
    {
        const i32 nxt = (i + 1) % N;
        const i32 in1 = ring0 + i, in2 = ring0 + nxt;
        const i32 out1 = ring1 + i, out2 = ring1 + nxt;
        idx.push_back(in1);
        idx.push_back(out1);
        idx.push_back(out2);
        idx.push_back(in1);
        idx.push_back(out2);
        idx.push_back(in2);
    }
    c->device->draw(bs->quarter, verts.data(), static_cast<i32>(verts.size()), idx.data(),
                    static_cast<i32>(idx.size()));
}

// ---- SDL3 device + surface ----
#if defined(PUFFERUI_ENABLE_SDL3)
namespace
{

// A device texture. Streaming textures keep a CPU mirror so every surface's
// renderer can get its own SDL_Texture copy (SDL textures are renderer-scoped);
// the core still sees one shared handle. Targets are slot-local scratch.
struct sdl3_texture
{
    i32 w = 0, h = 0;
    bool target = false;               // render target (no CPU mirror)
    bool streaming = false;            // CPU-mirrored (atlas, images)
    std::vector<u8> pixels;            // streaming: RGBA mirror
    std::vector<SDL_Texture *> copies; // one per device slot
};

struct sdl3_device;

struct sdl3_surface : render_surface
{
    sdl3_device *dev = nullptr;
    i32 slot = -1;

    void make_current(render_device &device) override;
    texture_handle scene_target() override;
    void output_size(i32 &w, i32 &h) override;
    void present() override;
};

struct sdl3_device : render_device
{
    struct slot
    {
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        bool owns_renderer = false;
        texture_handle scene = nullptr;
        i32 scene_w = 0, scene_h = 0;
    };

    std::vector<slot> slots;
    std::vector<sdl3_surface *> surfaces;
    std::vector<sdl3_texture *> textures;
    SDL_Renderer *primary_renderer = nullptr;
    bool primary_claimed = false;
    bool vsync_enabled = true;
    i32 current_slot = -1;
    texture_handle current_target_ = nullptr;
    // draw(): the only converted part (u8 -> float colors). The slots are
    // padded to sizeof(vertex) because SDL's software geometry path reads UVs
    // with the color stride (SDL_SW_RenderGeometryRaw) — heterogeneous
    // strides feed it garbage, so all three strides must be equal.
    struct padded_color
    {
        SDL_FColor col{};
        f32 pad_ = 0.0f;
    };
    static_assert(sizeof(padded_color) == sizeof(vertex), "strides must match");
    std::vector<padded_color> colors_scratch;
    SDL_Cursor *cursors[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

    ~sdl3_device() override
    {
        for (SDL_Cursor *cu : cursors)
            if (cu) SDL_DestroyCursor(cu);
        for (sdl3_texture *t : textures)
        {
            for (SDL_Texture *c : t->copies)
                if (c) SDL_DestroyTexture(c);
            delete t;
        }
        for (sdl3_surface *s : surfaces) delete s;
        for (slot &sl : slots)
            if (sl.owns_renderer && sl.renderer) SDL_DestroyRenderer(sl.renderer);
    }

    SDL_Renderer *renderer() const
    {
        if (current_slot >= 0 && current_slot < static_cast<i32>(slots.size()))
            return slots[static_cast<usize>(current_slot)].renderer;
        return primary_renderer;
    }

    SDL_Texture *resolve(sdl3_texture *t)
    {
        if (!t || current_slot < 0 || current_slot >= static_cast<i32>(slots.size()))
            return nullptr;
        if (static_cast<i32>(t->copies.size()) < static_cast<i32>(slots.size()))
            t->copies.resize(slots.size(), nullptr);
        SDL_Texture *&tex = t->copies[static_cast<usize>(current_slot)];
        if (tex) return tex;
        SDL_Renderer *ren = renderer();
        if (!ren) return nullptr;
        if (t->target)
        {
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, t->w,
                                    t->h);
            if (tex)
            {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
            }
        }
        else
        {
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, t->w,
                                    t->h);
            if (tex)
            {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
                if (!t->pixels.empty()) SDL_UpdateTexture(tex, nullptr, t->pixels.data(), t->w * 4);
            }
        }
        return tex;
    }

    texture_handle make_texture(i32 w, i32 h, bool target, const u8 *rgba)
    {
        if (w <= 0 || h <= 0) return nullptr;
        auto *t = new sdl3_texture();
        t->w = w;
        t->h = h;
        t->target = target;
        t->streaming = !target;
        t->copies.resize(slots.size(), nullptr);
        if (!target)
        {
            t->pixels.assign(static_cast<usize>(w) * static_cast<usize>(h) * 4, 0);
            if (rgba) std::memcpy(t->pixels.data(), rgba, t->pixels.size());
        }
        textures.push_back(t);
        return reinterpret_cast<texture_handle>(t);
    }

    backend_caps caps() const override
    {
        return backend_caps::RENDER_TARGETS | backend_caps::SCISSOR |
               backend_caps::STREAMING_TEXTURES | backend_caps::SHARED_DEVICE;
    }

    render_surface *create_surface(void *native_window) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(native_window);
        for (sdl3_surface *s : surfaces)
            if (s->slot >= 0 && slots[static_cast<usize>(s->slot)].window == win && win) return s;

        SDL_Renderer *ren = nullptr;
        bool owns = false;
        if (!primary_claimed && primary_renderer)
        {
            ren = primary_renderer;
            primary_claimed = true;
            if (!win) win = SDL_GetRenderWindow(ren);
            SDL_SetRenderVSync(ren, vsync_enabled ? 1 : 0);
        }
        else
        {
            if (!win) return nullptr;
            ren = SDL_CreateRenderer(win, nullptr);
            if (!ren) return nullptr;
            owns = true;
            SDL_SetRenderVSync(ren, vsync_enabled ? 1 : 0);
        }

        auto *s = new sdl3_surface();
        s->dev = this;
        s->slot = static_cast<i32>(slots.size());
        slot sl;
        sl.window = win;
        sl.renderer = ren;
        sl.owns_renderer = owns;
        slots.push_back(sl);
        surfaces.push_back(s);
        return s;
    }

    void ensure_scene(i32 slot_index, i32 w, i32 h)
    {
        if (slot_index < 0 || slot_index >= static_cast<i32>(slots.size())) return;
        slot &sl = slots[static_cast<usize>(slot_index)];
        if (sl.scene && sl.scene_w == w && sl.scene_h == h) return;
        if (sl.scene)
        {
            destroy_texture(sl.scene);
            sl.scene = nullptr;
        }
        sl.scene = make_texture(w, h, true, nullptr);
        sl.scene_w = w;
        sl.scene_h = h;
    }

    void begin_frame() override {}
    void end_frame() override {}

    void set_vsync(bool enabled) override
    {
        vsync_enabled = enabled;
        for (slot &sl : slots)
            if (sl.renderer) SDL_SetRenderVSync(sl.renderer, enabled ? 1 : 0);
    }

    void clear(color col) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren) return;
        SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
        SDL_RenderClear(ren);
    }

    void set_cursor(cursor c) override
    {
        const int i = static_cast<int>(c);
        if (i < 0 || i > 4) return;
        if (!cursors[i])
        {
            SDL_SystemCursor sc = SDL_SYSTEM_CURSOR_DEFAULT;
            switch (c)
            {
            case cursor::IBEAM:
                sc = SDL_SYSTEM_CURSOR_TEXT;
                break;
            case cursor::HAND:
                sc = SDL_SYSTEM_CURSOR_POINTER;
                break;
            case cursor::HRESIZE:
                sc = SDL_SYSTEM_CURSOR_EW_RESIZE;
                break;
            case cursor::VRESIZE:
                sc = SDL_SYSTEM_CURSOR_NS_RESIZE;
                break;
            default:
                sc = SDL_SYSTEM_CURSOR_DEFAULT;
                break;
            }
            cursors[i] = SDL_CreateSystemCursor(sc);
        }
        if (cursors[i]) SDL_SetCursor(cursors[i]);
    }

    void set_clip(const rect *clip) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren) return;
        if (clip && clip->w > 0.0f && clip->h > 0.0f)
        {
            SDL_Rect r{static_cast<int>(clip->x), static_cast<int>(clip->y),
                       static_cast<int>(clip->w), static_cast<int>(clip->h)};
            SDL_SetRenderClipRect(ren, &r);
        }
        else
        {
            SDL_SetRenderClipRect(ren, nullptr);
        }
    }

    void set_target(texture_handle target) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren) return;
        SDL_SetRenderTarget(ren,
                            target ? resolve(reinterpret_cast<sdl3_texture *>(target)) : nullptr);
        current_target_ = target;
    }
    texture_handle current_target() const override { return current_target_; }

    texture_handle create_texture(i32 w, i32 h, const u8 *rgba) override
    {
        return make_texture(w, h, false, rgba);
    }

    texture_handle create_target(i32 w, i32 h) override
    {
        return make_texture(w, h, true, nullptr);
    }

    void destroy_target(texture_handle target) override { destroy_texture(target); }

    void blit(texture_handle src, const rect *src_rect, texture_handle dst,
              const rect *dst_rect) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren || !src) return;
        SDL_Texture *s = resolve(reinterpret_cast<sdl3_texture *>(src));
        SDL_Texture *d = dst ? resolve(reinterpret_cast<sdl3_texture *>(dst)) : nullptr;
        if (!s) return;
        SDL_SetRenderTarget(ren, d);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_FRect sfr{}, dfr{};
        SDL_FRect *sp = nullptr;
        SDL_FRect *dp = nullptr;
        if (src_rect)
        {
            sfr = SDL_FRect{src_rect->x, src_rect->y, src_rect->w, src_rect->h};
            sp = &sfr;
        }
        if (dst_rect)
        {
            dfr = SDL_FRect{dst_rect->x, dst_rect->y, dst_rect->w, dst_rect->h};
            dp = &dfr;
        }
        SDL_RenderTexture(ren, s, sp, dp);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        current_target_ = dst;
    }

    void update_texture(texture_handle handle, i32 x, i32 y, i32 w, i32 h, const u8 *rgba) override
    {
        sdl3_texture *t = reinterpret_cast<sdl3_texture *>(handle);
        if (!t || t->target || !rgba || w <= 0 || h <= 0) return;
        if (x < 0 || y < 0 || x + w > t->w || y + h > t->h) return;
        for (i32 r = 0; r < h; ++r)
        {
            const usize dst =
                (static_cast<usize>(y + r) * static_cast<usize>(t->w) + static_cast<usize>(x)) * 4;
            std::memcpy(t->pixels.data() + dst,
                        rgba + static_cast<usize>(r) * static_cast<usize>(w) * 4,
                        static_cast<usize>(w) * 4);
        }
        SDL_Rect sub{x, y, w, h};
        for (SDL_Texture *c : t->copies)
            if (c) SDL_UpdateTexture(c, &sub, rgba, w * 4);
    }

    void destroy_texture(texture_handle handle) override
    {
        sdl3_texture *t = reinterpret_cast<sdl3_texture *>(handle);
        if (!t) return;
        for (SDL_Texture *c : t->copies)
            if (c) SDL_DestroyTexture(c);
        for (usize i = 0; i < textures.size(); ++i)
        {
            if (textures[i] == t)
            {
                textures.erase(textures.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        delete t;
    }

    void draw(texture_handle handle, const vertex *vertices, i32 vertex_count, const i32 *indices,
              i32 index_count) override
    {
        SDL_Renderer *ren = renderer();
        if (!ren || vertex_count <= 0 || index_count <= 0) return;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        colors_scratch.resize(static_cast<usize>(vertex_count));
        for (i32 i = 0; i < vertex_count; ++i)
        {
            colors_scratch[static_cast<usize>(i)].col =
                SDL_FColor{vertices[i].c.r / 255.0f, vertices[i].c.g / 255.0f,
                           vertices[i].c.b / 255.0f, vertices[i].c.a / 255.0f};
        }
        // Zero-copy positions and UVs: RenderGeometryRaw strides straight over
        // the core's interleaved vertex buffer. All three strides are
        // sizeof(vertex): SDL's software geometry path reads UVs with the
        // color stride (SDL_SW_RenderGeometryRaw's quad detection), so the
        // strides must be equal — the color slots are padded accordingly.
        constexpr int stride = static_cast<int>(sizeof(vertex));
        SDL_RenderGeometryRaw(
            ren, handle ? resolve(reinterpret_cast<sdl3_texture *>(handle)) : nullptr,
            &vertices[0].x, stride, reinterpret_cast<const SDL_FColor *>(colors_scratch.data()),
            stride, &vertices[0].u, stride, vertex_count, indices, index_count,
            static_cast<int>(sizeof(i32)));
    }
};

void sdl3_surface::make_current(render_device &)
{
    sdl3_device &d = *dev;
    if (slot < 0 || slot >= static_cast<i32>(d.slots.size())) return;
    d.current_slot = slot;
    sdl3_device::slot &sl = d.slots[static_cast<usize>(slot)];
    if (!sl.renderer) return;
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(sl.renderer, &w, &h);
    if (w > 0 && h > 0) d.ensure_scene(slot, w, h);
    SDL_SetRenderDrawBlendMode(sl.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(sl.renderer,
                        sl.scene ? d.resolve(reinterpret_cast<sdl3_texture *>(sl.scene)) : nullptr);
    d.current_target_ = sl.scene;
}

texture_handle sdl3_surface::scene_target()
{
    if (slot < 0 || slot >= static_cast<i32>(dev->slots.size())) return nullptr;
    return dev->slots[static_cast<usize>(slot)].scene;
}

void sdl3_surface::output_size(i32 &w, i32 &h)
{
    w = 0;
    h = 0;
    if (slot < 0 || slot >= static_cast<i32>(dev->slots.size())) return;
    SDL_Renderer *ren = dev->slots[static_cast<usize>(slot)].renderer;
    if (!ren) return;
    int iw = 0, ih = 0;
    SDL_GetCurrentRenderOutputSize(ren, &iw, &ih);
    w = iw;
    h = ih;
}

void sdl3_surface::present()
{
    sdl3_device &d = *dev;
    if (slot < 0 || slot >= static_cast<i32>(d.slots.size())) return;
    sdl3_device::slot &sl = d.slots[static_cast<usize>(slot)];
    if (!sl.renderer) return;
    d.current_slot = slot;
    SDL_SetRenderTarget(sl.renderer, nullptr);
    SDL_SetRenderDrawBlendMode(sl.renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderClipRect(sl.renderer, nullptr);
    if (sl.scene)
    {
        SDL_Texture *scene_tex = d.resolve(reinterpret_cast<sdl3_texture *>(sl.scene));
        if (scene_tex) SDL_RenderTexture(sl.renderer, scene_tex, nullptr, nullptr);
    }
    SDL_SetRenderDrawBlendMode(sl.renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderPresent(sl.renderer);
}

} // namespace
#endif

render_device *create_sdl3_device(void *sdl_renderer)
{
#if defined(PUFFERUI_ENABLE_SDL3)
    auto *d = new sdl3_device();
    d->primary_renderer = static_cast<SDL_Renderer *>(sdl_renderer);
    return d;
#else
    (void)sdl_renderer;
    return nullptr;
#endif
}

void destroy_sdl3_device(render_device *device)
{
    delete device;
}

#if defined(PUFFERUI_ENABLE_SDL3)
namespace
{

// The hit-test callback runs on every pointer move over the window, so it
// stays stateless and cheap: geometry only, no layout.
SDL_HitTestResult SDLCALL sdl3_chrome_hit_test(SDL_Window *win, const SDL_Point *area, void *data)
{
    context *c = static_cast<context *>(data);
    window *w = c ? window_at(c, win) : nullptr;
    if (!w) return SDL_HITTEST_NORMAL;
    const f32 lx = static_cast<f32>(area->x), ly = static_cast<f32>(area->y);
    if (!w->maximized)
    {
        // Generous native resize borders: the corner squares claim both axes
        // at once and take precedence over the straight edges. A maximized
        // window keeps no resize bands (its state is fixed), but its caption
        // below stays draggable - the system restores it during the native
        // drag.
        constexpr f32 edge = 8.0f, corner = 20.0f;
        const bool left = lx <= edge, right = lx >= w->area.w - edge;
        const bool top = ly <= edge, bottom = ly >= w->area.h - edge;
        if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    }
    // The bar minus the glyph buttons is the system caption. For a maximized
    // window SDL keeps the WS_MAXIMIZEBOX style, so the system's move loop
    // restores it when the caption is dragged.
    const f32 buttons_w = detail::CLOSE_BTN_W + detail::TITLE_BTN_W * 2.0f;
    if (ly <= detail::TITLEBAR_H && lx < w->area.w - buttons_w) return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

// The SDL3 window host: the platform actions the custom titlebar issues.
struct sdl3_window_host : window_host
{
    void move_window(window &w, f32 desktop_x, f32 desktop_y) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (win)
            SDL_SetWindowPosition(win, static_cast<int>(desktop_x), static_cast<int>(desktop_y));
    }
    void resize_window(window &w, f32 width, f32 height) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (win) SDL_SetWindowSize(win, static_cast<int>(width), static_cast<int>(height));
    }
    void minimize_window(window &w) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (win) SDL_MinimizeWindow(win);
    }
    void toggle_maximize(window &w) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (!win) return;
        (void)w;
        // Read the real window state, not `w.maximized`: the library flag is
        // set optimistically by the pump, and a stale flag must never wedge
        // the OS state (restore must stay reachable by clicking the glyph).
        if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED)
            SDL_RestoreWindow(win);
        else
            SDL_MaximizeWindow(win);
    }
    // Native chrome for a borderless window: the titlebar's drag strip maps to
    // the system caption (native dragging = the system's own Aero Snap, its
    // preview, and maximize-on-double-click), and generous edge/corner bands
    // map to the native resize borders. The glyph buttons stay normal client
    // area so the library's clicks reach them.
    void install_system_chrome(window &w) override
    {
        SDL_Window *win = static_cast<SDL_Window *>(w.handle);
        if (!win) return;
        SDL_SetWindowResizable(win, true);
        SDL_SetWindowHitTest(win, sdl3_chrome_hit_test, w.owner);
    }
};

window_host *sdl3_host_instance()
{
    static sdl3_window_host host;
    return &host;
}
} // namespace

bool sdl3_route_impl(context *c, SDL_Event *e)
{
    if (!c || !e) return false;
    if (!window_host_of(c)) set_window_host(c, sdl3_host_instance());

    switch (e->type)
    {
    case SDL_EVENT_QUIT:
        return true; // the app should exit
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) w->close_requested = true;
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) focus_window(c, *w);
        break;
    }
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MOVED:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        window *w = sdl ? window_at(c, sdl) : nullptr;
        if (w)
        {
            int x = 0, y = 0, ww = 0, wh = 0;
            SDL_GetWindowPosition(sdl, &x, &y);
            SDL_GetWindowSize(sdl, &ww, &wh);
            set_window_client(*w, rect::make(static_cast<f32>(x), static_cast<f32>(y),
                                             static_cast<f32>(ww), static_cast<f32>(wh)));
            w->maximized = (SDL_GetWindowFlags(sdl) & SDL_WINDOW_MAXIMIZED) != 0;
        }
        break;
    }
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->window.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr)
            w->maximized = (SDL_GetWindowFlags(sdl) & SDL_WINDOW_MAXIMIZED) != 0;
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->motion.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) mouse_move(*w, e->motion.x, e->motion.y);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->button.windowID);
        window *w = sdl ? window_at(c, sdl) : nullptr;
        if (!w) break;
        mouse_move(*w, e->button.x, e->button.y); // buttons carry coordinates
        const bool down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (e->button.button == SDL_BUTTON_LEFT)
            mouse_button(*w, down);
        else if (e->button.button == SDL_BUTTON_RIGHT)
            mouse_button(*w, pointer_button::RIGHT, down);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->wheel.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) mouse_wheel(*w, e->wheel.x, e->wheel.y);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        const bool down = e->type == SDL_EVENT_KEY_DOWN;
        window *w = window_at(c, SDL_GetWindowFromID(e->key.windowID));
        if (!w) w = focused_window(c);
        if (!w) break;
        mods_event(*w, (e->key.mod & SDL_KMOD_SHIFT) != 0, (e->key.mod & SDL_KMOD_CTRL) != 0);
        key k = key::ESCAPE;
        bool mapped = true;
        switch (e->key.key)
        {
        case SDLK_ESCAPE:
            k = key::ESCAPE;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            k = key::ENTER;
            break;
        case SDLK_TAB:
            k = key::TAB;
            break;
        case SDLK_BACKSPACE:
            k = key::BACKSPACE;
            break;
        case SDLK_SPACE:
            k = key::SPACE;
            break;
        case SDLK_LEFT:
            k = key::LEFT;
            break;
        case SDLK_RIGHT:
            k = key::RIGHT;
            break;
        case SDLK_UP:
            k = key::UP;
            break;
        case SDLK_DOWN:
            k = key::DOWN;
            break;
        case SDLK_HOME:
            k = key::HOME;
            break;
        case SDLK_END:
            k = key::END;
            break;
        case SDLK_A:
            k = key::A;
            break;
        case SDLK_C:
            k = key::C;
            break;
        case SDLK_X:
            k = key::X;
            break;
        case SDLK_V:
            k = key::V;
            break;
        case SDLK_Z:
            k = key::Z;
            break;
        case SDLK_Y:
            k = key::Y;
            break;
        case SDLK_DELETE:
            k = key::DEL;
            break;
        default:
            mapped = false;
            break;
        }
        if (mapped && !(down && e->key.repeat)) key_event(*w, k, down);
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->text.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr) text_input_event(*w, e->text.text);
        break;
    }
    case SDL_EVENT_TEXT_EDITING:
    {
        SDL_Window *sdl = SDL_GetWindowFromID(e->edit.windowID);
        if (window *w = sdl ? window_at(c, sdl) : nullptr)
            ime_event(*w, e->edit.text, e->edit.start);
        break;
    }
    default:
        break;
    }
    return false;
}

bool sdl3_route(context *c, void *sdl_event)
{
    return sdl3_route_impl(c, static_cast<SDL_Event *>(sdl_event));
}

bool sdl3_pump(context *c)
{
    if (!c) return false;
    bool quit = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) quit = sdl3_route(c, &e) || quit;
    return quit;
}

// ---- one-call bootstrap -----------------------------------------------------
bool sdl3_app_init(sdl3_app &a, const char *title, i32 w, i32 h, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (a.inited) return true;
    if (!title || w <= 0 || h <= 0) return false;

    if (!SDL_Init(SDL_INIT_VIDEO)) return false;

    const SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE |
                                  (a.hidden ? SDL_WINDOW_HIDDEN : static_cast<SDL_WindowFlags>(0));
    a.sdl_window = SDL_CreateWindow(title, w, h, flags);
    if (!a.sdl_window)
    {
        SDL_Quit();
        return false;
    }
    a.sdl_renderer = SDL_CreateRenderer(static_cast<SDL_Window *>(a.sdl_window), a.renderer_name);
    if (!a.sdl_renderer)
    {
        SDL_DestroyWindow(static_cast<SDL_Window *>(a.sdl_window));
        a.sdl_window = nullptr;
        SDL_Quit();
        return false;
    }
    a.device = create_sdl3_device(a.sdl_renderer);
    if (!a.device)
    {
        SDL_DestroyRenderer(static_cast<SDL_Renderer *>(a.sdl_renderer));
        a.sdl_renderer = nullptr;
        SDL_DestroyWindow(static_cast<SDL_Window *>(a.sdl_window));
        a.sdl_window = nullptr;
        SDL_Quit();
        return false;
    }
    a.surface = a.device->create_surface(static_cast<SDL_Window *>(a.sdl_window));
    a.ctx = a.surface ? create_context(a.device, a.surface) : nullptr;
    a.win = a.ctx ? add_window(a.ctx, a.sdl_window, a.surface,
                               rect::make(0, 0, static_cast<f32>(w), static_cast<f32>(h)))
                  : nullptr;
    if (!a.ctx || !a.win)
    {
        if (a.ctx) destroy_context(a.ctx);
        a.ctx = nullptr;
        destroy_sdl3_device(a.device);
        a.device = nullptr;
        a.surface = nullptr;
        SDL_DestroyRenderer(static_cast<SDL_Renderer *>(a.sdl_renderer));
        a.sdl_renderer = nullptr;
        SDL_DestroyWindow(static_cast<SDL_Window *>(a.sdl_window));
        a.sdl_window = nullptr;
        SDL_Quit();
        return false;
    }
    install_window_chrome(a.ctx, *a.win); // native drag/resize/snap

    // The base font: the bundled DejaVu set when it can be found (system fonts
    // are the fallback), then a working base theme. Apps override with
    // set_theme afterwards; the handle is handed back for reuse.
    if (a.asset_dir)
    {
        char font_path[512];
        std::snprintf(font_path, sizeof(font_path), "%s/fonts/DejaVuSans.ttf", a.asset_dir);
        a.font = load_font(a.ctx, font_path);
    }
#if defined(PUFFERUI_ASSET_DIR)
    if (a.font == FONT_INVALID)
        a.font = load_font(a.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
#endif
    if (a.font == FONT_INVALID) a.font = load_font(a.ctx, "assets/fonts/DejaVuSans.ttf");
    if (a.font == FONT_INVALID) a.font = load_font(a.ctx, "C:/Windows/Fonts/segoeui.ttf");
    if (a.font == FONT_INVALID) a.font = load_font(a.ctx, "C:/Windows/Fonts/arial.ttf");
    theme base = default_dark();
    base.font = a.font;
    base.text_size = 15.0f;
    set_theme(a.ctx, base);

    a.width = w;
    a.height = h;
    a.freq = static_cast<f64>(SDL_GetPerformanceFrequency());
    a.last_counter = SDL_GetPerformanceCounter();
    a.inited = true;
    return true;
}

bool sdl3_app_pump(sdl3_app &a)
{
    if (!a.inited) return false;
    if (a.wait_when_idle && a.ctx && !needs_redraw(a.ctx))
    {
        // Idle: sleep until something happens (bounded, so housekeeping —
        // keyed-state sweeps, the caret — stays alive), then drain the burst.
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 120))
        {
            if (sdl3_route(a.ctx, &e)) return false;
        }
        while (SDL_PollEvent(&e))
        {
            if (sdl3_route(a.ctx, &e)) return false;
        }
    }
    if (sdl3_pump(a.ctx)) return false; // the app should stop
    if (a.win)
    {
        // keep the client rect in sync with SDL (drag-resize, maximize)
        int x = 0, y = 0, ww = 0, wh = 0;
        SDL_GetWindowPosition(static_cast<SDL_Window *>(a.sdl_window), &x, &y);
        SDL_GetWindowSize(static_cast<SDL_Window *>(a.sdl_window), &ww, &wh);
        if (static_cast<f32>(ww) != a.win->client.w || static_cast<f32>(wh) != a.win->client.h ||
            static_cast<f32>(x) != a.win->client.x || static_cast<f32>(y) != a.win->client.y)
            set_window_client(*a.win, rect::make(static_cast<f32>(x), static_cast<f32>(y),
                                                 static_cast<f32>(ww), static_cast<f32>(wh)));
        if (a.win->close_requested) return false;
    }
    return true;
}

void sdl3_app_tick(sdl3_app &a)
{
    if (!a.inited) return;
    const u64 counter = SDL_GetPerformanceCounter();
    a.freq = static_cast<f64>(SDL_GetPerformanceFrequency());
    if (a.last_counter != 0)
        a.dt = static_cast<f64>(counter - a.last_counter) / a.freq;
    else
        a.dt = 1.0 / 60.0;
    if (a.dt <= 0.0) a.dt = 1.0 / 60.0;
    a.last_counter = counter;
    a.now += a.dt;
}

void sdl3_app_shutdown(sdl3_app &a)
{
    if (!a.inited) return;
    if (a.ctx) destroy_context(a.ctx);
    if (a.device) destroy_sdl3_device(a.device);
    if (a.sdl_renderer) SDL_DestroyRenderer(static_cast<SDL_Renderer *>(a.sdl_renderer));
    if (a.sdl_window) SDL_DestroyWindow(static_cast<SDL_Window *>(a.sdl_window));
    SDL_Quit();
    a.ctx = nullptr;
    a.device = nullptr;
    a.surface = nullptr;
    a.sdl_renderer = nullptr;
    a.sdl_window = nullptr;
    a.win = nullptr;
    a.inited = false;
}
#endif

void install_window_chrome(context *c, window &w)
{
    if (!c) return;
#if defined(PUFFERUI_ENABLE_SDL3)
    // The SDL3 host installs itself lazily on the first routed event, which
    // is AFTER the app created its windows - ensure it exists here so the
    // chrome install cannot be silently skipped.
    if (!c->host) set_window_host(c, sdl3_host_instance());
#endif
    if (!c->host) return;
    c->host->install_system_chrome(w);
}

} // namespace pui

#endif // PUFFERUI_IMPLEMENTATION