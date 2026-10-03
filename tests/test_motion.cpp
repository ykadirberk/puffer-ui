// motion: u.animate_rect / u.motion_info (animated layout). One test per rule
// of the contract: in place on first sight, springs to retargets and settles
// exactly, the origin is a frame of reference, from / snap, reduced motion,
// velocity, collection, the double-step guard, and the redraw gates.
#include "test_util.h"

namespace
{
constexpr f64 DT = 0.016; // tf_env frames are 16 ms

bool rect_eq(rect a, rect b, f32 eps = 0.001f)
{
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps &&
           std::fabs(a.w - b.w) <= eps && std::fabs(a.h - b.h) <= eps;
}
} // namespace

PUI_TEST(test_motion_first_sight_in_place)
{
    tf_env env;
    const rect target = rect::make(40, 30, 120, 50);
    rect got{};
    env.frame(0.0, [&](ui &u) { got = u.animate_rect("box"_id, target); });
    CHECK(rect_eq(got, target)); // no fly-in from 0
    env.frame(DT, [&](ui &u) { got = u.animate_rect("box"_id, target); });
    CHECK(rect_eq(got, target));
    CHECK(!needs_redraw(env.c)); // a resting rect lets the app sleep
}

PUI_TEST(test_motion_retarget_springs_and_settles)
{
    tf_env env;
    rect a = rect::make(0, 0, 100, 40), b = rect::make(200, 120, 60, 80);
    rect got{};
    f64 t = 0.0;
    env.frame(t, [&](ui &u) { got = u.animate_rect("box"_id, a); });
    t += DT;
    env.frame(t, [&](ui &u) { got = u.animate_rect("box"_id, b); });
    // one frame in: on the way, not there yet
    CHECK(got.x > a.x && got.x < b.x);
    CHECK(got.y > a.y && got.y < b.y);
    CHECK(got.w < a.w && got.h > a.h);
    bool active = false;
    env.frame(t + DT,
              [&](ui &u)
              {
                  got = u.animate_rect("box"_id, b);
                  active = u.animations_active();
              });
    CHECK(active);
    CHECK(needs_redraw(env.c)); // moving keeps frames coming
    for (i32 i = 0; i < 200; ++i)
    {
        t += DT;
        env.frame(t, [&](ui &u) { got = u.animate_rect("box"_id, b); });
    }
    CHECK(rect_eq(got, b)); // settles exactly (snaps under the epsilon)
    rect_motion_info info{};
    env.frame(t + DT, [&](ui &u) { info = u.motion_info("box"_id); });
    CHECK(info.known && info.settled);
    CHECK(info.velocity.x == 0.0f && info.velocity.y == 0.0f);
}

PUI_TEST(test_motion_origin_is_a_frame_of_reference)
{
    // scrolling the parent by 300 px moves the child with it, exactly
    tf_env env;
    const rect local = rect::make(10, 20, 50, 30);
    rect got{};
    for (i32 i = 0; i < 4; ++i)
    {
        const vec2 o{0.0f, -100.0f * static_cast<f32>(i)};
        const rect target = rect::make(o.x + local.x, o.y + local.y, local.w, local.h);
        env.frame(DT * i, [&](ui &u) { got = u.animate_rect("row"_id, target, {.origin = o}); });
        CHECK(rect_eq(got, target)); // no lag: only the origin moved
    }
}

PUI_TEST(test_motion_from_and_snap)
{
    tf_env env;
    const rect target = rect::make(100, 100, 80, 40);
    const rect dot = rect::make(140, 120, 0, 0);
    rect got{};
    env.frame(0.0, [&](ui &u) { got = u.animate_rect("in"_id, target, {.from = some(dot)}); });
    CHECK(got.w > 0.0f && got.w < target.w); // grows out of `from`
    for (i32 i = 1; i < 200; ++i)
        env.frame(DT * i,
                  [&](ui &u) { got = u.animate_rect("in"_id, target, {.from = some(dot)}); });
    CHECK(rect_eq(got, target)); // `from` only matters on first sight

    // snap: jump with the pointer; release springs home from the snapped spot
    const rect held = rect::make(300, 20, 80, 40);
    env.frame(4.0, [&](ui &u) { got = u.animate_rect("in"_id, held, {.snap = true}); });
    CHECK(rect_eq(got, held));
    env.frame(4.0 + DT, [&](ui &u) { got = u.animate_rect("in"_id, target); });
    CHECK(got.x < held.x && got.x > target.x); // from the drop point, not a teleport
}

