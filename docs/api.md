# PufferUI API reference (generated)

Generated from the declaration section of `include/pufferui/pufferui.h`
by `tools/gen_api.ps1` (CI checks it does not drift). Every declaration here
is public; the implementation lives behind `PUFFERUI_IMPLEMENTATION`.

## `ui` methods

- `region region(rect area, uiid key, bool push_clip = true);`
- `vec2 button_size(std::string_view label);`
- `vec2 text_size(std::string_view s);`
- `interaction interact(uiid id, rect area, bool enabled = true, bool focusable = true);`
- `f32 text_width(std::string_view s);`
- `void text(rect r, std::string_view s, color c, align a = ALIGN_LEFT);`
- `void textf(rect r, color c, align a, const char *fmt, ...) PUFFERUI_PRINTF(5, 6);`
- `measure_size measure_text(std::string_view s, f32 available_width);`
- `measure_size measure(function_ref<measure_size(f32)> fn, f32 available_width);`
- `void text_wrapped(rect r, std::string_view s, color c);`
- `f32 text_wrapped_height(f32 width, std::string_view s);`
- `void text_ellipsis(rect r, std::string_view s, color c, align a = ALIGN_LEFT);`
- `text_scope text_style(f32 size, font_handle font = FONT_INVALID);`
- `bool has_glyph(u32 cp);`
- `f32 text_fit(rect r, std::string_view s, color c, align a = ALIGN_LEFT);`
- `bool is_visible(rect r) const;`
- `button_style resolve_button_style(uiid role_id = 0, const button_override &ov = {}) const;`
- `bool button(rect r, std::string_view label, uiid id, uiid role_id = 0, button_override ov = {});`
- `bool button(rect r, std::string_view label, uiid id, const button_opts &opts);`
- `void card(rect r, card_override ov = {});`
- `bool checkbox(rect r, std::string_view label, bool &value, uiid id);`
- `bool slider_float(rect r, std::string_view label, f32 &value, f32 min_value, f32 max_value, uiid id, const char *fmt = "%.1f");`
- `void progress_bar(rect r, f32 fraction, color fill, color bg);`
- `scroll_view scroll(rect viewport, uiid id, scroll_options ov = {});`
- `panel_scope panel(std::string_view title, rect &bounds, u32 flags = PANEL_NONE, uiid dock_panel = 0, const char *dock_name = nullptr, panel_override ov = {});`
- `panel_scope panel(std::string_view title, rect &bounds, const panel_opts &opts);`
- `std::pair<rect, rect> split_horizontal_interactive(uiid id, rect bounds, f32 *position, f32 min_left = 60.0f, f32 max_left = 1.0e9f, f32 thickness = 6.0f, f32 gap = 2.0f);`
- `std::pair<rect, rect> split_vertical_interactive(uiid id, rect bounds, f32 *position, f32 min_top = 40.0f, f32 max_top = 1.0e9f, f32 thickness = 6.0f, f32 gap = 2.0f);`
- `dock_action dock_space(uiid id, rect area, dock_node &root, function_ref<void(uiid, rect, bool)> draw_panel);`
- `bool text_field(rect r, std::string &value, uiid id);`
- `bool text_field(rect r, std::string &value, uiid id, const field_opts &opts);`
- `bool number_field(rect r, f32 &value, uiid id, const char *fmt = "%.2f");`
- `popup_scope popup(uiid id, rect area, popup_flags flags = popup_flags::NONE);`
- `titlebar_result titlebar(window &w, rect &bounds, std::string_view title);`
- `bool combo(rect r, std::string_view label, const char *const *items, i32 item_count, i32 &selected, uiid id, f32 max_popup_height = 220.0f);`
- `rect tooltip(rect anchor, uiid id, std::string_view tip);`
- `i32 context_menu(uiid id, rect anchor, const char *const *item_labels, i32 item_count);`
- `void draw_triangles(texture_handle tex, const vertex *vertices, i32 vertex_count, const i32 *indices, i32 index_count);`
- `void draw_rect(rect r, color c);`
- `void draw_line(f32 x0, f32 y0, f32 x1, f32 y1, color c, f32 thickness = 1.0f);`
- `void draw_rounded_rect(rect r, color c, f32 radius = 0.0f);`
- `void draw_rounded_rect(rect r, color c, const corner_radii &radii);`
- `void draw_image(const skin_image &img, rect dst);`
- `void draw_nine_slice(const skin_image &img, rect dst);`
- `f32 animate(uiid key, f32 target, const tween &spec);`
- `f32 animate(uiid key, f32 target, const spring &spec);`
- `f32 animate_global(uiid key, f32 target, const tween &spec);`
- `f32 animate_global(uiid key, f32 target, const spring &spec);`
- `f32 smooth(uiid key, f32 target, f32 half_life = 0.1f);`
- `f32 appear(uiid key, f32 duration = 0.2f, easing curve = easing::EASE_OUT);`
- `color animate_color(uiid key, color target, const tween &spec);`
- `rect animate_rect(uiid key, rect target, const rect_motion &m = {});`
- `rect_motion_info motion_info(uiid key) const;`
- `bool animations_active() const;`
- `void blur(rect r, f32 blur_radius = 12.0f, f32 corner_radius = 0.0f, f32 alpha = 1.0f);`
- `void blur(rect r, f32 blur_radius, const corner_radii &radii, f32 alpha = 1.0f);`
- `void draw_polygon(std::span<const vec2> points, color c);`
- `void draw_sector(vec2 center, f32 r_in, f32 r_out, f32 a0, f32 a1, color c);`
- `void draw_arc(vec2 center, f32 radius, f32 thickness, f32 a0, f32 a1, color c);`

