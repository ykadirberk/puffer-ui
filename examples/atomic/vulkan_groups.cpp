// Bounded native distance unions with one fill, outline and glow.
#include "../example_common.h"
#include <pufferui/vulkan.h>

static void groups_frame(ui &u, example_app &app)
{
    const auto page = example_begin_page(u, app, "Vulkan groups",
                                         "One distance union, with shared fill, outline and glow");
    rect area =
        example_section(u, page.content, "Shared styling",
                        "Overlaps do not paint the same fill or outline twice", app.font_bold);
    row cards(area, u.spacing());
    const f32 width = (area.w - u.spacing() * 2) / 3;
    for (i32 i = 0; i < 3; ++i)
    {
        const rect card = cards.next(width).pad(u.spacing());
        const f32 side = std::min(card.w * 0.6f, card.h * 0.6f);
        const f32 top = card.y + (card.h - side * 1.5f) * 0.5f;
        const vulkan_group_shape shapes[] = {
            {{card.x, top, side, side}, corner_radii::all(u.spacing() * 3)},
            {{card.x + card.w - side, top + side * 0.5f, side, side},
             corner_radii::all(u.spacing() * 3)}};
        const bool painted =
            vulkan_shape_group(u, {.shapes = shapes,
                                   .fill = {70, 135, 215, static_cast<u8>(i == 0 ? 255 : 151)},
                                   .outline = {201, 225, 251, 231},
                                   .glow = {95, 161, 235, 107},
                                   .outline_width = u.spacing() * 0.4f,
                                   .glow_radius = i == 2 ? u.spacing() * 2 : 0});
        if (!painted) app.running = false;
    }
}
int main(int argc, char **argv)
{
    std::vector<char *> args{argv[0], const_cast<char *>("--vulkan-painter"),
                             const_cast<char *>("integrated")};
    for (i32 i = 1; i < argc; ++i) args.push_back(argv[i]);
    return example_run("Vulkan groups", 800, 540, static_cast<int>(args.size()), args.data(),
                       groups_frame);
}
