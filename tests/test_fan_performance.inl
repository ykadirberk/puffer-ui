// Additional tests use the unchanged original fan oracle in test_geometry_performance.inl.
#include <cfenv>
#include <array>
namespace
{
struct fan_rounding_scope
{
    int previous = std::fegetround();
    ~fan_rounding_scope() { std::fesetround(previous); }
};
struct fan_rng
{
    u32 state = 0x9a07da11;
    u32 next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    f32 bits()
    {
        const u32 n = next();
        f32 value;
        std::memcpy(&value, &n, sizeof(value));
        return value;
    }
};
rect fan_scalar_bounds(const vertex *vertices, i32 count)
{
    f32 minx = vertices[0].x, maxx = minx, miny = vertices[0].y, maxy = miny;
    for (i32 i = 1; i < count; ++i)
    {
        minx = min2(minx, vertices[i].x);
        maxx = max2(maxx, vertices[i].x);
        miny = min2(miny, vertices[i].y);
        maxy = max2(maxy, vertices[i].y);
    }
    return {minx, miny, maxx - minx, maxy - miny};
}
} // namespace

PUI_TEST(fan_directions_preserve_original_stream_in_all_rounding_modes)
{
    fan_rounding_scope restore;
    fan_rng rng;
    std::vector<vertex> actual, expected;
    std::vector<i32> indices, expected_indices;
    for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO})
    {
        CHECK(std::fesetround(mode) == 0);
        for (i32 sample = 0; sample < 20000; ++sample)
        {
            rect area{static_cast<f32>(static_cast<i32>(rng.next() % 200000) - 100000) / 16,
                      static_cast<f32>(static_cast<i32>(rng.next() % 200000) - 100000) / 16,
                      static_cast<f32>(rng.next() % 20000) / 16,
                      static_cast<f32>(rng.next() % 20000) / 16};
            auto radius = [&]() { return static_cast<f32>(rng.next() % 10000) / 32; };
            corner_radii radii{radius(), radius(), radius(), radius()};
            if (sample % 7 == 0) radii = {};
            if (sample % 19 == 0) area = {-0.0f, 0.0f, 10, 10};
            const color tint{static_cast<u8>(rng.next()), static_cast<u8>(rng.next()),
                             static_cast<u8>(rng.next()), static_cast<u8>(rng.next())};
            auto uv = [](f32 x, f32 y, f32 &u, f32 &v)
            {
                u = (x + 12.25f) * .03125f;
                v = (y - 7.75f) * .0625f;
            };
            actual.assign(sample % 3, vertex{});
            expected = actual;
            indices.clear();
            expected_indices.clear();
            detail::build_rounded_fan(area, radii, tint, uv, actual, indices);
            reference_rounded_fan(area, radii, tint, uv, expected, expected_indices);
            if (actual.size() != expected.size() || indices != expected_indices ||
                std::memcmp(actual.data(), expected.data(), actual.size() * sizeof(vertex)))
            {
                CHECK(false);
                return;
            }
        }
    }
}

PUI_TEST(fan_endpoint_bounds_match_original_order_for_extreme_inputs)
{
    fan_rounding_scope restore;
    fan_rng rng;
    std::vector<vertex> vertices;
    std::vector<i32> indices;
    for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO})
    {
        CHECK(std::fesetround(mode) == 0);
        for (i32 sample = 0; sample < 100000; ++sample)
        {
            rect area{rng.bits(), rng.bits(), rng.bits(), rng.bits()};
            corner_radii radii{rng.bits(), rng.bits(), rng.bits(), rng.bits()};
            if (sample % 3 == 0)
            {
                area = {static_cast<f32>(rng.next() % 100000) / 16,
                        static_cast<f32>(rng.next() % 100000) / 16,
                        static_cast<f32>(rng.next() % 10000) / 16,
                        static_cast<f32>(rng.next() % 10000) / 16};
                radii = corner_radii::all(static_cast<f32>(rng.next() % 10000) / 32);
            }
            if (sample % 11 == 0)
            {
                area = {-0.0f, 0.0f, 10, 10};
                radii = {};
            }
            vertices.clear();
            indices.clear();
            reference_rounded_fan(
                area, radii, {1, 2, 3, 4}, [](f32, f32, f32 &u, f32 &v) { u = v = 0; }, vertices,
                indices);
            const rect expected = fan_scalar_bounds(vertices.data(), 73);
            const rect actual = detail::rounded_fan_bounds(vertices.data());
            if (std::memcmp(&actual, &expected, sizeof(rect)))
            {
                CHECK(false);
                return;
            }
        }
    }
}

PUI_TEST(fan_prepared_work_is_bounded_without_allocations)
{
    std::vector<vertex> vertices;
    std::vector<i32> indices;
    auto uv = [](f32, f32, f32 &u, f32 &v) { u = v = 0; };
    detail::build_rounded_fan({0, 0, 100, 80}, corner_radii::all(9), color::white(), uv, vertices,
                              indices);
    detail::fan_direction_work = detail::fan_bounds_work = 0;
    allocation_count = 0;
    count_allocations = true;
    for (i32 i = 0; i < 1000; ++i)
    {
        vertices.clear();
        indices.clear();
        detail::build_rounded_fan({static_cast<f32>(i), 0, 100, 80}, corner_radii::all(9),
                                  color::white(), uv, vertices, indices);
        (void)detail::rounded_fan_bounds(vertices.data());
    }
    count_allocations = false;
    CHECK(allocation_count == 0);
    CHECK(detail::fan_direction_work == 8000);
    CHECK(detail::fan_bounds_work == 17000);
    vertices[1].x = std::numeric_limits<f32>::quiet_NaN();
    detail::fan_bounds_work = 0;
    const rect actual = detail::rounded_fan_bounds(vertices.data());
    const rect expected = fan_scalar_bounds(vertices.data(), 73);
    CHECK(std::memcmp(&actual, &expected, sizeof(rect)) == 0);
    CHECK(detail::fan_bounds_work == 90);
}

PUI_TEST(fan_shortcut_does_not_apply_to_arbitrary_meshes_of_the_same_size)
{
    tf_env env;
    std::array<vertex, 73> vertices{};
    std::array<i32, 324> indices{};
    for (auto &v : vertices) v = {-1000, -1000, 0, 0, color::white()};
    vertices[4] = {5, 5, 0, 0, color::white()}; // not one of the fan's endpoints
    begin_frame(env.c, 0, 1.0 / 60.0, {0, 0, 100, 100});
    detail::fan_bounds_work = 0;
    ui(env.c).draw_triangles(nullptr, vertices.data(), static_cast<i32>(vertices.size()),
                             indices.data(), static_cast<i32>(indices.size()));
    CHECK(detail::fan_bounds_work == 73);
    CHECK(env.c->dl->verts.size() == 73);
    end_frame(env.c);
}
