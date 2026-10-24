// Optional analytic shape and HarfBuzz/Slug text renderer.
#include "../example_common.h"
#include <pufferui/vulkan.h>

static void painter_frame(ui &u, example_app &app)
{
    const auto page = example_begin_page(
        u, app, "GPU painter", "Analytic rounded shapes and optional HarfBuzz / Slug text");
    column sections(page.content, example_ui::SECTION_GAP);
    rect shapes = example_section(u, sections.next(u.control_h() * 7), "Analytic shapes",
                                  "The same outline is evaluated at each pixel", app.font_bold);
    row cards(shapes, u.spacing());
    for (i32 i = 0; i < 4; ++i)
    {
        rect card = cards.next(shapes.w / 4 - u.spacing());
        if (!vulkan_try_rounded_gradient(u, {.area = card,
                                             .radii = corner_radii::all(8.0f + i * 8.0f),
                                             .underlay = {35, 65, 95, 255},
                                             .top_left = {50, 130, 210, 210},
                                             .top_right = {130, 80, 220, 210},
                                             .bottom_right = {80, 160, 180, 210},
                                             .bottom_left = {30, 70, 130, 210}}))
            u.draw_rounded_rect(card, {35, 65, 95, 255}, 8.0f + i * 8.0f);
    }
    rect text = example_section(u, sections.remaining(), "Vector text",
                                "HarfBuzz shapes runs; Slug evaluates font curves on the GPU",
                                app.font_bold);
    column lines(text, u.spacing());
    u.text(lines.next(u.control_h()), "office / ffi / affine", u.th().text);
    u.text(lines.next(u.control_h()), "a\xcc\x81", u.th().text);
    u.text(lines.next(u.control_h()), "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", u.th().text);
    u.text_wrapped(
        lines.remaining(),
        "Use --vulkan-painter compute to exercise GPU tile binning and the compute painter. "
        "The default here is the independent SDF raster reference. See docs/harfbuzz.md.",
        u.th().text_dim);
}

int main(int argc, char **argv)
{
    std::vector<char *> args{argv[0], const_cast<char *>("--vulkan-painter"),
                             const_cast<char *>("sdf")};
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
    args.push_back(const_cast<char *>("--vector-text"));
#endif
    for (i32 i = 1; i < argc; ++i) args.push_back(argv[i]);
    return example_run("GPU painter", 800, 540, static_cast<int>(args.size()), args.data(),
                       painter_frame);
}
