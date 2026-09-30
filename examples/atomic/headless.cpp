// headless — running the core with no window at all.
//
// Shows: `null_device` + `create_context`, frames driven by synthetic input,
// `set_violation_handler` / `violation_count` for catching (and testing)
// contract violations, and `backend_caps`.
//
//   pui_ex_headless        (no SDL, no display; prints a summary)
//
//  <pufferui/pufferui.h>
#include <pufferui/pufferui.h>

#include <cstdio>

using namespace pui;

static i32 g_captured = 0;
static const char *g_last_message = "";

static void capture_violation(void *, const char * /*condition*/, const char *message,
                              const char * /*file*/, i32 /*line*/)
{
    g_captured += 1;
    g_last_message = message;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    null_device nd;
    nd.my_surface.w = 320;
    nd.my_surface.h = 200;
    context *c = create_context(&nd, nd.create_surface());
    set_violation_handler(c, capture_violation, nullptr);

    const rect screen = rect::make(0.0f, 0.0f, 320.0f, 200.0f);
    f32 value = 0.0f;
    bool flag = false;

    // The same frame loop as a windowed app; input is fed directly.
    for (i32 frame = 0; frame < 5; ++frame)
    {
        if (frame == 1)
        {
            mouse_move(c, 80.0f, 56.0f); // over the checkbox
            mouse_button(c, true);
        }
        else if (frame == 2)
        {
            mouse_button(c, false); // release over it -> click
        }
        else if (frame == 3)
        {
            mouse_move(c, 90.0f, 86.0f); // over the slider
            mouse_button(c, true);
        }
        else if (frame == 4)
        {
            mouse_button(c, false);
        }

        begin_frame(c, static_cast<f64>(frame) * (1.0 / 60.0), 1.0 / 60.0, screen);
        {
            ui u(c);
            u.draw_rect(screen, color{18, 20, 26, 255});
            (void)u.button(rect::make(10.0f, 10.0f, 120.0f, 26.0f), "Headless", "hd_btn"_id);
            (void)u.checkbox(rect::make(10.0f, 44.0f, 140.0f, 24.0f), "Flag", flag, "hd_flag"_id);
            (void)u.slider_float(rect::make(10.0f, 74.0f, 180.0f, 24.0f), "value", value, 0.0f,
                                 1.0f, "hd_value"_id, "%.2f");
        }
        end_frame(c);
    }

    // Snapshot before the violation frame (begin_frame resets the counters).
    const i32 drawn_calls = nd.draw_calls;
    const i32 drawn_vertices = nd.vertices;
    std::printf("draw calls: %d   vertices: %d   cursor sets: %d\n", drawn_calls, drawn_vertices,
                nd.cursor_sets);
    std::printf("flag: %s   value: %.2f\n", flag ? "on" : "off", static_cast<double>(value));
    std::printf("RENDER_TARGETS: %s   SHARED_DEVICE: %s\n",
                has_cap(nd.caps(), backend_caps::RENDER_TARGETS) ? "yes" : "no",
                has_cap(nd.caps(), backend_caps::SHARED_DEVICE) ? "yes" : "no");

    // Violations are captured, not printed: this is exactly how the test suite
    // asserts that a misuse is reported.
    const i32 before = g_captured;
    {
        begin_frame(c, 0.1, 1.0 / 60.0, screen);
        {
            ui u(c);
            {
                region a(u, "same"_id, rect::make(0.0f, 0.0f, 50.0f, 50.0f));
            }
            {
                // Sibling region with the same key: id_child(parent, "same") collides.
                region b(u, "same"_id, rect::make(60.0f, 0.0f, 50.0f, 50.0f));
            }
        }
        end_frame(c);
    }
    std::printf("duplicate region captured: %d (%s)\n", g_captured - before,
                g_captured > before ? g_last_message : "none");
    std::printf("violation_count(): %d\n", violation_count(c));

    const bool ok =
        drawn_calls > 0 && drawn_vertices > 0 && (g_captured - before) == 1 && value > 0.0f && flag;
    destroy_context(c);
    std::printf(ok ? "ok   headless\n" : "FAIL headless: unexpected results\n");
    return ok ? 0 : 1;
}
