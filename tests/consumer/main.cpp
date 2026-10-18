// An external program built against the installed package: it uses only the
// documented surface (no context fields), so a change that breaks it breaks
// real clients too.
#include <pufferui/pufferui.h>

#include <cstring>
#include <string>

int main()
{
    if (std::strncmp(pui::version(), "0.", 2) != 0 && PUFFERUI_VERSION_MAJOR == 0) return 2;

    pui::null_device device;
    pui::context *ctx = pui::create_context(&device, device.create_surface());
    bool ok = true;
    std::string notes = "one\ntwo";
    pui::i32 choice = 0;
    static const char *const items[] = {"a", "b"};
    pui::begin_frame(ctx, 0, 0.016, {0, 0, 100, 100});
    {
        pui::ui u(ctx);
        pui::field_opts opts{.enabled = false, .read_only = true};
        auto disabled = u.disabled(!opts.enabled);
        u.draw_rect({0, 0, 10, 10}, {255, 255, 255, 255});
        // the frame accessors replace context fields
        ok = ok && u.now() == 0.0 && u.dt() > 0.0 && u.current_window() != nullptr;
        ok = ok && u.mods() == pui::MOD_NONE && !u.mouse_down();
        (void)u.mouse();
        (void)u.clip_rect();
        (void)u.text_area({0, 20, 100, 60}, notes, pui::hash("notes"));
        ok = ok && u.editor_info(pui::hash("notes")).caret <= notes.size();
        (void)u.combo({0, 80, 60, 20}, "", std::span<const char *const>(items), choice,
                      pui::hash("pick"));
    }
    pui::end_frame(ctx);
    ok = ok && pui::window_count(ctx) == 1 && pui::window_by_index(ctx, 0) != nullptr;
    ok = ok && pui::violation_count(ctx) == 0;
    pui::destroy_context(ctx);
    return ok ? 0 : 1;
}
