// Application-owned SVG/path sources shared by the standalone and opt-in gallery.
#pragma once
#include "example_common.h"
#include "material_icons.h"
#include <cmath>

struct vector_gallery
{
    static constexpr i32 ICON_COUNT = static_cast<i32>(std::size(example_assets::MATERIAL_ICONS));
    static constexpr f32 ICON_PIXELS = 128;
    static constexpr f32 PREVIEW_PIXELS = 256;
    vector_handle images[3]{};
    vector_handle icons[ICON_COUNT]{};
    bool ready = false;
    f32 opacity = 1, angle = 0;
    i32 palette = 0;
    static constexpr color colors[] = {
        {82, 191, 255, 255}, {248, 128, 132, 255}, {103, 222, 175, 255}, {255, 195, 92, 255}};
    static vector_result badge()
    {
        return load_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">
          <defs><linearGradient id="ocean" x1="0" y1="0" x2="1" y2="1"><stop stop-color="currentColor"/><stop offset="1" stop-color="#6c63d9"/></linearGradient>
          <radialGradient id="light" cx=".3" cy=".25" r=".7"><stop stop-color="white" stop-opacity=".7"/><stop offset="1" stop-color="white" stop-opacity="0"/></radialGradient>
          <clipPath id="disc"><circle cx="64" cy="64" r="54"/></clipPath></defs>
          <circle cx="64" cy="64" r="54" fill="url(#ocean)"/>
          <g clip-path="url(#disc)"><path d="M0 76Q32 45 64 76T128 76V128H0Z" fill="#172646" fill-opacity=".55"/>
          <path d="M0 96Q32 65 64 96T128 96V128H0Z" fill="white" fill-opacity=".18"/>
          <circle cx="64" cy="64" r="54" fill="url(#light)"/></g>
          <g opacity=".9"><path d="M40 67C40 42 77 35 89 57L104 47V81L88 72C73 94 40 88 40 67Z" fill="white"/>
          <circle cx="77" cy="57" r="4" fill="#172646"/></g></svg>)svg");
    }
    static vector_result route()
    {
        return load_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">
          <defs><clipPath id="ring"><path d="M8 8H120V120H8ZM36 36H92V92H36Z" clip-rule="evenodd"/></clipPath></defs>
          <rect x="8" y="8" width="112" height="112" rx="22" fill="currentColor" fill-opacity=".12"/>
          <g clip-path="url(#ring)"><circle cx="64" cy="64" r="44" fill="none" stroke="currentColor" stroke-width="16" stroke-dasharray="14 9"/></g>
          <path d="M32 85C28 40 76 33 92 58M72 57L94 58L95 36" fill="none" stroke="white" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/></svg>)svg");
    }
    static vector_result heart()
    {
        const path_command commands[] = {{path_verb::MOVE, {64, 109}},
                                         {path_verb::CUBIC, {55, 100}, {13, 74}, {15, 43}},
                                         {path_verb::CUBIC, {17, 15}, {51, 12}, {64, 35}},
                                         {path_verb::CUBIC, {77, 12}, {111, 15}, {113, 43}},
                                         {path_verb::CUBIC, {115, 74}, {73, 100}, {64, 109}},
                                         {path_verb::CLOSE}};
        const gradient_stop stops[] = {{0, {255, 255, 255, 255}, 1, true},
                                       {1, {115, 80, 205, 255}}};
        return create_path(
            {.commands = commands,
             .fill = {.kind = vector_paint_kind::LINEAR,
                      .start = {20, 20},
                      .end = {110, 110},
                      .stops = stops},
             .stroke = {.kind = vector_paint_kind::SOLID, .tint = {255, 255, 255, 145}},
             .stroke_options = {.width = 2, .join = path_line_join::ROUND},
             .view_box = {0, 0, 128, 128}});
    }
    void close(example_app &app)
    {
        ui u(app.ctx);
        const auto release = [&](vector_handle &image)
        {
            if (image)
            {
                (void)u.release_vector(image);
                destroy_vector(image);
            }
        };
        for (auto &image : images) release(image);
        for (auto &icon : icons) release(icon);
    }
    void prepare(example_app &app)
    {
        app.before_shutdown = [](example_app &a, void *user)
        { static_cast<vector_gallery *>(user)->close(a); };
        app.shutdown_user = this;
        ui u(app.ctx);
        ready = true;
        const auto prepare_image = [&](vector_result source, vector_handle &image, const char *name,
                                       vector_prepare_opts options)
        {
            image = source.image;
            if (!source)
            {
                std::fprintf(stderr, "%s: %s (byte %zu)\n", name, source.error.message,
                             source.error.offset);
                ready = false;
                return;
            }
            for (auto current : colors)
            {
                options.style.current_color = current;
                if (const auto status = u.prepare_vector(image, options); !status)
                {
                    std::fprintf(stderr, "%s: %s\n", name, status.error.message);
                    ready = false;
                    return;
                }
            }
        };
        const vector_prepare_opts preview{.max_pixel_size = {PREVIEW_PIXELS, PREVIEW_PIXELS},
                                          .resource_budget_bytes = 32 * 1024 * 1024};
        prepare_image(badge(), images[0], "SVG badge", preview);
        prepare_image(route(), images[1], "Clips and strokes", preview);
        prepare_image(heart(), images[2], "Cubic path", preview);
        for (i32 i = 0; i < ICON_COUNT; ++i)
        {
            const char *filename = example_assets::MATERIAL_ICONS[i].filename;
            prepare_image(example_assets::load_svg_file(filename), icons[i], filename,
                          {.max_pixel_size = {ICON_PIXELS, ICON_PIXELS}, .max_instances = 1});
        }
        if (!ready) app.running = false;
    }
    void draw_preview(ui &u, example_app &app, rect bounds, vector_handle image, f32 pixels)
    {
        const f32 size =
            max2(0, std::min({bounds.w, bounds.h, pixels / app.win->scale}) - u.spacing() * 2);
        const vec2 center{bounds.center_x(), bounds.center_y()};
        const rect area{center.x - size * .5f, center.y - size * .5f, size, size};
        const f32 c = std::cos(angle), s = std::sin(angle);
        const affine2 rotation{c,
                               s,
                               -s,
                               c,
                               center.x - c * center.x + s * center.y,
                               center.y - s * center.x - c * center.y};
        const auto status =
            u.draw_vector(area, image,
                          {.transform = rotation,
                           .style = {.current_color = colors[palette], .opacity = opacity}});
        if (!status)
        {
            std::fprintf(stderr, "vector gallery draw failed: %s\n", status.error.message);
            ready = false;
            app.running = false;
        }
    }
    void draw_material_icons(ui &u, example_app &app, rect &content)
    {
        const auto &appearance = u.th();
        const f32 gap = u.spacing();
        const f32 inner_width = content.pad(appearance.card.padding).w;
        f32 minimum_width = 0;
        for (const auto &icon : example_assets::MATERIAL_ICONS)
            minimum_width = max2(minimum_width, u.text_size(icon.label).x + 2 * gap);
        const auto fits = [&](i32 columns)
        { return columns * minimum_width + (columns - 1) * gap <= inner_width; };
        const i32 columns = fits(ICON_COUNT) ? ICON_COUNT : fits(3) ? 3 : fits(2) ? 2 : 1;
        const i32 rows = (ICON_COUNT + columns - 1) / columns;
        const f32 cell_h = u.control_h() * 3;
        const auto &section = appearance.section;
        const f32 header_h = 2 * appearance.card.padding + section.title_h + section.caption_h +
                             section.caption_gap + section.body_gap;
        const rect card = content.cut_top(header_h + rows * cell_h + (rows - 1) * gap);
        if (card.w <= 0 || card.h <= 0) return;
        auto clip = u.region(card, "material_icons"_id);
        rect body = example_section(u, card, "Material Icons", "Google SVGs with a shared palette",
                                    app.font_bold);
        const f32 width = max2(0, (body.w - (columns - 1) * gap) / columns);
        for (i32 i = 0; i < ICON_COUNT && body.h > 0;)
        {
            row cells(body.cut_top(cell_h), gap);
            for (i32 column = 0; column < columns && i < ICON_COUNT; ++column, ++i)
            {
                rect cell = cells.next(width);
                if (cell.w <= 0 || cell.h <= 0) continue;
                const auto &icon = example_assets::MATERIAL_ICONS[i];
                u.text_ellipsis(cell.cut_bottom(u.control_h()), icon.label, appearance.text_dim,
                                ALIGN_CENTER);
                draw_preview(u, app, cell, icons[i], ICON_PIXELS);
            }
            (void)body.cut_top(gap);
        }
    }
    void operator()(ui &u, example_app &app)
    {
        const char *renderer = "Portable vector painter";
#if defined(PUFFERUI_ENABLE_VULKAN) && defined(PUFFERUI_ENABLE_HARFBUZZ)
        if (app.boot.vulkan.vector_paths) renderer = "Vulkan Slug vector paths";
#endif
        auto page = example_begin_page(u, app, "SVG and paths", renderer);
        rect controls = page.content.cut_top(u.control_h() * 2 + u.spacing());
        row sliders(controls.cut_top(u.control_h()), u.spacing());
        u.slider_float(sliders.next(controls.w * .48f), "Opacity", opacity, .15f, 1,
                       "vector_opacity"_id, "%.2f");
        u.slider_float(sliders.remaining(), "Rotation", angle, -.65f, .65f, "vector_rotation"_id,
                       "%.2f");
        row swatches(controls.cut_bottom(u.control_h()), u.spacing());
        static constexpr const char *names[] = {"Sky", "Coral", "Mint", "Gold"};
        for (i32 i = 0; i < 4; ++i)
        {
            auto scope = u.scope(static_cast<uiid>(i + 1));
            if (u.button(swatches.next(u.button_size(names[i]).x), names[i], u.local("color")))
                palette = i;
        }
        (void)page.content.cut_top(u.spacing());
        draw_material_icons(u, app, page.content);
        (void)page.content.cut_top(u.spacing());
        if (page.content.w <= 0 || page.content.h <= 0) return;
        const f32 width = std::max(0.0f, (page.content.w - u.spacing() * 2) / 3);
        row cards(page.content, u.spacing());
        const char *titles[] = {"SVG badge", "Clips and strokes", "Cubic path"};
        const char *captions[] = {"Gradients and opacity", "Clips and dashes",
                                  "Cubic path commands"};
        for (i32 i = 0; i < 3; ++i)
        {
            auto scope = u.scope(static_cast<uiid>(i + 20));
            const rect card_area = cards.next(width);
            auto card_clip = u.region(card_area, u.local("card_clip"));
            rect body = example_section(u, card_area, titles[i], captions[i], app.font_bold);
            auto clip = u.region(body, u.local("preview"));
            draw_preview(u, app, body, images[i], PREVIEW_PIXELS);
        }
    }
};

inline int vector_gallery_run(const char *title, int argc, char **argv)
{
    vector_gallery gallery;
    const int result = example_run(title, 960, 620, argc, argv, gallery);
    bool help = false;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--help")) help = true;
    return result ? result : (help || gallery.ready ? 0 : 1);
}
