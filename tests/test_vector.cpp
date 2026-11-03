#include "test_util.h"

PUI_TEST(vector_path_sources_copy_commands_and_have_curve_bounds)
{
    path_command commands[] = {{path_verb::MOVE, {0, 0}},
                               {path_verb::CUBIC, {0, 100}, {100, 100}, {100, 0}},
                               {path_verb::CLOSE}};
    auto result = create_path({.commands = commands});
    CHECK(static_cast<bool>(result));
    CHECK(result.error.code == vector_error_code::NONE);
    commands[1].p2 = {999, 999};
    const rect box = vector_view_box(result.image);
    CHECK(box.x == 0 && box.y == 0 && box.w == 100 && box.h == 75);
    destroy_vector(result.image);
    CHECK(result.image == nullptr);
    destroy_vector(result.image);
    CHECK(vector_view_box(nullptr).w == 0);

    const path_command quadratic[] = {{path_verb::MOVE, {0, 0}},
                                      {path_verb::QUADRATIC, {50, 100}, {100, 0}}};
    result = create_path({.commands = quadratic});
    CHECK(static_cast<bool>(result));
    CHECK(std::fabs(vector_view_box(result.image).h - 50) < .0001f);
    destroy_vector(result.image);
}

PUI_TEST(vector_invalid_sources_return_structured_errors_without_ui_violations)
{
    tf_env env;
    path_command commands[] = {{path_verb::LINE, {10, 10}}};
    auto result = create_path({.commands = commands});
    CHECK(!result && result.error.code == vector_error_code::INVALID_PATH);
    commands[0] = {path_verb::MOVE, {std::numeric_limits<f32>::infinity(), 0}};
    result = create_path({.commands = commands});
    CHECK(!result && result.error.code == vector_error_code::INVALID_PATH);
    commands[0] = {path_verb::MOVE, {0, 0}};
    gradient_stop stops[] = {{.75f, color::white()}, {.25f, color::black()}};
    result = create_path(
        {.commands = commands, .fill = {.kind = vector_paint_kind::LINEAR, .stops = stops}});
    CHECK(!result && result.error.code == vector_error_code::INVALID_PAINT);
    const f32 dashes[] = {4, -1};
    result = create_path({.commands = commands, .stroke_options = {.width = 1, .dashes = dashes}});
    CHECK(!result && result.error.code == vector_error_code::INVALID_PAINT);
    result = create_path({.commands = commands, .view_box = {0, 0, -1, 10}});
    CHECK(!result && result.error.code == vector_error_code::INVALID_PATH);
}

PUI_TEST(vector_paths_have_explicit_viewports_and_independent_ownership)
{
    const path_command commands[] = {{path_verb::MOVE, {1, 2}}, {path_verb::LINE, {6, 8}}};
    auto a = create_path({.commands = commands, .view_box = {-10, -20, 100, 200}});
    auto b = create_path({.commands = commands});
    CHECK(a && b && a.image != b.image);
    CHECK(vector_view_box(a.image).x == -10 && vector_view_box(a.image).h == 200);
    destroy_vector(a.image);
    CHECK(vector_view_box(b.image).x == 1 && vector_view_box(b.image).w == 5);
    destroy_vector(b.image);
}