## Free functions and parameters

- `context *current_context();`
- `void set_current_context(context *);`
- `void report_violation(violation_code code, const char *condition, const char *message, const char *file, int line, bool fatal);`
- `void set_violation_handler(context *c, violation_handler handler, void *user);`
- `constexpr u64 hash(std::string_view s);`
- `render_device *create_sdl3_device(void *sdl_renderer);`
- `void destroy_sdl3_device(render_device *device);`
- `bool sdl3_route(context *c, void *sdl_event);`
- `bool sdl3_pump(context *c);`
- `clipboard *sdl3_system_clipboard();`
- `bool sdl3_app_init(sdl3_app &a, const char *title, i32 w, i32 h, int argc, char **argv);`
- `bool sdl3_app_pump(sdl3_app &a);`
- `void sdl3_app_tick(sdl3_app &a);`
- `void sdl3_app_shutdown(sdl3_app &a);`
- `void set_window_host(context *c, window_host *host);`
- `window_host *window_host_of(context *c);`
- `i32 resolve_track_sizes(f32 available, std::span<const track_size> tracks, f32 *out, i32 max_out);`
- `grid_cursor auto_fit_grid(rect container, i32 total_items, f32 min_item_w, f32 item_h, f32 gap = 8.0f);`
- `void set_report_layout_overflow(context *c, bool enabled);`
- `i32 layout_overflow_count(context *c); // clamps counted since enable (or since reset) // ---- development overlay ---- // The last violation message recorded on this context, exactly as the guard // wrote it ("" when none). The messages are stable string literals, so tests // may match on them; they are not localized and never change shape between // builds. const char *violation_last(context *c);`
- `violation_code violation_last_code(context *c);`
- `const char *violation_code_name(violation_code code);`
- `void set_break_on_violation(context *c, bool enabled);`
- `std::string dock_save_tree(const dock_node &root);`
- `bool dock_restore_tree(std::string_view text, dock_node *nodes, i32 node_cap, std::string &name_storage, dock_node *&out_root);`
- `void draw_violation_overlay(ui &u, rect r);`
- `context *create_context(render_device *device = nullptr, render_surface *surface = nullptr);`
- `void destroy_context(context *c);`
- `void set_device(context *c, render_device *device, render_surface *surface);`
- `void set_theme(context *c, const theme &t);`
- `font_handle load_font(context *c, const char *path);`
- `window *add_window(context *c, void *handle, render_surface *surface, rect client);`
- `void remove_window(context *c, window &w);`
- `window *window_at(context *c, void *handle);`
- `window *first_window(context *c);`
- `void set_window_client(window &w, rect client);`
- `void focus_window(context *c, window &w);`
- `window *focused_window(context *c);`
- `rect desktop_rect(context *c);`
- `void set_global_mouse(context *c, f32 x, f32 y);`
- `void begin_frame(context *c, window &w, f64 now, f64 dt);`
- `void begin_frame(context *c, f64 now, f64 dt, rect screen); // primary window void end_frame(context *c);`
- `bool needs_redraw(const context *c);`
- `void mouse_move(window &w, f32 x, f32 y);`
- `void mouse_button(window &w, bool down); // left button void mouse_button(window &w, pointer_button b, bool down); // any button void key_event(window &w, key k, bool down);`
- `void key_repeat(window &w, key k);`
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
- `i32 violation_count(context *c);`

## Components (`pui::comp`)

- `vec2 switch_size(ui &u, std::string_view label);`
- `switch_result switch_toggle(ui &u, rect area, std::string_view label, bool &value, const switch_props &p = {});`
- `radio_result radio_group(ui &u, rect area, std::span<const char *const> labels, i32 &selected, const radio_props &p = {});`
- `segmented_result segmented(ui &u, rect area, std::span<const char *const> labels, i32 &selected, const segmented_props &p = {});`
- `tabs_result tab_bar(ui &u, rect area, std::span<const char *const> labels, i32 &active, const tabs_props &p = {});`
- `void toast_draw(ui &u, rect anchor, toast_host &host, const toast_props &p = {});`
- `table_result table(ui &u, rect area, std::span<const char *const> headers, i32 row_count, function_ref<void(ui &, rect, i32 row, i32 col)> cell_draw, const table_props &p = {});`
- `palette_result command_palette(ui &u, rect screen, bool &open, palette_state &st, std::span<const palette_command> commands, const palette_props &p = {});`
- `rect section(ui &u, rect area, std::string_view title, std::string_view caption = {}, const section_props &p = {});`

