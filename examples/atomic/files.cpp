// files — pictures, OS drag-and-drop, file dialogs and a remembered window.
//
// Shows: `sdl3_load_image` + `ui::image` (fit modes, rounded corners),
// `ui::drop_target` for files dropped from Explorer/Finder, `sdl3_file_dialog`
// + `take_file_dialog` (asynchronous), and `save_window_state` /
// `sdl3_apply_window_state` (the window reopens where it was).
//
//   pui_ex_files   (drop pictures on the window, or press Open)
#include "../example_common.h"

#include <string>
#include <vector>

struct picture
{
    std::string name;
    image_handle img;
};

static std::vector<picture> g_pictures;
static i32 g_selected = 0;
static std::string g_status = "Drop BMP, PNG or JPEG files here, or press Open.";

static void add_picture(example_app &app, const char *path)
{
    image_handle img = sdl3_load_image(app.ctx, path);
    if (!img.valid())
    {
        g_status = std::string("Could not load ") + path;
        return;
    }
    const char *slash = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\') slash = p + 1;
    g_pictures.push_back({slash, img});
    g_selected = static_cast<i32>(g_pictures.size()) - 1;
    g_status = std::to_string(g_pictures.size()) + " picture(s)";
}

static void frame(ui &u, example_app &app)
{
    const theme &th = u.th();
    auto page = example_begin_page(u, app, "Files", "Pictures, drops, dialogs, window state");
    rect content = page.content;

    // Toolbar: Open starts the platform dialog; its result arrives a few
    // frames later through take_file_dialog.
    rect bar = content.cut_top(u.control_h());
    (void)content.cut_top(example_ui::SECTION_GAP);
    if (u.button(bar.cut_left(u.button_size("Open...").x), "Open...", "open"_id,
                 {.role = "primary"_id}))
    {
        static const file_filter filters[] = {{"Images", "png;jpg;jpeg;bmp"}, {"All files", "*"}};
        sdl3_file_dialog(app.ctx, "open_dialog"_id,
                         {.filters = filters, .allow_many = true, .parent = app.win});
    }
    const dialog_result picked = take_file_dialog(app.ctx, "open_dialog"_id);
    if (picked.done && !picked.cancelled)
        for (i32 i = 0; i < picked.count; ++i) add_picture(app, picked.path(i));
    (void)bar.cut_left(u.spacing() * 2.0f);
    u.text(bar, g_status, th.text_dim, ALIGN_LEFT);

    // The whole body is a drop target; it lights up while a drag is over it.
    const drop_result drop = u.drop_target(content, "drop_area"_id);
    if (drop.dropped)
        for (i32 i = 0; i < drop.count; ++i)
            if (drop.is_file(i)) add_picture(app, drop.item(i));

    rect thumbs = content.cut_left(150.0f);
    (void)content.cut_left(example_ui::SECTION_GAP);
    rect view = example_section(u, content, "Preview", "FIT_CONTAIN keeps the whole picture",
                                app.font_bold);
    if (!g_pictures.empty())
    {
        const picture &p = g_pictures[static_cast<usize>(g_selected)];
        rect name = view.cut_top(u.control_h());
        u.text(name, p.name, th.text, ALIGN_LEFT);
        (void)u.image(view, p.img, {.fit = FIT_CONTAIN});
    }

    rect list = example_section(u, thumbs, "Pictures", "FIT_COVER, rounded", app.font_bold);
    column col(list, u.spacing());
    for (i32 i = 0; i < static_cast<i32>(g_pictures.size()); ++i)
    {
        const rect cell = col.next(80.0f);
        if (cell.h <= 0.0f) break;
        id_scope sg = u.scope(static_cast<uiid>(i) + 1);
        const interaction in = u.interact(u.local("thumb"), cell);
        if (in.hovered) u.set_cursor(CURSOR_HAND);
        if (in.clicked) g_selected = i;
        (void)u.image(cell, g_pictures[static_cast<usize>(i)].img,
                      {.fit = FIT_COVER, .radius = th.radius});
        if (i == g_selected || in.hovered)
            u.draw_rounded_rect(rect::make(cell.x, cell.bottom() - 3.0f, cell.w, 3.0f),
                                i == g_selected ? th.accent : th.widget_hover, 1.5f);
    }

    if (drop.hovering)
    {
        const rect all =
            rect::make(0, 0, static_cast<f32>(app.width), static_cast<f32>(app.height));
        u.draw_rect(all, color{86, 156, 255, 40});
        u.text(all, "Drop to open", th.text, ALIGN_CENTER);
    }
}

