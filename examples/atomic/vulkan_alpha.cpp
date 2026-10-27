// Shared opacity preserves the ordered shapes and text inside one local group.
#include "../example_common.h"
#include <pufferui/vulkan.h>

static void alpha_frame(ui &u, example_app &app)
{
    const auto page = example_begin_page(u, app, "Vulkan alpha groups",
                                         "Shared opacity across overlapping shapes and text");
    rect area = example_section(u, page.content, "Common opacity",
                                "The same group at 25%, 50% and 75% opacity", app.font_bold);
    row cards(area, u.spacing());
    const f32 width = (area.w - u.spacing() * 2) / 3;
    for (i32 i = 0; i < 3; ++i)
    {
        const rect card = cards.next(width).pad(u.spacing() * 2);
        {
            auto alpha = vulkan_alpha(u, {.opacity = (i + 1) * 0.25f});
            if (!alpha.active())
            {
                PUFFERUI_CHECK(VIOL_SCOPE_UNBALANCED, false, "alpha preparation unavailable");
                return;
            }
            const f32 side = std::min(card.w * 0.7f, card.h * 0.65f);
            const f32 top = card.y + (card.h - side * 1.5f) * 0.5f;
            u.draw_rounded_rect({card.x, top, side, side}, {63, 121, 213, 255}, u.spacing() * 3);
            u.draw_rounded_rect({card.right() - side, top + side * 0.5f, side, side},
                                {213, 97, 151, 255}, u.spacing() * 3);
            u.text(card,
                   i == 0   ? "25%"
                   : i == 1 ? "50%"
                            : "75%",
                   {255, 255, 255, 255}, ALIGN_CENTER);
        }
    }
}
int main(int argc, char **argv)
{
    std::vector<char *> args{argv[0], const_cast<char *>("--vulkan-painter"),
                             const_cast<char *>("integrated")};
    for (i32 i = 1; i < argc; ++i) args.push_back(argv[i]);
    return example_run("Vulkan alpha groups", 800, 540, static_cast<int>(args.size()), args.data(),
                       alpha_frame);
}
