// test_core.cpp - the runner (r88 split): registers every test from the
// per-area files, supports `--list` and `--filter NAME` (substring), and
// keeps the suite contract: unfiltered + zero failures prints
// "all core tests passed" and exits 0.
#include "test_util.h"
#include "test_list.h"
#include <cstring>

static const struct
{
    const char *name;
    void (*fn)();
} TESTS[] = {
    {"test_rect_algebra", test_rect_algebra},
    {"test_ids", test_ids},
    {"test_click", test_click},
    {"test_release_outside_cancels", test_release_outside_cancels},
    {"test_cursors", test_cursors},
    {"test_draw_batching", test_draw_batching},
    {"test_draw_list_snapshot", test_draw_list_snapshot},
    {"test_culling_and_visibility", test_culling_and_visibility},
    {"test_identity_scope", test_identity_scope},
    {"test_dup_widget_id_and_sizes", test_dup_widget_id_and_sizes},
    {"test_needs_redraw", test_needs_redraw},
    {"test_switch_toggle", test_switch_toggle},
    {"test_radio_group", test_radio_group},
    {"test_segmented", test_segmented},
    {"test_tab_bar", test_tab_bar},
    {"test_accordion", test_accordion},
    {"test_drawer", test_drawer},
    {"test_toast_host", test_toast_host},
    {"test_table", test_table},
    {"test_command_palette", test_command_palette},
    {"test_theme_tokens", test_theme_tokens},
    {"test_theme", test_theme},
    {"test_utf8", test_utf8},
    {"test_text", test_text},
    {"test_style_cascade", test_style_cascade},
    {"test_rounded_rect", test_rounded_rect},
    {"test_popups", test_popups},
    {"test_keys_and_escape", test_keys_and_escape},
    {"test_rect_edges", test_rect_edges},
    {"test_stack_clamp", test_stack_clamp},
    {"test_interaction_edges", test_interaction_edges},
    {"test_text_edges", test_text_edges},
    {"test_utf8_truncated", test_utf8_truncated},
    {"test_ime", test_ime},
    {"test_draw_flush", test_draw_flush},
    {"test_blur_fallback", test_blur_fallback},
    {"test_window_errors", test_window_errors},
    {"test_duplicate_region_id", test_duplicate_region_id},
    {"test_auto_id_local", test_auto_id_local},
    {"test_limit_overflows", test_limit_overflows},
    {"test_split_interactive", test_split_interactive},
    {"test_panel_flags", test_panel_flags},
    {"test_dock_empty", test_dock_empty},
    {"test_nine_slice", test_nine_slice},
    {"test_state_view_update", test_state_view_update},
    {"test_widget_outline", test_widget_outline},
    {"test_animation_tween", test_animation_tween},
    {"test_animation_spring_smooth", test_animation_spring_smooth},
    {"test_animation_scope_color_appear", test_animation_scope_color_appear},
    {"test_font_coverage", test_font_coverage},
    {"test_popup_focus_trap", test_popup_focus_trap},
    {"test_rect_helpers", test_rect_helpers},
    {"test_text_polish", test_text_polish},
    {"test_widget_extras", test_widget_extras},
    {"test_tracks_and_grid", test_tracks_and_grid},
    {"test_scroll_view", test_scroll_view},
    {"test_text_field_caret_placement", test_text_field_caret_placement},
    {"test_text_field_word_ops", test_text_field_word_ops},
    {"test_text_field_undo_redo", test_text_field_undo_redo},
    {"test_layout_overflow_report", test_layout_overflow_report},
    {"test_region_corner", test_region_corner},
    {"test_combo", test_combo},
    {"test_combo_mouse_pick", test_combo_mouse_pick},
    {"test_tooltip_delay", test_tooltip_delay},
    {"test_context_menu", test_context_menu},
    {"test_scroll_options_and_limits", test_scroll_options_and_limits},
    {"test_virtual_list", test_virtual_list},
    {"test_region_id_overflow", test_region_id_overflow},
    {"test_thread_isolation", test_thread_isolation},
    {"test_input_overflow", test_input_overflow},
    {"test_draw_shapes_coverage", test_draw_shapes_coverage},
    {"test_tooltip_flip_above", test_tooltip_flip_above},
    {"test_combo_popup_clamp", test_combo_popup_clamp},
    {"test_text_field_undo_across_focus", test_text_field_undo_across_focus},
    {"test_text_field_kerned_caret", test_text_field_kerned_caret},
    {"test_text_field_drag_select", test_text_field_drag_select},
    {"test_text_field_drag_drop", test_text_field_drag_drop},
    {"test_text_field_drag_edit_cancels", test_text_field_drag_edit_cancels},
    {"test_text_field_long_value", test_text_field_long_value},
    {"test_popup_blocks_underlying", test_popup_blocks_underlying},
    {"test_text_field", test_text_field},
    {"test_number_field", test_number_field},
    {"test_focus_tab_clipboard", test_focus_tab_clipboard},
    {"test_number_locale", test_number_locale},
    {"test_shapes", test_shapes},
    {"test_panel_card", test_panel_card},
    {"test_panel_blocks_underlying", test_panel_blocks_underlying},
    {"test_row_cut_right_no_overlap", test_row_cut_right_no_overlap},
    {"test_device_contract", test_device_contract},
    {"test_blur", test_blur},
    {"test_multi_window", test_multi_window},
    {"test_window_pointer_invalidation", test_window_pointer_invalidation},
    {"test_titlebar", test_titlebar},
    {"test_titlebar_maximize_restore", test_titlebar_maximize_restore},
    {"test_titlebar_close_paths", test_titlebar_close_paths},
    {"test_violation_overlay", test_violation_overlay},
    {"test_interact_topmost_wins", test_interact_topmost_wins},
    {"test_keyboard_navigation", test_keyboard_navigation},
    {"test_text_atlas_paging", test_text_atlas_paging},
    {"test_text_wrapped_height", test_text_wrapped_height},
    {"test_dock_persistence", test_dock_persistence},
    {"test_dock_tabs", test_dock_tabs},
    {"test_dock_split_and_ratio", test_dock_split_and_ratio},
    {"test_dock_drag_action", test_dock_drag_action},
    {"test_dock_tabs_no_overflow", test_dock_tabs_no_overflow},
    {"test_dock_drop_left_zone", test_dock_drop_left_zone},
    {"test_dock_content_clipped", test_dock_content_clipped},
    {"test_float_panel_dock_drag", test_float_panel_dock_drag},
    {"test_panel_close_button", test_panel_close_button},
    // These three were written but never registered in the old single-file
    // main (r88 split found them); they run last, in file order.
    {"test_context_menu_clamp", test_context_menu_clamp},
    {"test_popup_retract_no_ghost", test_popup_retract_no_ghost},
    {"test_checkbox_slider_edge", test_checkbox_slider_edge},
};

int main(int argc, char **argv)
{
    const char *filter = nullptr;
    bool list_only = false;
    for (i32 i = 1; i < argc; ++i)
    {
        if (std::strncmp(argv[i], "--filter", 8) == 0)
        {
            if (argv[i][8] == '=')
                filter = argv[i] + 9;
            else if (i + 1 < argc)
                filter = argv[++i];
        }
        else if (std::strcmp(argv[i], "--list") == 0)
            list_only = true;
    }

    i32 run = 0;
    for (const auto &t : TESTS)
    {
        if (filter && (!t.name || !std::strstr(t.name, filter))) continue;
        if (list_only)
        {
            std::printf("%s\n", t.name);
            continue;
        }
        t.fn();
        ++run;
    }
    if (list_only) return 0;

    if (g_failures == 0)
    {
        if (run == static_cast<i32>(sizeof(TESTS) / sizeof(TESTS[0])))
            std::printf("all core tests passed\n");
        else
            std::printf("%d test(s) passed (filtered)\n", run);
        return 0;
    }
    std::printf("%d core test(s) failed\n", g_failures);
    return 1;
}