// SVG document import is shared by renderer resource preparation.
// It produces immutable path/paint/scene data, never UI geometry or GPU calls.
// This slice is intentionally separate from backend raster/native compilation.
// Full icon importer implementation follows the path data/mathematics slice.
namespace vector_detail
{
#include "vector_svg_path.inl"
#include "vector_svg_xml.inl"
#include "vector_svg_namespaces.inl"
#include "vector_svg_style.inl"
#include "vector_svg_geometry.inl"
#include "vector_svg_scene.inl"
} // namespace vector_detail

vector_result load_svg(std::string_view input, const svg_load_opts &options)
try
{
    vector_error error;
    std::vector<vector_detail::svg_element> tree;
    const auto failure = [&]()
    {
        error.offset = std::min(error.offset, input.size());
        return vector_result{nullptr, error};
    };
    if (!vector_detail::svg_xml(input, options, tree, error) ||
        !vector_detail::svg_namespaces(tree, error))
        return failure();
    vector_detail::document source;
    vector_detail::svg_compiler compiler{tree, options, source, error};
    if (!compiler.compile()) return failure();
    vector_resource *result = new vector_resource;
    if (!result->identity)
    {
        delete result;
        return {nullptr, {vector_error_code::LIMIT, 0, "vector identity exhausted"}};
    }
    result->source = std::move(source);
    return {result, {}};
}
catch (const std::bad_alloc &)
{
    return {nullptr, {vector_error_code::LIMIT, 0, "SVG source allocation"}};
}
