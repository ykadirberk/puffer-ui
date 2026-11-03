// SVG presentation parsing shared by fill/stroke/gradient compilation.
// Included inside pui::vector_detail; all work occurs while loading a resource.
// Numeric parsing is locale-independent; styles never execute or fetch content.
// Named color values are the CSS/SVG sRGB keywords.
inline bool svg_scalar(std::string_view value, f32 &out, vector_error &error, usize at = 0,
                       bool percentage = false, f32 basis = 1)
{
    value = trim(value);
    svg_numbers n{value, 0, at, error};
    f64 number = 0;
    if (!n.number(number)) return false;
    const auto suffix = svg_lower(trim(value.substr(n.at)));
    f64 scale = 1;
    if (suffix == "%" && percentage)
        scale = basis / 100.0;
    else if (suffix.empty() || suffix == "px")
    {
    }
    else if (suffix == "in")
        scale = 96;
    else if (suffix == "cm")
        scale = 96 / 2.54;
    else if (suffix == "mm")
        scale = 96 / 25.4;
    else if (suffix == "q")
        scale = 96 / 101.6;
    else if (suffix == "pt")
        scale = 96 / 72.0;
    else if (suffix == "pc")
        scale = 16;
    else
        return fail(error, vector_error_code::UNSUPPORTED, "unsupported SVG length unit",
                    at + n.at);
    out = static_cast<f32>(number * scale);
    return std::isfinite(out)
               ? true
               : fail(error, vector_error_code::INVALID_SVG, "SVG numeric overflow", at);
}
inline bool svg_unit(std::string_view value, f32 &out, vector_error &error, usize at = 0)
{
    value = trim(value);
    svg_numbers n{value, 0, at, error};
    f64 number;
    if (!n.number(number)) return false;
    const auto suffix = trim(value.substr(n.at));
    if (suffix == "%")
        number /= 100;
    else if (!suffix.empty())
        return fail(error, vector_error_code::INVALID_SVG, "opacity must be a number or percentage",
                    at + n.at);
    out = static_cast<f32>(std::clamp(number, 0.0, 1.0));
    return true;
}
inline bool svg_color(std::string_view value, color &out, vector_error &error, usize at = 0)
{
    value = trim(value);
    const std::string lower = svg_lower(value);
    value = lower;
    const auto hex = [](char c) -> i32
    { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1); };
    if (value.starts_with('#'))
    {
        value.remove_prefix(1);
        u8 channels[4] = {0, 0, 0, 255};
        const bool short_form = value.size() == 3 || value.size() == 4;
        if (!short_form && value.size() != 6 && value.size() != 8)
            return fail(error, vector_error_code::INVALID_SVG, "invalid hex color", at);
        const usize count = short_form ? value.size() : value.size() / 2;
        for (usize i = 0; i < count; ++i)
        {
            const i32 a = hex(value[short_form ? i : 2 * i]),
                      b = short_form ? a : hex(value[2 * i + 1]);
            if (a < 0 || b < 0)
                return fail(error, vector_error_code::INVALID_SVG, "invalid hex color", at);
            channels[i] = static_cast<u8>(a * 16 + b);
        }
        out = {channels[0], channels[1], channels[2], channels[3]};
        return true;
    }
    if (value == "transparent")
    {
        out = {0, 0, 0, 0};
        return true;
    }
    if (value.starts_with("rgb(") || value.starts_with("rgba("))
    {
        if (!value.ends_with(')'))
            return fail(error, vector_error_code::INVALID_SVG, "unterminated rgb color", at);
        const bool alpha = value.starts_with("rgba(");
        const usize begin = alpha ? 5 : 4;
        const auto components = value.substr(begin, value.size() - begin - 1);
        svg_numbers n{components, 0, at + begin, error};
        u8 channels[4] = {0, 0, 0, 255};
        for (i32 i = 0; i < (alpha ? 4 : 3); ++i)
        {
            f64 number;
            if (!n.number(number)) return false;
            bool percent = n.at < components.size() && components[n.at] == '%';
            if (percent) ++n.at;
            const f64 normalized = i == 3 ? (percent ? number / 100 : number)
                                          : (percent ? number / 100 : number / 255);
            channels[i] = static_cast<u8>(std::floor(std::clamp(normalized, 0.0, 1.0) * 255 + .5));
        }
        n.whitespace();
        if (n.at != components.size())
            return fail(error, vector_error_code::INVALID_SVG, "unexpected rgb component",
                        at + n.at);
        out = {channels[0], channels[1], channels[2], channels[3]};
        return true;
    }
    struct named
    {
        std::string_view name;
        u32 rgb;
    };
    static constexpr named colors[] = {{"aliceblue", 0xf0f8ff},
                                       {"antiquewhite", 0xfaebd7},
                                       {"aqua", 0x00ffff},
                                       {"aquamarine", 0x7fffd4},
                                       {"azure", 0xf0ffff},
                                       {"beige", 0xf5f5dc},
                                       {"bisque", 0xffe4c4},
                                       {"black", 0x000000},
                                       {"blanchedalmond", 0xffebcd},
                                       {"blue", 0x0000ff},
                                       {"blueviolet", 0x8a2be2},
                                       {"brown", 0xa52a2a},
                                       {"burlywood", 0xdeb887},
                                       {"cadetblue", 0x5f9ea0},
                                       {"chartreuse", 0x7fff00},
                                       {"chocolate", 0xd2691e},
                                       {"coral", 0xff7f50},
                                       {"cornflowerblue", 0x6495ed},
                                       {"cornsilk", 0xfff8dc},
                                       {"crimson", 0xdc143c},
                                       {"cyan", 0x00ffff},
                                       {"darkblue", 0x00008b},
                                       {"darkcyan", 0x008b8b},
                                       {"darkgoldenrod", 0xb8860b},
                                       {"darkgray", 0xa9a9a9},
                                       {"darkgrey", 0xa9a9a9},
                                       {"darkgreen", 0x006400},
                                       {"darkkhaki", 0xbdb76b},
                                       {"darkmagenta", 0x8b008b},
                                       {"darkolivegreen", 0x556b2f},
                                       {"darkorange", 0xff8c00},
                                       {"darkorchid", 0x9932cc},
                                       {"darkred", 0x8b0000},
                                       {"darksalmon", 0xe9967a},
                                       {"darkseagreen", 0x8fbc8f},
                                       {"darkslateblue", 0x483d8b},
                                       {"darkslategray", 0x2f4f4f},
                                       {"darkslategrey", 0x2f4f4f},
                                       {"darkturquoise", 0x00ced1},
                                       {"darkviolet", 0x9400d3},
                                       {"deeppink", 0xff1493},
                                       {"deepskyblue", 0x00bfff},
                                       {"dimgray", 0x696969},
                                       {"dimgrey", 0x696969},
                                       {"dodgerblue", 0x1e90ff},
                                       {"firebrick", 0xb22222},
                                       {"floralwhite", 0xfffaf0},
                                       {"forestgreen", 0x228b22},
                                       {"fuchsia", 0xff00ff},
                                       {"gainsboro", 0xdcdcdc},
                                       {"ghostwhite", 0xf8f8ff},
                                       {"gold", 0xffd700},
                                       {"goldenrod", 0xdaa520},
                                       {"gray", 0x808080},
                                       {"grey", 0x808080},
                                       {"green", 0x008000},
                                       {"greenyellow", 0xadff2f},
                                       {"honeydew", 0xf0fff0},
                                       {"hotpink", 0xff69b4},
                                       {"indianred", 0xcd5c5c},
                                       {"indigo", 0x4b0082},
                                       {"ivory", 0xfffff0},
                                       {"khaki", 0xf0e68c},
                                       {"lavender", 0xe6e6fa},
                                       {"lavenderblush", 0xfff0f5},
                                       {"lawngreen", 0x7cfc00},
                                       {"lemonchiffon", 0xfffacd},
                                       {"lightblue", 0xadd8e6},
                                       {"lightcoral", 0xf08080},
                                       {"lightcyan", 0xe0ffff},
                                       {"lightgoldenrodyellow", 0xfafad2},
                                       {"lightgray", 0xd3d3d3},
                                       {"lightgrey", 0xd3d3d3},
                                       {"lightgreen", 0x90ee90},
                                       {"lightpink", 0xffb6c1},
                                       {"lightsalmon", 0xffa07a},
                                       {"lightseagreen", 0x20b2aa},
                                       {"lightskyblue", 0x87cefa},
                                       {"lightslategray", 0x778899},
                                       {"lightslategrey", 0x778899},
                                       {"lightsteelblue", 0xb0c4de},
                                       {"lightyellow", 0xffffe0},
                                       {"lime", 0x00ff00},
                                       {"limegreen", 0x32cd32},
                                       {"linen", 0xfaf0e6},
                                       {"magenta", 0xff00ff},
                                       {"maroon", 0x800000},
                                       {"mediumaquamarine", 0x66cdaa},
                                       {"mediumblue", 0x0000cd},
                                       {"mediumorchid", 0xba55d3},
                                       {"mediumpurple", 0x9370db},
                                       {"mediumseagreen", 0x3cb371},
                                       {"mediumslateblue", 0x7b68ee},
                                       {"mediumspringgreen", 0x00fa9a},
                                       {"mediumturquoise", 0x48d1cc},
                                       {"mediumvioletred", 0xc71585},
                                       {"midnightblue", 0x191970},
                                       {"mintcream", 0xf5fffa},
                                       {"mistyrose", 0xffe4e1},
                                       {"moccasin", 0xffe4b5},
                                       {"navajowhite", 0xffdead},
                                       {"navy", 0x000080},
                                       {"oldlace", 0xfdf5e6},
                                       {"olive", 0x808000},
                                       {"olivedrab", 0x6b8e23},
                                       {"orange", 0xffa500},
                                       {"orangered", 0xff4500},
                                       {"orchid", 0xda70d6},
                                       {"palegoldenrod", 0xeee8aa},
                                       {"palegreen", 0x98fb98},
                                       {"paleturquoise", 0xafeeee},
                                       {"palevioletred", 0xdb7093},
                                       {"papayawhip", 0xffefd5},
                                       {"peachpuff", 0xffdab9},
                                       {"peru", 0xcd853f},
                                       {"pink", 0xffc0cb},
                                       {"plum", 0xdda0dd},
                                       {"powderblue", 0xb0e0e6},
                                       {"purple", 0x800080},
                                       {"rebeccapurple", 0x663399},
                                       {"red", 0xff0000},
                                       {"rosybrown", 0xbc8f8f},
                                       {"royalblue", 0x4169e1},
                                       {"saddlebrown", 0x8b4513},
                                       {"salmon", 0xfa8072},
                                       {"sandybrown", 0xf4a460},
                                       {"seagreen", 0x2e8b57},
                                       {"seashell", 0xfff5ee},
                                       {"sienna", 0xa0522d},
                                       {"silver", 0xc0c0c0},
                                       {"skyblue", 0x87ceeb},
                                       {"slateblue", 0x6a5acd},
                                       {"slategray", 0x708090},
                                       {"slategrey", 0x708090},
                                       {"snow", 0xfffafa},
                                       {"springgreen", 0x00ff7f},
                                       {"steelblue", 0x4682b4},
                                       {"tan", 0xd2b48c},
                                       {"teal", 0x008080},
                                       {"thistle", 0xd8bfd8},
                                       {"tomato", 0xff6347},
                                       {"turquoise", 0x40e0d0},
                                       {"violet", 0xee82ee},
                                       {"wheat", 0xf5deb3},
                                       {"white", 0xffffff},
                                       {"whitesmoke", 0xf5f5f5},
                                       {"yellow", 0xffff00},
                                       {"yellowgreen", 0x9acd32}};
    for (const auto &named : colors)
        if (value == named.name)
        {
            out = {static_cast<u8>(named.rgb >> 16), static_cast<u8>(named.rgb >> 8),
                   static_cast<u8>(named.rgb), 255};
            return true;
        }
    return fail(error, vector_error_code::UNSUPPORTED, "unsupported SVG color", at);
}
inline bool svg_local_reference(std::string_view value, std::string &id, vector_error &error,
                                usize at = 0)
{
    value = trim(value);
    if (svg_lower(value.substr(0, 4)) == "url(")
    {
        if (!value.ends_with(')'))
            return fail(error, vector_error_code::INVALID_SVG, "unterminated SVG reference", at);
        value = trim(value.substr(4, value.size() - 5));
        if (value.size() >= 2 && ((value.front() == '\'' && value.back() == '\'') ||
                                  (value.front() == '"' && value.back() == '"')))
            value = value.substr(1, value.size() - 2);
    }
    if (value.size() < 2 || value.front() != '#')
        return fail(error, vector_error_code::UNSUPPORTED,
                    "only local SVG references are supported", at);
    id = value.substr(1);
    return true;
}
struct svg_style
{
    std::string fill = "black", outline = "none", clip_id, stop_color = "black";
    std::string width_value = "1", dash_offset_value = "0", dashes_value = "none";
    color current{0, 0, 0, 255};
    bool dynamic_color = true, hidden = false, visible = true, stroke_first = false;
    f32 fill_opacity = 1, stroke_opacity = 1, opacity = 1, stop_opacity = 1;
    stroke pen{{.width = 1}};
    path_fill_rule rule = path_fill_rule::NONZERO, clip_rule = path_fill_rule::NONZERO;
};
inline bool svg_style_apply(const svg_element &element, svg_style &style, vector_error &error,
                            vec2 viewport = {1, 1})
{
    const f32 inherited_opacity = style.opacity;
    const std::string inherited_clip = style.clip_id;
    const f32 inherited_stop_opacity = style.stop_opacity;
    const std::string inherited_stop_color = style.stop_color;
    style.opacity = 1;
    style.stop_color = "black";
    style.stop_opacity = 1;
    style.clip_id.clear();
    svg_attribute keyword;
    const auto get = [&](std::string_view key) -> const svg_attribute *
    {
        const auto *original = svg_property(element, key);
        if (!original) return nullptr;
        const auto value = trim(original->value);
        const bool word = std::all_of(value.begin(), value.end(), [](char c)
                                      { return svg_alpha(c) || svg_space(c) || c == '-'; });
        if (!word) return original;
        keyword = *original;
        keyword.value = svg_lower(value);
        return &keyword;
    };
    for (const auto &declaration : element.style)
        if (declaration.name == "d")
            return fail(error, vector_error_code::UNSUPPORTED, "CSS path data is unsupported",
                        declaration.offset);
    for (std::string_view key : {"transform-origin", "transform-box", "clip", "isolation"})
        if (const auto *a = get(key))
            return fail(error, vector_error_code::UNSUPPORTED, "unsupported SVG rendering property",
                        a->offset);
    for (std::string_view key : {"overflow", "shape-rendering"})
        if (const auto *a = get(key))
            if (key == "overflow" ? (a->value != "hidden" && a->value != "visible")
                                  : a->value != "auto")
                return fail(error, vector_error_code::UNSUPPORTED, "unsupported SVG rendering hint",
                            a->offset);
    if (const auto *a = get("color"))
    {
        if (trim(a->value) != "inherit" && svg_lower(trim(a->value)) != "currentcolor")
        {
            if (!svg_color(a->value, style.current, error, a->offset)) return false;
            style.dynamic_color = false;
        }
    }
    if (const auto *a = get("fill"))
        if (trim(a->value) != "inherit") style.fill = std::string(trim(a->value));
    if (const auto *a = get("stroke"))
        if (trim(a->value) != "inherit") style.outline = std::string(trim(a->value));
    if (const auto *a = get("stop-color"))
        style.stop_color =
            trim(a->value) == "inherit" ? inherited_stop_color : std::string(trim(a->value));
    if (const auto *a = get("stop-opacity"))
    {
        if (trim(a->value) == "inherit")
            style.stop_opacity = inherited_stop_opacity;
        else if (!svg_unit(a->value, style.stop_opacity, error, a->offset))
            return false;
    }
    if (const auto *a = get("opacity"))
        if (trim(a->value) == "inherit") style.opacity = inherited_opacity;
    for (auto item : {std::pair<std::string_view, f32 *>{"fill-opacity", &style.fill_opacity},
                      {"stroke-opacity", &style.stroke_opacity},
                      {"opacity", &style.opacity}})
        if (const auto *a = get(item.first))
            if (trim(a->value) != "inherit" && !svg_unit(a->value, *item.second, error, a->offset))
                return false;
    for (auto item : {std::pair<std::string_view, path_fill_rule *>{"fill-rule", &style.rule},
                      {"clip-rule", &style.clip_rule}})
        if (const auto *a = get(item.first))
        {
            const auto value = trim(a->value);
            if (value == "evenodd")
                *item.second = path_fill_rule::EVENODD;
            else if (value == "nonzero")
                *item.second = path_fill_rule::NONZERO;
            else if (value != "inherit")
                return fail(error, vector_error_code::INVALID_SVG, "invalid fill rule", a->offset);
        }
    if (const auto *a = get("stroke-width"))
        if (trim(a->value) != "inherit") style.width_value = a->value;
    if (const auto *a = get("stroke-dashoffset"))
        if (trim(a->value) != "inherit") style.dash_offset_value = a->value;
    if (const auto *a = get("stroke-miterlimit"))
        if (trim(a->value) != "inherit" &&
            !svg_scalar(a->value, style.pen.miter_limit, error, a->offset))
            return false;
    const f32 diagonal =
        static_cast<f32>(std::hypot(static_cast<f64>(viewport.x), viewport.y) / std::sqrt(2.0));
    if (!svg_scalar(style.width_value, style.pen.width, error, element.offset, true, diagonal) ||
        !svg_scalar(style.dash_offset_value, style.pen.dash_offset, error, element.offset, true,
                    diagonal))
        return false;
    if (style.pen.width < 0 || style.pen.miter_limit < 0)
        return fail(error, vector_error_code::INVALID_SVG, "invalid stroke width/miter limit",
                    element.offset);
    if (const auto *a = get("stroke-linecap"))
    {
        auto value = trim(a->value);
        if (value == "butt")
            style.pen.cap = path_line_cap::BUTT;
        else if (value == "round")
            style.pen.cap = path_line_cap::ROUND;
        else if (value == "square")
            style.pen.cap = path_line_cap::SQUARE;
        else if (value != "inherit")
            return fail(error, vector_error_code::INVALID_SVG, "invalid stroke cap", a->offset);
    }
    if (const auto *a = get("stroke-linejoin"))
    {
        auto value = trim(a->value);
        if (value == "miter")
            style.pen.join = path_line_join::MITER;
        else if (value == "round")
            style.pen.join = path_line_join::ROUND;
        else if (value == "bevel")
            style.pen.join = path_line_join::BEVEL;
        else if (value != "inherit")
            return fail(error, vector_error_code::UNSUPPORTED, "unsupported stroke join",
                        a->offset);
    }
    if (const auto *a = get("stroke-dasharray"))
        if (trim(a->value) != "inherit") style.dashes_value = a->value;
    {
        const auto value = trim(style.dashes_value);
        {
            style.pen.dashes.clear();
            if (value != "none")
            {
                svg_numbers n{value, 0, element.offset, error};
                while (n.available())
                {
                    if (!n.separator()) return false;
                    const usize start = n.at;
                    f64 number;
                    if (!n.number(number)) return false;
                    while (n.at < value.size() && (svg_alpha(value[n.at]) || value[n.at] == '%'))
                        ++n.at;
                    f32 v;
                    if (!svg_scalar(value.substr(start, n.at - start), v, error,
                                    element.offset + start, true, diagonal))
                        return false;
                    if (v < 0)
                        return fail(error, vector_error_code::INVALID_SVG, "invalid SVG dash",
                                    element.offset + n.at);
                    if (style.pen.dashes.size() >= DASH_LIMIT)
                        return fail(error, vector_error_code::LIMIT, "SVG dash limit",
                                    element.offset + n.at);
                    style.pen.dashes.push_back(v);
                }
                n.whitespace();
                if (n.at != value.size() || style.pen.dashes.empty())
                    return fail(error, vector_error_code::INVALID_SVG, "invalid SVG dash array",
                                element.offset + n.at);
            }
        }
    }
    if (const auto *a = get("clip-path"))
    {
        if (trim(a->value) == "inherit")
            style.clip_id = inherited_clip;
        else if (trim(a->value) != "none" &&
                 !svg_local_reference(a->value, style.clip_id, error, a->offset))
            return false;
    }
    if (const auto *a = get("display"))
        if (trim(a->value) == "none") style.hidden = true;
    if (const auto *a = get("visibility"))
    {
        const auto value = trim(a->value);
        if (value == "hidden" || value == "collapse")
            style.visible = false;
        else if (value == "visible")
            style.visible = true;
        else if (value != "inherit")
            return fail(error, vector_error_code::INVALID_SVG, "invalid SVG visibility", a->offset);
    }
    if (const auto *a = get("paint-order"))
    {
        const auto value = trim(a->value);
        if (value == "stroke" || value == "stroke fill")
            style.stroke_first = true;
        else if (value == "normal" || value == "fill" || value == "fill stroke")
            style.stroke_first = false;
        else
            return fail(error, vector_error_code::UNSUPPORTED, "unsupported SVG paint order",
                        a->offset);
    }
    for (std::string_view key : {"filter", "mask", "marker-start", "marker-mid", "marker-end"})
        if (const auto *a = get(key))
            if (trim(a->value) != "none")
                return fail(error, vector_error_code::UNSUPPORTED,
                            "SVG filters/masks/markers are unsupported", a->offset);
    if (const auto *a = get("vector-effect"))
        if (trim(a->value) != "none")
            return fail(error, vector_error_code::UNSUPPORTED,
                        "non-scaling strokes are unsupported", a->offset);
    for (std::string_view key : {"mix-blend-mode", "color-interpolation"})
        if (const auto *a = get(key))
            if (trim(a->value) != (key == "mix-blend-mode" ? "normal" : "srgb"))
                return fail(error, vector_error_code::UNSUPPORTED,
                            "unsupported SVG compositing/color space", a->offset);
    return true;
}
