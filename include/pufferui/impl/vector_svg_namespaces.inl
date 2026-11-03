// Resolve namespaces before SVG names are interpreted or referenced.
// Bare SVG snippets are accepted; explicit namespaces obey their lexical scope.
// Foreign metadata is inert; foreign graphical elements are explicit errors.
// Included inside pui::vector_detail after the bounded XML parser.
inline bool svg_namespaces(std::vector<svg_element> &tree, vector_error &error)
{
    constexpr std::string_view svg_uri = "http://www.w3.org/2000/svg",
                               xml_uri = "http://www.w3.org/XML/1998/namespace",
                               xlink_uri = "http://www.w3.org/1999/xlink";
    const bool snippet =
        tree[0].qualified.find(':') == std::string::npos && !svg_attr(tree[0], "xmlns");
    struct binding
    {
        std::string_view uri;
        bool found = false;
    };
    const auto resolve = [&](u32 index, std::string_view prefix) -> binding
    {
        if (prefix == "xml") return {xml_uri, true};
        for (;; index = tree[index].parent)
        {
            for (const auto &a : tree[index].attributes)
                if ((prefix.empty() && a.name == "xmlns") ||
                    (!prefix.empty() && a.name.starts_with("xmlns:") &&
                     std::string_view(a.name).substr(6) == prefix))
                    return {a.value, true};
            if (index == 0) break;
        }
        return {};
    };
    const auto split =
        [&](std::string_view name, std::string_view &prefix, std::string_view &local, usize at)
    {
        const usize colon = name.find(':');
        prefix = colon == std::string_view::npos ? std::string_view{} : name.substr(0, colon);
        local = name.substr(colon == std::string_view::npos ? 0 : colon + 1);
        if (local.empty() || local.find(':') != std::string_view::npos ||
            !(svg_alpha(local.front()) || local.front() == '_' ||
              static_cast<u8>(local.front()) >= 128))
            return fail(error, vector_error_code::INVALID_SVG, "invalid XML qualified name", at);
        return true;
    };
    std::vector<bool> metadata(tree.size());
    for (u32 i = 0; i < tree.size(); ++i)
    {
        auto &node = tree[i];
        for (const auto &attribute : node.attributes)
            if (attribute.name == "xmlns" || attribute.name.starts_with("xmlns:"))
            {
                const bool prefixed = attribute.name != "xmlns";
                const auto prefix =
                    prefixed ? std::string_view(attribute.name).substr(6) : std::string_view{};
                if ((prefixed && (prefix.empty() || prefix.find(':') != std::string_view::npos ||
                                  prefix == "xmlns" || attribute.value.empty())) ||
                    (prefix == "xml" && attribute.value != xml_uri) ||
                    (prefix != "xml" && attribute.value == xml_uri) ||
                    attribute.value == "http://www.w3.org/2000/xmlns/")
                    return fail(error, vector_error_code::INVALID_SVG,
                                "invalid reserved XML namespace declaration", attribute.offset);
            }
        metadata[i] = i > 0 && (metadata[node.parent] || tree[node.parent].name == "metadata");
        if (metadata[i]) continue;
        std::string_view prefix, local;
        if (!split(node.qualified, prefix, local, node.offset)) return false;
        const auto ns = resolve(i, prefix);
        if (!prefix.empty() && !ns.found)
            return fail(error, vector_error_code::INVALID_SVG, "undeclared SVG namespace prefix",
                        node.offset);
        if (ns.uri != svg_uri && !(prefix.empty() && !ns.found && snippet))
            return fail(error, vector_error_code::UNSUPPORTED, "foreign SVG graphical namespace",
                        node.offset);
        for (auto &a : node.attributes)
        {
            if (a.name == "xmlns" || a.name.starts_with("xmlns:"))
            {
                if (a.name == "xmlns:xml" && a.value != xml_uri)
                    return fail(error, vector_error_code::INVALID_SVG,
                                "reserved XML namespace binding", a.offset);
                continue;
            }
            if (!split(a.name, prefix, local, a.offset)) return false;
            if (prefix.empty()) continue;
            const auto attribute_ns = resolve(i, prefix);
            if (!attribute_ns.found)
                return fail(error, vector_error_code::INVALID_SVG, "undeclared attribute namespace",
                            a.offset);
            if (attribute_ns.uri == xml_uri)
            {
                if (local == "base" && !a.value.empty())
                    return fail(error, vector_error_code::UNSUPPORTED,
                                "SVG XML base is unsupported", a.offset);
            }
            else if (attribute_ns.uri == xlink_uri)
            {
                const std::string canonical = "xlink:" + std::string(local);
                for (const auto &other : node.attributes)
                    if (&other != &a && other.name == canonical)
                        return fail(error, vector_error_code::INVALID_SVG,
                                    "duplicate expanded SVG attribute", a.offset);
                a.name = canonical;
            }
            else
                return fail(error, vector_error_code::UNSUPPORTED,
                            "foreign SVG attribute namespace", a.offset);
        }
    }
    return true;
}