PUI_TEST(test_motion_reduced_motion_snaps)
{
    tf_env env;
    rect got{};
    env.frame(0.0, [&](ui &u) { got = u.animate_rect("box"_id, rect::make(0, 0, 10, 10)); });
    const rect b = rect::make(500, 300, 40, 40);
    env.frame(DT,
              [&](ui &u)
              {
                  u.set_reduced_motion(true);
                  got = u.animate_rect("box"_id, b);
              });
    CHECK(rect_eq(got, b));
    CHECK(!needs_redraw(env.c));
}

PUI_TEST(test_motion_velocity_and_unknown_keys)
{
    tf_env env;
    rect_motion_info info{};
    env.frame(0.0, [&](ui &u) { info = u.motion_info("nope"_id); });
    CHECK(!info.known);
    env.frame(DT, [&](ui &u) { (void)u.animate_rect("v"_id, rect::make(0, 0, 20, 20)); });
    env.frame(2 * DT,
              [&](ui &u)
              {
                  (void)u.animate_rect("v"_id, rect::make(400, 0, 20, 20));
                  info = u.motion_info("v"_id);
              });
    CHECK(info.known && !info.settled);
    CHECK(info.velocity.x > 0.0f);              // heading right
    CHECK(std::fabs(info.velocity.y) < 0.001f); // not vertically
    CHECK(info.target.x == 400.0f);             // reported in the caller's space
    CHECK(info.current.x > 0.0f && info.current.x < 400.0f);
}

PUI_TEST(test_motion_keys_are_collected)
{
    tf_env env;
    rect got{};
    env.frame(0.0, [&](ui &u) { got = u.animate_rect("gone"_id, rect::make(0, 0, 10, 10)); });
    env.frame(6.0, [&](ui &) {}); // over 5 s of disuse
    // the key is new again: in place at the new target, no motion from the old one
    env.frame(6.0 + DT,
              [&](ui &u) { got = u.animate_rect("gone"_id, rect::make(300, 0, 10, 10)); });
    CHECK(rect_eq(got, rect::make(300, 0, 10, 10)));
}

PUI_TEST(test_motion_keys_are_region_scoped)
{
    tf_env env;
    rect a{}, b{};
    env.frame(0.0,
              [&](ui &u)
              {
                  id_scope s1 = u.scope("one"_id);
                  a = u.animate_rect("pos"_id, rect::make(0, 0, 10, 10));
              });
    env.frame(DT,
              [&](ui &u)
              {
                  {
                      id_scope s1 = u.scope("one"_id);
                      a = u.animate_rect("pos"_id, rect::make(100, 0, 10, 10));
                  }
                  id_scope s2 = u.scope("two"_id); // same key, another scope: its own entry
                  b = u.animate_rect("pos"_id, rect::make(100, 0, 10, 10));
              });
    CHECK(a.x > 0.0f && a.x < 100.0f); // moving
    CHECK(b.x == 100.0f);              // new: in place
}

PUI_TEST(test_motion_double_step_is_reported)
{
    tf_env env;
    env.expect_clean = false;
    rect first{}, second{};
    env.frame(0.0, [&](ui &u) { (void)u.animate_rect("twice"_id, rect::make(0, 0, 10, 10)); });
    g_violation_events = 0;
    env.frame(DT,
              [&](ui &u)
              {
                  first = u.animate_rect("twice"_id, rect::make(200, 0, 10, 10));
                  second = u.animate_rect("twice"_id, rect::make(200, 0, 10, 10));
              });
    CHECK(g_violation_events == 1);
    CHECK(violation_last_code(env.c) == VIOL_DUP_MOTION_KEY);
    CHECK(rect_eq(first, second)); // the second call did not step again
}

PUI_TEST(test_motion_invalid_target_is_reported)
{
    tf_env env;
    env.expect_clean = false;
    rect got = rect::make(1, 1, 1, 1);
    env.frame(0.0, [&](ui &u) { got = u.animate_rect("bad"_id, rect::make(0, 0, -5.0f, 10)); });
    CHECK(violation_last_code(env.c) == VIOL_INVALID_RECT);
    CHECK(rect_eq(got, rect{}));
}
