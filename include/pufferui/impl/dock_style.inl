// pufferui/impl/dock_style.inl - part of the PufferUI implementation (see pufferui.h).
// Included by pufferui.h inside `namespace pui` when PUFFERUI_IMPLEMENTATION is
// defined; never include it directly. The slices concatenate to the original
// implementation block in order (tools/amalgamate.ps1 rebuilds the single header).
// ---- dock layout persistence -----------------------------------------------
static void dock_save_node(const dock_node &n, i32 depth, std::string &out)
{
    char line[512];
    if (n.kind == DOCK_SPLIT_H || n.kind == DOCK_SPLIT_V)
    {
        std::snprintf(line, sizeof(line), "d%d split %c %.4f\n", depth,
                      n.kind == DOCK_SPLIT_H ? 'h' : 'v', static_cast<double>(n.ratio));
        out += line;
        if (n.a) dock_save_node(*n.a, depth + 1, out);
        if (n.b) dock_save_node(*n.b, depth + 1, out);
        return;
    }
    // leaf: "d<depth> leaf <active> id|name;id|name"
    i32 at = static_cast<i32>(std::snprintf(line, sizeof(line), "d%d leaf %d ", depth, n.active));
    for (i32 i = 0; i < n.panel_count; ++i)
    {
        const char *name = n.panel_names[i] ? n.panel_names[i] : "panel";
        at += static_cast<i32>(std::snprintf(line + at, sizeof(line) - static_cast<usize>(at),
                                             "%s%u|%s", i > 0 ? ";" : "",
                                             static_cast<unsigned>(n.panels[i]), name));
    }
    if (at < 0) at = 0;
    if (at > static_cast<i32>(sizeof(line)) - 2) at = static_cast<i32>(sizeof(line)) - 2;
    line[at] = '\n';
    line[at + 1] = '\0';
    out += line;
}

std::string dock_save_tree(const dock_node &root)
{
    std::string out;
    dock_save_node(root, 0, out);
    return out;
}

bool dock_restore_tree(std::string_view text, dock_node *nodes, i32 node_cap,
                       std::string &name_storage, dock_node *&out_root)
{
    out_root = nullptr;
    i32 used = 0;
    // The name pointers handed out point into `name_storage`; reserve once so
    // no reallocation ever invalidates them mid-parse.
    name_storage.reserve(name_storage.size() + text.size());
    // The parse stack: (depth, node) — pop on a shallower depth, attach as the
    // first free child of the parent.
    struct frame
    {
        i32 depth;
        dock_node *n;
    };
    frame stack[MAX_DOCK_DEPTH + 1]{};
    i32 stack_top = 0;
    usize pos = 0;
    while (pos < text.size())
    {
        usize end = pos;
        while (end < text.size() && text[end] != '\n') ++end;
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (line.empty()) continue;
        if (line[0] != 'd') return false; // every line starts with its depth
        usize p = 1;
        i32 depth = 0;
        while (p < line.size() && line[p] >= '0' && line[p] <= '9')
        {
            depth = depth * 10 + (line[p] - '0');
            ++p;
        }
        if (depth > MAX_DOCK_DEPTH) return false;
        if (used >= node_cap) return false;
        while (stack_top > 0 && stack[stack_top - 1].depth >= depth) --stack_top;
        dock_node &n = nodes[used++];
        n = dock_node{};
        if (stack_top > 0)
        {
            dock_node *parent = stack[stack_top - 1].n;
            if (parent->a == nullptr)
                parent->a = &n;
            else if (parent->b == nullptr)
                parent->b = &n;
            else
                return false; // a split with three children: malformed
        }
        else
        {
            if (out_root != nullptr) return false; // two roots
            out_root = &n;
        }
        std::string_view args = line.substr(p);
        while (!args.empty() && args[0] == ' ') args.remove_prefix(1);
        if (args.size() >= 5 && args.compare(0, 5, "split") == 0)
        {
            n.kind = (args.size() > 6 && args[6] == 'v') ? DOCK_SPLIT_V : DOCK_SPLIT_H;
            const f32 v = static_cast<f32>(std::atof(std::string(args.substr(8)).c_str()));
            n.ratio = clampf(v, 0.01f, 0.99f);
            stack[stack_top].depth = depth;
            stack[stack_top].n = &n;
            ++stack_top;
            continue;
        }
        if (args.size() >= 4 && args.compare(0, 4, "leaf") == 0)
        {
            n.kind = DOCK_LEAF;
            // "<active> id|name;id|name..."
            usize q = 4;
            while (q < args.size() && args[q] == ' ') ++q;
            n.active = 0;
            while (q < args.size() && args[q] >= '0' && args[q] <= '9')
            {
                n.active = n.active * 10 + (args[q] - '0');
                ++q;
            }
            while (q < args.size() && args[q] == ' ') ++q;
            while (q < args.size())
            {
                u32 id = 0;
                while (q < args.size() && args[q] >= '0' && args[q] <= '9')
                {
                    id = id * 10 + (args[q] - '0');
                    ++q;
                }
                if (q >= args.size() || args[q] != '|') return false;
                ++q;
                usize name_start = name_storage.size();
                while (q < args.size() && args[q] != ';' && args[q] != ' ')
                {
                    name_storage.push_back(args[q]);
                    ++q;
                }
                name_storage.push_back('\0');
                if (n.panel_count < MAX_DOCK_PANELS)
                {
                    n.panels[n.panel_count] = static_cast<uiid>(id);
                    n.panel_names[n.panel_count] = name_storage.c_str() + name_start;
                    n.panel_count += 1;
                }
                if (q < args.size() && args[q] == ';') ++q;
            }
            if (n.panel_count == 0) return false;
            if (n.active >= n.panel_count) n.active = n.panel_count - 1;
            continue;
        }
        return false;
    }
    return true;
}