PUI_TEST(vector_svg_icons_import_paths_shapes_references_and_presentation)
{
    tf_env env;
    constexpr std::string_view documents[] = {
        R"(<svg viewBox="10 20 64 96"><path d="M10 20h5v6l2-3q1 2 3 4t4 5c1 2 3 4 5 6s4 5 6 7a8 6 30 0 1 10 12Z"/></svg>)",
        R"svg(<svg viewBox="10 20 64 96"><g transform="translate(1 2) rotate(15 20 30) scale(2 .5) skewX(2)" fill="rebeccapurple" stroke="currentColor" stroke-width="2" stroke-linejoin="round" stroke-linecap="square" stroke-dasharray="2 1 4" opacity=".5"><rect x="10" y="20" width="50" height="70" rx="4"/><circle cx="30" cy="40" r="10"/><ellipse cx="40" cy="50" rx="8" ry="12"/><line x1="1" y1="2" x2="50" y2="80"/><polyline points="0,0 5,6 10,3"/><polygon points="10 20 20 30 40 20"/></g></svg>)svg",
        R"svg(<svg viewBox="10 20 64 96"><defs><path id="p" d="M0 0L20 0L10 30Z"/><linearGradient id="g"><stop offset="0" stop-color="red"/><stop offset="100%" style="stop-color:rgb(0,0,255);stop-opacity:.4"/></linearGradient><radialGradient id="r" href="#g" cx="40%" spreadMethod="reflect"/><clipPath id="c" clipPathUnits="objectBoundingBox"><circle cx=".5" cy=".5" r=".5"/></clipPath></defs><g clip-path="url(#c)"><use href="#p" x="20" y="30" style="fill:url('#g');stroke:#f008"/><path d="M10 20H60V90H10Z" fill="url(#r)" fill-rule="evenodd"/></g></svg>)svg",
        R"(<svg viewBox="10 20 64 96" preserveAspectRatio="xMaxYMin slice"><defs><symbol id="s" viewBox="0 0 10 10"><path d="M0 0H10V10H0Z"/></symbol></defs><use href="#s" x="10" y="20" width="64" height="96"/><svg x="10" y="20" width="16" height="24" viewBox="0 0 1 1"><circle cx=".5" cy=".5" r=".4"/></svg></svg>)",
        R"(<?xml version="1.0"?><svg viewBox="10 20 64 96"><title>A &amp; B</title><!-- comment --><path id="a&#x62;" d="M0 0L1 1"/><use href="#ab"/></svg>)"};
    for (auto document : documents)
    {
        auto result = load_svg(document);
        CHECK(static_cast<bool>(result));
        CHECK(result.error.code == vector_error_code::NONE);
        const rect box = vector_view_box(result.image);
        CHECK(box.x == 10 && box.y == 20 && box.w == 64 && box.h == 96);
        destroy_vector(result.image);
    }
}

PUI_TEST(vector_svg_malformed_and_unsupported_inputs_never_return_partial_resources)
{
    tf_env env;
    constexpr std::string_view invalid[] = {
        "",
        "<g/>",
        "<svg><g></svg>",
        "<svg/><svg/>",
        "<svg a='1' a='2'/>",
        "<svg viewBox='0 0 -1 10'/>",
        "<svg><path d='L1 2'/></svg>",
        "<svg><path d='M0 0 A5 5 0 2 1 10 10'/></svg>",
        "<svg><path d='M+-1 0'/></svg>",
        "<svg><path d='M1e999 0'/></svg>",
        "<svg><path transform=',scale(1)' d='M0 0'/></svg>",
        "<svg><path fill='url(#missing)' d='M0 0L1 1'/></svg>",
        "<svg><g id='a'/><path id='a'/></svg>",
        "<svg><defs><g id='a'><use href='#a'/></g></defs><use href='#a'/></svg>"};
    for (auto input : invalid)
    {
        const auto result = load_svg(input);
        CHECK(!result && result.image == nullptr && result.error.code != vector_error_code::NONE);
        CHECK(result.error.message != nullptr);
        CHECK(result.error.offset <= input.size());
    }
    constexpr std::string_view unsupported[] = {
        "<!DOCTYPE svg><svg/>",
        "<svg><text>x</text></svg>",
        "<svg><script/></svg>",
        "<svg><style>path{fill:red}</style></svg>",
        "<svg><image href='file.png'/></svg>",
        "<svg><use href='https://example.com/a.svg#p'/></svg>",
        "<svg filter='url(#x)'/>",
        "<svg onload='x()'/>",
        "<svg>&custom;</svg>"};
    for (auto input : unsupported)
    {
        const auto result = load_svg(input);
        CHECK(!result && result.image == nullptr &&
              result.error.code == vector_error_code::UNSUPPORTED);
    }
    CHECK(!load_svg("<svg/>", {.max_bytes = 5}));
    CHECK(!load_svg("<svg><g/></svg>", {.max_depth = 1}));
    CHECK(!load_svg("<svg><path d='M0 0L1 1'/></svg>", {.max_commands = 1}));
    CHECK(!load_svg(std::string_view("<svg>\0</svg>", 12)));
}

PUI_TEST(vector_immutable_views_supply_the_complete_scene_to_external_backends)
{
    auto result = load_svg(
        R"svg(<svg viewBox="0 0 32 16" preserveAspectRatio="xMaxYMin slice"><defs><linearGradient id="p"><stop offset="0" stop-color="red"/><stop offset="1" stop-color="blue"/></linearGradient><clipPath id="c"><rect width="32" height="16"/></clipPath></defs><g opacity=".5" clip-path="url(#c)"><path d="M0 0H32V16H0Z" fill="url(#p)" stroke="black" stroke-width="2" stroke-dasharray="4 2"/></g></svg>)svg");
    CHECK(result);
    if (!result) return;
    const auto scene = inspect_vector(result.image);
    CHECK(scene.valid && scene.view_box.w == 32 && scene.shape_count == 1 &&
          scene.clip_count == 1 && scene.operation_count == 3);
    CHECK(scene.alignment.x == 1 && scene.alignment.y == 0 && scene.slice && scene.preserve_aspect);
    const auto shape = inspect_vector_shape(result.image, 0);
    CHECK(shape.valid && shape.path.commands.size() == 5 && shape.path.view_box.h == 16);
    CHECK(shape.path.fill.kind == vector_paint_kind::LINEAR &&
          shape.path.fill.units == vector_paint_units::OBJECT_BOUNDS &&
          shape.path.fill.stops.size() == 2);
    CHECK(shape.path.stroke_options.width == 2 && shape.path.stroke_options.dashes.size() == 2);
    const auto group = inspect_vector_operation(result.image, 0);
    CHECK(group.valid && group.kind == vector_operation_kind::BEGIN_GROUP && group.opacity == .5f &&
          group.clips.size() == 1);
    CHECK(inspect_vector_clip(result.image, 0).shape_count == 1);
    CHECK(inspect_vector_clip_shape(result.image, {}).path.commands.size() == 5);
    CHECK(!inspect_vector_shape(result.image, 1).valid &&
          !inspect_vector_clip(result.image, 1).valid &&
          !inspect_vector_operation(result.image, 3).valid);
    CHECK(!inspect_vector(nullptr).valid && !inspect_vector_clip_shape(nullptr, {}).valid &&
          !retain_vector(nullptr));
    vector_handle additional = result.image;
    CHECK(retain_vector(additional));
    destroy_vector(result.image);
    CHECK(inspect_vector_shape(additional, 0).path.fill.stops[1].tint.b == 255);
    destroy_vector(additional);
    CHECK(additional == nullptr);
}
