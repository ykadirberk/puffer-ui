#pragma once

#include "material_icons.h"
#include <cmath>
#include <cstdio>

namespace pui::example_assets
{
enum icon_kind : i32
{
    ICON_GRID,
    ICON_LAYERS,
    ICON_BOARD,
    ICON_SPARK,
    ICON_WAVE,
    ICON_GEAR,
    ICON_BELL,
    ICON_SEARCH,
    ICON_PLUS,
    ICON_MINUS,
    ICON_CHEVRON,
    ICON_ROCKET,
    ICON_SHUFFLE,
    ICON_SORT,
    ICON_CHECK,
    ICON_COUNT,
};

inline constexpr const char *SHOWCASE_ICON_FILES[] = {
    PUFFERUI_ASSET_DIR "/icons/material/home.svg",
    PUFFERUI_ASSET_DIR "/icons/material/layers.svg",
    PUFFERUI_ASSET_DIR "/icons/material/view_column.svg",
    PUFFERUI_ASSET_DIR "/icons/material/auto_awesome.svg",
    PUFFERUI_ASSET_DIR "/icons/material/waves.svg",
    PUFFERUI_ASSET_DIR "/icons/material/settings.svg",
    PUFFERUI_ASSET_DIR "/icons/material/notifications.svg",
    PUFFERUI_ASSET_DIR "/icons/material/search.svg",
    PUFFERUI_ASSET_DIR "/icons/material/add.svg",
    PUFFERUI_ASSET_DIR "/icons/material/remove.svg",
    PUFFERUI_ASSET_DIR "/icons/material/chevron_left.svg",
    PUFFERUI_ASSET_DIR "/icons/material/rocket_launch.svg",
    PUFFERUI_ASSET_DIR "/icons/material/shuffle.svg",
    PUFFERUI_ASSET_DIR "/icons/material/sort.svg",
    PUFFERUI_ASSET_DIR "/icons/material/check.svg",
};
static_assert(std::size(SHOWCASE_ICON_FILES) == ICON_COUNT);

struct showcase_icon_set
{
    // The largest icon is 22 logical pixels. Leave room for 4x framebuffer DPI.
    static constexpr f32 MAX_PIXELS = 96.0f;
    vector_handle images[ICON_COUNT]{};

    void prepare(ui &u)
    {
        // Prepare white once. Animation and theme colors use a per-draw tint.
        const vector_prepare_opts options{.max_pixel_size = {MAX_PIXELS, MAX_PIXELS},
                                          .max_instances = 16,
                                          .style = {.current_color = color::white()}};
        for (i32 i = 0; i < ICON_COUNT; ++i)
        {
            if (images[i]) continue;
            const char *filename = SHOWCASE_ICON_FILES[i];
            auto source = load_svg_file(filename);
            const auto error =
                source ? u.prepare_vector(source.image, options).error : source.error;
            if (error.code == vector_error_code::NONE)
                images[i] = source.image;
            else
            {
                std::fprintf(stderr, "showcase SVG icon %s: %s\n", filename, error.message);
                destroy_vector(source.image);
            }
        }
    }

    bool draw(ui &u, i32 kind, vec2 center, f32 size, color tint, f32 angle = 0) const
    {
        if (kind < 0 || kind >= ICON_COUNT || !images[kind]) return false;
        affine2 transform;
        if (angle != 0)
        {
            const f32 c = std::cos(angle), s = std::sin(angle);
            transform = {c,
                         s,
                         -s,
                         c,
                         center.x - c * center.x + s * center.y,
                         center.y - s * center.x - c * center.y};
        }
        const rect area{center.x - size * .5f, center.y - size * .5f, size, size};
        return static_cast<bool>(u.draw_vector(
            area, images[kind],
            {.transform = transform, .style = {.current_color = color::white(), .tint = tint}}));
    }

    void release(ui &u)
    {
        for (auto &image : images)
        {
            if (!image) continue;
            (void)u.release_vector(image);
            destroy_vector(image);
        }
    }
};
} // namespace pui::example_assets