// A checkerboard written to a temp BMP, dropped in by the selftest so the
// screenshot and CI exercise loading + the drop path end to end.
static std::string write_test_picture()
{
    SDL_Surface *s = SDL_CreateSurface(64, 40, SDL_PIXELFORMAT_RGBA32);
    if (!s) return {};
    for (i32 y = 0; y < s->h; ++y)
        for (i32 x = 0; x < s->w; ++x)
        {
            u8 *px = static_cast<u8 *>(s->pixels) + y * s->pitch + x * 4;
            const bool on = ((x / 8) + (y / 8)) % 2 == 0;
            px[0] = on ? 86 : 30;
            px[1] = on ? 156 : 34;
            px[2] = on ? 255 : 42;
            px[3] = 255;
        }
    const char *dir = SDL_GetBasePath();
    const std::string path = std::string(dir ? dir : "") + "pui_ex_files_test.bmp";
    const bool ok = SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    return ok ? path : std::string();
}

int main(int argc, char **argv)
{
    example_app app;
    if (!example_init(app, "files", 760, 500, argc, argv)) return example_shutdown(app);

    // The window reopens where it was (interactive runs only: CI stays put).
    char *pref = app.selftest ? nullptr : SDL_GetPrefPath("PufferUI", "pui_ex_files");
    const std::string state_file = pref ? std::string(pref) + "window.txt" : std::string();
    SDL_free(pref);
    if (!state_file.empty())
    {
        size_t len = 0;
        if (void *data = SDL_LoadFile(state_file.c_str(), &len))
        {
            window_state s{};
            if (load_window_state(std::string_view(static_cast<char *>(data), len), s))
                sdl3_apply_window_state(*app.win, s);
            SDL_free(data);
        }
        example_sync_client(app);
    }

    const std::string test_picture = app.selftest ? write_test_picture() : std::string();

    example_clock clock;
    while (app.running)
    {
        if (app.selftest && app.frame_index >= app.selftest_frames) break;
        example_tick(app, clock);
        example_pump(app);
        example_script(app);
        if (app.selftest && app.frame_index == 1 && !test_picture.empty())
        {
            // what the SDL pump delivers for a file dropped from Explorer
            drop_event(*app.win, drop_kind::BEGIN, 400.0f, 300.0f);
            drop_event(*app.win, drop_kind::FILE, 400.0f, 300.0f, test_picture.c_str());
            drop_event(*app.win, drop_kind::COMPLETE, 0.0f, 0.0f);
        }
        begin_frame(app.ctx, *app.win, app.now, app.dt);
        {
            ui u(app.ctx);
            frame(u, app);
            if (app.show_violations)
                draw_violation_overlay(u, rect::make(8.0f, app.win->area.h - 40.0f, 360.0f, 32.0f));
        }
        end_frame(app.ctx);
        const bool last =
            app.selftest ? app.frame_index + 1 >= app.selftest_frames : app.frame_index == 60;
        if (app.screenshot && last) example_screenshot(app, app.screenshot);
        app.frame_index += 1;
    }

    if (app.selftest && g_pictures.empty())
    {
        std::printf("FAIL files: the dropped test picture did not load\n");
        return 1;
    }
    if (!test_picture.empty()) SDL_RemovePath(test_picture.c_str());
    if (!state_file.empty())
    {
        const std::string s = save_window_state(get_window_state(*app.win));
        SDL_SaveFile(state_file.c_str(), s.data(), s.size());
    }
    for (picture &p : g_pictures) destroy_image(app.ctx, p.img);
    return example_shutdown(app);
}
