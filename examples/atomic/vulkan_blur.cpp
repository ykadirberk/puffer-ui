// Optional Vulkan-only dual-filter Kawase blur.
#include "../example_common.h"

static void blur_frame(ui &u, example_app &app)
{
    const auto page =
        example_begin_page(u, app, "Vulkan blur", "Dual-filter Kawase on a prepared image pyramid");
    rect area =
        example_section(u, page.content, "Kawase glass",
                        "The filter samples the scene prefix beneath each card", app.font_bold);
    row cards(area, u.spacing());
    for (i32 i = 0; i < 3; ++i)
    {
        rect card = cards.next((area.w - u.spacing() * 2) / 3);
        for (i32 stripe = 0; stripe < 16; ++stripe)
        {
            rect band{card.x, card.y + card.h * stripe / 16, card.w, card.h / 16};
            u.draw_rect(band, stripe % 2 ? color{50, 130, 210, 255} : color{145, 65, 125, 255});
        }
        rect glass = card.pad(u.spacing() * 2);
        u.blur(glass, 3.0f + i * 12.0f, u.spacing() * 2);
        u.draw_rounded_rect(glass, {20, 24, 30, 75}, u.spacing() * 2);
        u.text(glass,
               i == 0   ? "Radius 3"
               : i == 1 ? "Radius 15"
                        : "Radius 27",
               u.th().text, ALIGN_CENTER);
    }
}
int main(int argc, char **argv)
{
    std::vector<char *> args{argv[0], const_cast<char *>("--vulkan-blur"),
                             const_cast<char *>("kawase")};
    for (i32 i = 1; i < argc; ++i) args.push_back(argv[i]);
    return example_run("Vulkan blur", 800, 540, static_cast<int>(args.size()), args.data(),
                       blur_frame);
}
