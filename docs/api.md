# PufferUI API reference (generated)

Generated from the declaration section of `include/pufferui/pufferui.h`
by `tools/gen_api.ps1` (CI checks it does not drift). Every declaration here
is public; the implementation lives behind `PUFFERUI_IMPLEMENTATION`.

## `ui` methods

- `vec2 button_size(std::string_view label);`
- `vec2 text_size(std::string_view s);`
- `interaction interact(uiid id, rect area, bool enabled = true, bool focusable = true);`
- `f32 text_width(std::string_view s);`
- `void text(rect r, std::string_view s, color c, align a = ALIGN_LEFT);`
- `void text_wrapped(rect r, std::string_view s, color c);`
- `f32 text_wrapped_height(f32 width, std::string_view s);`
- `void text_ellipsis(rect r, std::string_view s, color c, align a = ALIGN_LEFT);`
- `text_scope text_style(f32 size, font_handle font = FONT_INVALID);`
- `bool has_glyph(u32 cp);`
- `f32 text_fit(rect r, std::string_view s, color c, align a = ALIGN_LEFT);`
- `bool is_visible(rect r) const;`
- `bool button(rect r, std::string_view label, uiid id, uiid role_id = 0, button_override ov = {});`
- `bool button(rect r, std::string_view label, uiid id, const button_opts &opts);`
- `void card(rect r, card_override ov = {});`
- `bool checkbox(rect r, std::string_view label, bool &value, uiid id);`
- `void progress_bar(rect r, f32 fraction, color fill, color bg);`
- `scroll_view scroll(rect viewport, uiid id, scroll_options ov = {});`
- `panel_scope panel(std::string_view title, rect &bounds, const panel_opts &opts);`
- `bool text_field(rect r, std::string &value, uiid id);`
- `bool number_field(rect r, f32 &value, uiid id, const char *fmt = "%.2f");`
- `popup_scope popup(uiid id, rect area, popup_flags flags = popup_flags::NONE);`
- `rect tooltip(rect anchor, uiid id, std::string_view tip);`
- `i32 context_menu(uiid id, rect anchor, const char *const *item_labels, i32 item_count);`
- `void draw_rect(rect r, color c);`
- `void draw_line(f32 x0, f32 y0, f32 x1, f32 y1, color c, f32 thickness = 1.0f);`
- `void draw_rounded_rect(rect r, color c, f32 radius = 0.0f);`
- `void draw_image(const skin_image &img, rect dst);`
- `void draw_nine_slice(const skin_image &img, rect dst);`
- `f32 animate(uiid key, f32 target, const tween &spec);`
- `f32 animate(uiid key, f32 target, const spring &spec);`
- `f32 animate_global(uiid key, f32 target, const tween &spec);`
- `f32 animate_global(uiid key, f32 target, const spring &spec);`
- `f32 smooth(uiid key, f32 target, f32 half_life = 0.1f);`
- `f32 appear(uiid key, f32 duration = 0.2f, easing curve = easing::EASE_OUT);`
- `color animate_color(uiid key, color target, const tween &spec);`
- `bool animations_active() const;`
- `void blur(rect r, f32 blur_radius = 12.0f, f32 corner_radius = 0.0f, f32 alpha = 1.0f);`
- `void draw_polygon(std::span<const vec2> points, color c);`
- `void draw_sector(vec2 center, f32 r_in, f32 r_out, f32 a0, f32 a1, color c);`
- `void draw_arc(vec2 center, f32 radius, f32 thickness, f32 a0, f32 a1, color c);`

## Free functions and parameters

- `context *current_context();`
- `void set_current_context(context *);`
- `void set_violation_handler(context *c, violation_handler handler, void *user);`
- `void destroy_sdl3_device(render_device *device);`
- `bool sdl3_route(context *c, void *sdl_event);`
- `bool sdl3_pump(context *c);`
- `bool sdl3_app_init(sdl3_app &a, const char *title, i32 w, i32 h, int argc, char **argv);`
- `bool sdl3_app_pump(sdl3_app &a);`
- `void sdl3_app_tick(sdl3_app &a);`
- `void sdl3_app_shutdown(sdl3_app &a);`
- `void set_window_host(context *c, window_host *host);`
- `void toast_draw(ui &u, rect anchor, toast_host &host, const toast_props &p = {});`
- `void set_report_layout_overflow(context *c, bool enabled);`
- `const char *violation_last(context *c);`
- `const char *violation_code_name(violation_code code);`
- `void set_break_on_violation(context *c, bool enabled);`
- `void draw_violation_overlay(ui &u, rect r);`
- `context *create_context(render_device *device = nullptr, render_surface *surface = nullptr);`
- `void destroy_context(context *c);`
- `void set_device(context *c, render_device *device, render_surface *surface);`
- `void set_theme(context *c, const theme &t);`
- `void remove_window(context *c, window &w);`
- `void set_window_client(window &w, rect client);`
- `void focus_window(context *c, window &w);`
- `rect desktop_rect(context *c);`
- `void set_global_mouse(context *c, f32 x, f32 y);`
- `void begin_frame(context *c, window &w, f64 now, f64 dt);`
- `void end_frame(context *c);`
- `bool needs_redraw(const context *c);`
- `void mouse_move(window &w, f32 x, f32 y);`
- `void key_event(window &w, key k, bool down);`
- `void text_input_event(window &w, const char *utf8);`
- `void ime_event(window &w, const char *preedit, i32 cursor);`
- `void mods_event(window &w, bool shift, bool ctrl);`
- `void mouse_wheel(window &w, f32 dx, f32 dy);`
- `void mouse_move(context *c, f32 x, f32 y);`
- `void mouse_button(context *c, bool down);`
- `void mouse_button(context *c, pointer_button b, bool down);`
- `void key_event(context *c, key k, bool down);`
- `void text_input_event(context *c, const char *utf8);`
- `void ime_event(context *c, const char *preedit, i32 cursor);`
- `void mods_event(context *c, bool shift, bool ctrl);`
- `void mouse_wheel(context *c, f32 dx, f32 dy);`
- `void set_clipboard(context *c, clipboard *clip);`
- `void install_window_chrome(context *c, window &w);`
