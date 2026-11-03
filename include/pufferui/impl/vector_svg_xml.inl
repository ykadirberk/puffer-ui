// Bounded XML tree for SVG icon resources; no entity expansion or external I/O.
// Included inside pui::vector_detail, after SVG numeric/path parsing.
// Attributes own decoded UTF-8; source offsets remain available for diagnostics.
// DTDs, scripts and external resources are never evaluated.
struct svg_attribute
{
    std::string name, value;
    usize offset = 0;
    bool important = false;
};
inline std::string svg_lower(std::string_view value)
{
    std::string out(value);
    for (char &c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
struct svg_element
{
    std::string name, qualified;
    std::vector<svg_attribute> attributes, style;
    std::vector<u32> children;
    usize offset = 0;
    u32 parent = 0;
};
inline bool svg_utf8(std::string_view s)
{
    for (usize i = 0; i < s.size();)
    {
        const u8 first = static_cast<u8>(s[i++]);
        if (first == 0) return false;
        if (first < 128)
        {
            if (first < 32 && !svg_space(static_cast<char>(first))) return false;
            continue;
        }
        u32 value = 0, minimum = 0;
        i32 count = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            value = first & 31;
            minimum = 128;
            count = 1;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            value = first & 15;
            minimum = 2048;
            count = 2;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            value = first & 7;
            minimum = 65536;
            count = 3;
        }
        else
            return false;
        if (s.size() - i < static_cast<usize>(count)) return false;
        for (i32 n = 0; n < count; ++n)
        {
            const u8 c = static_cast<u8>(s[i++]);
            if ((c & 192) != 128) return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            value == 0xfffe || value == 0xffff)
            return false;
    }
    return true;
}
inline void svg_utf8_append(std::string &out, u32 c)
{
    if (c < 128)
        out.push_back(static_cast<char>(c));
    else if (c < 2048)
    {
        out.push_back(static_cast<char>(192 | (c >> 6)));
        out.push_back(static_cast<char>(128 | (c & 63)));
    }
    else if (c < 65536)
    {
        out.push_back(static_cast<char>(224 | (c >> 12)));
        out.push_back(static_cast<char>(128 | ((c >> 6) & 63)));
        out.push_back(static_cast<char>(128 | (c & 63)));
    }
    else
    {
        out.push_back(static_cast<char>(240 | (c >> 18)));
        out.push_back(static_cast<char>(128 | ((c >> 12) & 63)));
        out.push_back(static_cast<char>(128 | ((c >> 6) & 63)));
        out.push_back(static_cast<char>(128 | (c & 63)));
    }
}
inline bool svg_decode(std::string_view input, std::string &out, vector_error &error, usize origin)
{
    for (usize i = 0; i < input.size(); ++i)
    {
        if (input[i] != '&')
        {
            out.push_back(input[i] == '\r' || input[i] == '\n' || input[i] == '\t' ? ' '
                                                                                   : input[i]);
            continue;
        }
        const usize end = input.find(';', i + 1);
        if (end == std::string_view::npos)
            return fail(error, vector_error_code::INVALID_SVG, "unterminated XML entity",
                        origin + i);
        auto entity = input.substr(i + 1, end - i - 1);
        u32 code = 0;
        if (entity == "amp")
            code = '&';
        else if (entity == "lt")
            code = '<';
        else if (entity == "gt")
            code = '>';
        else if (entity == "quot")
            code = '"';
        else if (entity == "apos")
            code = '\'';
        else if (!entity.empty() && entity.front() == '#')
        {
            entity.remove_prefix(1);
            i32 base = 10;
            if (!entity.empty() && entity.front() == 'x')
            {
                base = 16;
                entity.remove_prefix(1);
            }
            const auto value =
                std::from_chars(entity.data(), entity.data() + entity.size(), code, base);
            if (entity.empty() || value.ec != std::errc{} ||
                value.ptr != entity.data() + entity.size() || code == 0 || code > 0x10ffff ||
                (code >= 0xd800 && code <= 0xdfff) || code == 0xfffe || code == 0xffff ||
                (code < 32 && !svg_space(static_cast<char>(code))))
                return fail(error, vector_error_code::INVALID_SVG,
                            "invalid XML character reference", origin + i);
        }
        else
            return fail(error, vector_error_code::UNSUPPORTED,
                        "custom XML entities are unsupported", origin + i);
        svg_utf8_append(out, code);
        i = end;
    }
    return true;
}
inline const svg_attribute *svg_attr(const svg_element &element, std::string_view name)
{
    for (const auto &a : element.attributes)
        if (a.name == name) return &a;
    return nullptr;
}
inline const svg_attribute *svg_property(const svg_element &element, std::string_view name)
{
    const svg_attribute *normal = nullptr;
    for (usize i = element.style.size(); i > 0; --i)
        if (element.style[i - 1].name == name)
        {
            if (element.style[i - 1].important) return &element.style[i - 1];
            if (!normal) normal = &element.style[i - 1];
        }
    return normal ? normal : svg_attr(element, name);
}
inline bool svg_inline_style(svg_element &element, vector_error &error)
{
    const auto *source = svg_attr(element, "style");
    if (!source) return true;
    std::string_view input = source->value;
    usize start = 0;
    while (start < input.size())
    {
        usize end = start;
        i32 depth = 0;
        char quote = 0;
        for (; end < input.size(); ++end)
        {
            const char c = input[end];
            if (quote)
            {
                if (c == quote) quote = 0;
                continue;
            }
            if (c == '\'' || c == '"')
            {
                quote = c;
                continue;
            }
            if (c == '(')
                ++depth;
            else if (c == ')')
                --depth;
            if (depth < 0)
                return fail(error, vector_error_code::INVALID_SVG, "unbalanced inline style",
                            source->offset + end);
            if (c == ';' && depth == 0) break;
        }
        if (depth || quote)
            return fail(error, vector_error_code::INVALID_SVG, "unbalanced inline style",
                        source->offset + start);
        auto declaration = trim(input.substr(start, end - start));
        if (!declaration.empty())
        {
            const usize colon = declaration.find(':');
            if (colon == std::string_view::npos)
                return fail(error, vector_error_code::INVALID_SVG, "invalid inline style",
                            source->offset + start);
            const auto name = svg_lower(trim(declaration.substr(0, colon)));
            auto value = trim(declaration.substr(colon + 1));
            const usize bang = value.rfind('!');
            const bool important = bang != std::string_view::npos &&
                                   svg_lower(trim(value.substr(bang + 1))) == "important";
            if (important) value = trim(value.substr(0, bang));
            if (name.empty() || value.empty() || element.style.size() >= 128)
                return fail(error, vector_error_code::INVALID_SVG, "invalid inline style property",
                            source->offset + start);
            element.style.push_back({name, std::string(value), source->offset + start, important});
        }
        start = end + 1;
    }
    return true;
}
inline bool svg_xml(std::string_view input, const svg_load_opts &options,
                    std::vector<svg_element> &tree, vector_error &error)
{
    if (input.size() > options.max_bytes || options.max_depth < 1 || options.max_depth > 256 ||
        options.max_commands > COMMAND_LIMIT || options.max_commands == 0)
        return fail(error, vector_error_code::LIMIT, "SVG input or configured limit");
    if (!svg_utf8(input))
        return fail(error, vector_error_code::INVALID_SVG, "SVG must be valid UTF-8 without NUL");
    usize at = input.starts_with("\xef\xbb\xbf") ? 3 : 0;
    std::vector<u32> stack;
    bool declaration = false;
    const auto whitespace = [&]()
    {
        while (at < input.size() && svg_space(input[at])) ++at;
    };
    const auto name = [&]()
    {
        const usize first = at;
        if (at < input.size() &&
            (svg_alpha(input[at]) || input[at] == '_' || static_cast<u8>(input[at]) >= 128))
        {
            ++at;
            while (at < input.size() &&
                   (svg_alpha(input[at]) || (input[at] >= '0' && input[at] <= '9') ||
                    input[at] == '_' || input[at] == '-' || input[at] == ':' || input[at] == '.' ||
                    static_cast<u8>(input[at]) >= 128))
                ++at;
        }
        return input.substr(first, at - first);
    };
    while (at < input.size())
    {
        if (input[at] != '<')
        {
            const usize end = input.find('<', at);
            const auto text =
                trim(input.substr(at, (end == std::string_view::npos ? input.size() : end) - at));
            bool descriptive = false;
            for (u32 ancestor : stack)
                if (tree[ancestor].name == "title" || tree[ancestor].name == "desc" ||
                    tree[ancestor].name == "metadata")
                {
                    descriptive = true;
                    break;
                }
            if (!text.empty() && !descriptive)
                return fail(error, vector_error_code::UNSUPPORTED,
                            "SVG text content is unsupported", at);
            at = end == std::string_view::npos ? input.size() : end;
            continue;
        }
        if (input.substr(at).starts_with("<!--"))
        {
            const usize end = input.find("-->", at + 4);
            if (end == std::string_view::npos)
                return fail(error, vector_error_code::INVALID_SVG, "unterminated XML comment", at);
            if (input.substr(at + 4, end - at - 4).find("--") != std::string_view::npos)
                return fail(error, vector_error_code::INVALID_SVG, "invalid XML comment", at);
            at = end + 3;
            continue;
        }
        if (input.substr(at).starts_with("<?xml") && at + 5 < input.size() &&
            svg_space(input[at + 5]))
        {
            if (!tree.empty() || declaration)
                return fail(error, vector_error_code::INVALID_SVG, "misplaced XML declaration", at);
            declaration = true;
            const usize end = input.find("?>", at + 5);
            if (end == std::string_view::npos)
                return fail(error, vector_error_code::INVALID_SVG, "unterminated XML declaration",
                            at);
            at = end + 2;
            continue;
        }
        if (input.substr(at).starts_with("<!") || input.substr(at).starts_with("<?"))
            return fail(error, vector_error_code::UNSUPPORTED,
                        "XML declarations/entities are unsupported", at);
        const usize begin = at++;
        const bool closing = at < input.size() && input[at] == '/';
        if (closing) ++at;
        const auto qualified = name();
        if (qualified.empty())
            return fail(error, vector_error_code::INVALID_SVG, "invalid XML element name", begin);
        if (closing)
        {
            whitespace();
            if (at == input.size() || input[at++] != '>' || stack.empty() ||
                tree[stack.back()].qualified != qualified)
                return fail(error, vector_error_code::INVALID_SVG, "mismatched SVG closing element",
                            begin);
            stack.pop_back();
            continue;
        }
        if (stack.empty() && !tree.empty())
            return fail(error, vector_error_code::INVALID_SVG, "multiple SVG roots", begin);
        if (stack.size() >= static_cast<usize>(options.max_depth) || tree.size() >= 32768)
            return fail(error, vector_error_code::LIMIT, "SVG tree limit", begin);
        svg_element element;
        element.qualified = qualified;
        const usize colon = qualified.find(':');
        element.name = qualified.substr(colon == std::string_view::npos ? 0 : colon + 1);
        element.offset = begin;
        element.parent = stack.empty() ? 0 : stack.back();
        bool empty = false;
        while (true)
        {
            const usize before = at;
            whitespace();
            if (at == input.size())
                return fail(error, vector_error_code::INVALID_SVG, "unterminated SVG start element",
                            begin);
            if (input[at] == '>')
            {
                ++at;
                break;
            }
            if (input[at] == '/' && at + 1 < input.size() && input[at + 1] == '>')
            {
                at += 2;
                empty = true;
                break;
            }
            if (at == before)
                return fail(error, vector_error_code::INVALID_SVG, "SVG attributes need whitespace",
                            at);
            const auto key = name();
            whitespace();
            if (key.empty() || at == input.size() || input[at++] != '=')
                return fail(error, vector_error_code::INVALID_SVG, "invalid SVG attribute", at);
            whitespace();
            if (at == input.size() || (input[at] != '\'' && input[at] != '"'))
                return fail(error, vector_error_code::INVALID_SVG, "SVG attribute must be quoted",
                            at);
            const char quote = input[at++];
            const usize value_start = at, end = input.find(quote, at);
            if (end == std::string_view::npos)
                return fail(error, vector_error_code::INVALID_SVG, "unterminated SVG attribute",
                            at);
            if (element.attributes.size() >= 128)
                return fail(error, vector_error_code::LIMIT, "SVG attribute limit", at);
            for (const auto &a : element.attributes)
                if (a.name == key)
                    return fail(error, vector_error_code::INVALID_SVG, "duplicate SVG attribute",
                                at);
            svg_attribute attribute;
            attribute.name = key;
            attribute.offset = value_start;
            if (input.substr(value_start, end - value_start).find('<') != std::string_view::npos ||
                !svg_decode(input.substr(value_start, end - value_start), attribute.value, error,
                            value_start))
                return fail(error, vector_error_code::INVALID_SVG, "invalid SVG attribute value",
                            value_start);
            element.attributes.push_back(std::move(attribute));
            at = end + 1;
        }
        if (!svg_inline_style(element, error)) return false;
        const u32 index = static_cast<u32>(tree.size());
        tree.push_back(std::move(element));
        if (!stack.empty()) tree[stack.back()].children.push_back(index);
        if (!empty) stack.push_back(index);
    }
    if (!stack.empty() || tree.empty() || tree[0].name != "svg")
        return fail(error, vector_error_code::INVALID_SVG, "expected complete SVG root", at);
    return true;
}
