// ids — regions and stable widget IDs.
//
// Shows: `region` scoping, `"name"_id` literals, `id_child`, `u.local(key)`,
// `u.auto_id()`, and why sibling regions must not share a key.
//
//   pui_ex_ids
#include "../example_common.h"

// The same widget function, instantiated twice. The region key scopes every id
// inside it (`u.local("inc")` = id_child(region, "inc")), so the two instances
// keep independent hover/press/focus state even though the code is identical.
// Row metrics come from the theme (`u.control_h()`), never magic numbers.
static void id_counter(ui &u, const theme &th, rect r, const char *label, uiid key, i32 &value)
{
    region reg(u, key, r);
    column c(reg.content().pad(10.0f), u.spacing());
    u.text(c.next(u.text_size(label).y), label, th.text, ALIGN_LEFT);
    if (u.button(c.next(u.control_h()), "increment", u.local("inc"))) value += 1;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "value: %d", value);
    u.text(c.next(18.0f), buf, th.text_dim, ALIGN_LEFT);
}

static void ids_frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    example_page page = example_begin_page(u, app, "Widget ids",
                                           "regions scope ids; literals and auto_id() stay stable");
    column col(page.content, example_ui::SECTION_GAP);

    // two instances of one widget function: region keys scope the ids.
    {
        const rect body =
            example_section(u, col.next(168.0f), "Region-scoped ids",
                            "identical widget code, independent state per region", app.font_bold);
        static i32 left_value = 0, right_value = 0;
        row r(body, 10.0f);
        id_counter(u, th, r.next(200.0f), "left", "left"_id, left_value);
        id_counter(u, th, r.next(200.0f), "right", "right"_id, right_value);
    }

    // `u.scope(key)` — pure identity scopes: the sugar for loops and reusable
    // components. Widgets inside derive with `local("part")`; two instances
    // never collide.
    {
        const rect body = example_section(
            u, col.next(220.0f), "Identity scopes",
            "u.scope(key) scopes ids without an area or clip; rows derive with local()",
            app.font_bold);
        column c(body, u.spacing());
        for (i32 i = 0; i < 3; ++i)
        {
            id_scope s = u.scope(id_child("row"_id, static_cast<uiid>(i)));
            const rect row = c.next(u.control_h());
            const vec2 open_sz = u.button_size("open");
            if (u.button(rect::make(row.x, row.y, open_sz.x, open_sz.y), "open", u.local("open")))
            {
            }
            char label[64];
            std::snprintf(label, sizeof(label),
                          "row %d's open button: u.local(\"open\") = 0x%016llX", i,
                          static_cast<unsigned long long>(u.local("open")));
            u.text(rect::make(row.right() + u.spacing(), row.y,
                              max2(0.0f, c.bounds_.right() - row.right() - u.spacing()), row.h),
                   label, th.text_dim, ALIGN_LEFT);
        }
    }

    // `u.auto_id()` is a draw-order id: stable while the order is stable, ideal
    // for unlabeled widgets. `"literal"_id` is a compile-time hash.
    {
        const rect body = example_section(
            u, col.next(156.0f), "auto_id() and literal ids",
            "auto_id() is draw-order stable; \"left\"_id is a compile-time hash", app.font_bold);
        column c(body, 6.0f);
        row r(c.next(40.0f), 8.0f);
        for (i32 i = 0; i < 4; ++i)
        {
            const rect swatch = r.next(120.0f);
            interaction in = u.interact(u.auto_id(), swatch);
            if (in.hovered) u.set_cursor(CURSOR_HAND);
            u.draw_rounded_rect(swatch,
                                in.held      ? th.accent
                                : in.hovered ? th.widget_hover
                                             : th.widget_bg,
                                4.0f);
            char label[32];
            std::snprintf(label, sizeof(label), "auto_id #%d", i + 1);
            u.text(swatch, label, in.held ? th.bg : th.text, ALIGN_CENTER);
        }

        // ids are hashes: printing them makes collisions/debugging concrete.
        char line[192];
        std::snprintf(
            line, sizeof(line),
            "\"left\"_id = 0x%016llX   \"right\"_id = 0x%016llX   id_child(x, 7) mixes a salt",
            static_cast<unsigned long long>("left"_id),
            static_cast<unsigned long long>("right"_id));
        u.text(c.next(18.0f), line, th.text_dim, ALIGN_LEFT);
        u.text(c.next(18.0f),
               "two sibling regions with the same key would trip the duplicate-id assert",
               th.text_dim, ALIGN_LEFT);
    }
}

int main(int argc, char **argv)
{
    return example_run("ids", 720, 460, argc, argv, ids_frame);
}
