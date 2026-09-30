// Small dock-tree helpers shared by the docking example and the tour.
//
// `dock_space` reports structural moves through `dock_action`; the app owns the
// tree and applies them. These helpers implement the usual app-side policy:
// remove from the old leaf, collapse empty splits, then tab into the drop target
// (center) or split it (edges). Dropping outside undocks to `floating`.
//
//   example_dock::dock_pool pool;
//   dock_node root = ...;
//   dock_action act = u.dock_space(id, area, root, draw_panel);
//   example_dock::apply(root, act, name_fn, floating_panel, pool);
#pragma once

#include <pufferui/pufferui.h>

using namespace pui;

namespace example_dock
{
using name_fn = const char *(*)(uiid panel);

struct dock_pool
{
    dock_node nodes[16];
    i32 used = 0;
    dock_node *alloc() { return (used < 16) ? &nodes[used++] : nullptr; }
    void reset() { used = 0; }
};

inline bool node_empty(const dock_node &n)
{
    if (n.kind == DOCK_SPLIT_H || n.kind == DOCK_SPLIT_V)
        return (!n.a || node_empty(*n.a)) && (!n.b || node_empty(*n.b));
    return n.panel_count == 0;
}

inline void node_collapse(dock_node &n)
{
    if (n.kind != DOCK_SPLIT_H && n.kind != DOCK_SPLIT_V) return;
    if (n.a && node_empty(*n.a))
    {
        if (n.b) n = *n.b;
        return;
    }
    if (n.b && node_empty(*n.b))
    {
        if (n.a) n = *n.a;
        return;
    }
    if (n.a) node_collapse(*n.a);
    if (n.b) node_collapse(*n.b);
}

inline void node_remove(dock_node &n, uiid p)
{
    for (i32 i = 0; i < n.panel_count; ++i)
    {
        if (n.panels[i] != p) continue;
        for (i32 j = i; j < n.panel_count - 1; ++j)
        {
            n.panels[j] = n.panels[j + 1];
            n.panel_names[j] = n.panel_names[j + 1];
        }
        n.panel_count -= 1;
        if (n.active >= n.panel_count) n.active = (n.panel_count > 0) ? n.panel_count - 1 : 0;
        return;
    }
    if (n.a) node_remove(*n.a, p);
    if (n.b) node_remove(*n.b, p);
}

inline bool node_contains(const dock_node &n, const dock_node *target)
{
    if (&n == target) return true;
    if (n.a && node_contains(*n.a, target)) return true;
    if (n.b && node_contains(*n.b, target)) return true;
    return false;
}

inline dock_node *first_leaf(dock_node &n)
{
    if (n.kind != DOCK_SPLIT_H && n.kind != DOCK_SPLIT_V) return &n;
    if (n.a)
    {
        dock_node *r = first_leaf(*n.a);
        if (r) return r;
    }
    if (n.b) return first_leaf(*n.b);
    return nullptr;
}

inline void add_center(dock_node &t, uiid panel, name_fn name)
{
    if (t.panel_count >= MAX_DOCK_PANELS) return;
    t.panels[t.panel_count] = panel;
    t.panel_names[t.panel_count] = name(panel);
    t.panel_count += 1;
    t.active = t.panel_count - 1;
    if (t.panel_count > 1) t.kind = DOCK_TABS;
}

inline void split_at(dock_node &target, uiid panel, name_fn name, i32 zone, dock_pool &pool)
{
    dock_node *fresh = pool.alloc();
    dock_node *old_copy = pool.alloc();
    if (!fresh || !old_copy) return;

    *fresh = dock_node{};
    fresh->kind = DOCK_LEAF;
    fresh->panels[0] = panel;
    fresh->panel_names[0] = name(panel);
    fresh->panel_count = 1;

    *old_copy = target;
    const bool first = (zone == DOCK_ZONE_LEFT || zone == DOCK_ZONE_TOP);
    target.kind = (zone == DOCK_ZONE_LEFT || zone == DOCK_ZONE_RIGHT) ? DOCK_SPLIT_H : DOCK_SPLIT_V;
    target.ratio = first ? 0.35f : 0.65f;
    target.a = first ? fresh : old_copy;
    target.b = first ? old_copy : fresh;
    target.panel_count = 0;
    target.active = 0;
}

// Applies one `dock_action` to `root`. `floating` tracks the undocked panel
// (0 = none): dropping outside sets it, re-docking the floating panel clears it.
inline void apply(dock_node &root, const dock_action &act, name_fn name, uiid &floating,
                  dock_pool &pool)
{
    if (!act.active) return;

    node_remove(root, act.panel);
    node_collapse(root);

    if (!act.target)
    {
        floating = act.panel;
        return;
    }
    if (floating == act.panel) floating = 0;

    // Collapsing can invalidate the reported target; fall back to the first leaf.
    dock_node *target = act.target;
    if (!node_contains(root, target)) target = first_leaf(root);
    if (!target) return;

    if (act.zone == DOCK_ZONE_CENTER)
        add_center(*target, act.panel, name);
    else
        split_at(*target, act.panel, name, act.zone, pool);
}
} // namespace example_dock