f32 ui::text_wrapped_height(f32 width, std::string_view s)
{
    if (s.empty() || width <= 0.0f) return 0.0f;
    const f32 line_h = line_height();
    const f32 space_w = text_width(" ");
    f32 x = 0.0f, y = 0.0f, line_w = 0.0f, height = 0.0f;
    std::string word;

    auto flush = [&]()
    {
        if (word.empty()) return;
        const f32 ww = text_width(word);
        if (line_w > 0.0f && line_w + space_w + ww > width)
        {
            y += line_h;
            x = 0.0f;
            line_w = 0.0f;
        }
        height = (y + line_h) > height ? (y + line_h) : height;
        x += ww + space_w;
        line_w += (line_w > 0.0f ? space_w : 0.0f) + ww;
        word.clear();
    };

    for (char ch : s)
    {
        if (ch == ' ' || ch == '\n')
        {
            flush();
            if (ch == '\n')
            {
                y += line_h;
                x = 0.0f;
                line_w = 0.0f;
                height = (y + line_h) > height ? (y + line_h) : height;
            }
        }
        else
        {
            word.push_back(ch);
        }
    }
    flush();
    return height;
}

f32 ui::text_fit(rect r, std::string_view s, color c, align a)
{
    if (s.empty()) return 0.0f;
    if (text_width(s) <= r.w)
    { // fits on one line
        text(r, s, c, a);
        return line_height();
    }
    const f32 h = measure_text(s, r.w).height; // wrap within the rect
    text_wrapped(rect::make(r.x, r.y, r.w, h), s, c);
    return h;
}

// ---- style cascade ----
button_style ui::resolve_button_style(uiid role_id, const button_override &ov) const
{
    button_style s = ctx->active_theme.button;
    if (role_id)
    {
        for (i32 i = 0; i < ctx->active_theme.button_role_count; ++i)
        {
            if (ctx->active_theme.button_roles[i].id == role_id)
            {
                s = merge_style(s, ctx->active_theme.button_roles[i].ov);
                break;
            }
        }
    }
    for (i32 i = 0; i < ctx->button_scope_depth; ++i) s = merge_style(s, ctx->button_scopes[i]);
    s = merge_style(s, ov);
    return s;
}

style_scope::style_scope(ui &u, button_override ov)
{
    u_ = &u;
    context *c = u.ctx;
    if (c->button_scope_depth < MAX_STYLE_SCOPES)
    {
        c->button_scopes[c->button_scope_depth++] = ov;
        pushed_ = true;
    }
    else
    {
        PUFFERUI_CHECK(VIOL_SCOPE_UNBALANCED, false, "button style scope overflow");
    }
}

style_scope::~style_scope()
{
    if (!u_ || !pushed_) return;
    context *c = u_->ctx;
    if (c->button_scope_depth > 0) --c->button_scope_depth;
}

// ---- popups ----
namespace detail
{
// Removes a popup pushed *this* frame from the live table, for widgets that
// close within the same frame they would have opened (a combo pick, an Escape
// close). Without it the entry would persist through end_frame and the next
// frame's `prev_popups` would still report the menu as open for one frame —
// enough to block a click and, for toggle-style widgets, to flash reopen.
inline void popup_retract(context *c, uiid id)
{
    for (i32 i = c->popup_depth - 1; i >= 0; --i)
    {
        if (c->popups[i].id == id)
        {
            for (i32 j = i; j < c->popup_depth - 1; ++j) c->popups[j] = c->popups[j + 1];
            c->popup_depth -= 1;
            return;
        }
    }
}
} // namespace detail

popup_scope::popup_scope(ui &u, uiid id, rect area, popup_flags flags)
{
    u_ = &u;
    id_ = id;
    area_ = area;
    context *c = u.ctx;
    if (has_flag(flags, POPUP_CLOSE_ON_CLICK_OUTSIDE))
    {
        if (c->popup_click_outside && c->popup_click_id == id) close_requested = true;
    }
    if (has_flag(flags, POPUP_CLOSE_ON_ESCAPE))
    {
        if (c->key_pressed[static_cast<i32>(key::ESCAPE)]) close_requested = true;
    }
    detail::ensure_popup_capacity(c, c->popup_depth + 1);
    c->popups[c->popup_depth++] =
        popup_entry{id, area, c->current_window ? c->current_window->index : 0};

    c->popup_layer_depth += 1;
    c->in_popup = true;
}

popup_scope::~popup_scope()
{
    // The popup entry persists for the rest of the frame so the *next* frame can
    // use it for input capture (§19.3); the stack resets in begin_frame. Only the
    // "currently drawing popup content" marker is restored here.
    if (u_)
    {
        context *c = u_->ctx;
        if (c->popup_layer_depth > 0) c->popup_layer_depth -= 1;
        c->in_popup = c->popup_layer_depth > 0;
    }
}

popup_scope ui::popup(uiid id, rect area, popup_flags flags)
{
    return popup_scope(*this, id, area, flags);
}
