#pragma once

#include <pufferui/pufferui.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace pui::example_assets
{
inline constexpr char EXPLORE_SVG[] = PUFFERUI_ASSET_DIR "/icons/material/explore.svg";
inline constexpr char FAVORITE_SVG[] = PUFFERUI_ASSET_DIR "/icons/material/favorite.svg";

struct icon_file
{
    const char *label;
    const char *filename;
};

inline constexpr icon_file MATERIAL_ICONS[] = {
    {"Home", PUFFERUI_ASSET_DIR "/icons/material/home.svg"},
    {"Explore", EXPLORE_SVG},
    {"Favorite", FAVORITE_SVG},
    {"Download", PUFFERUI_ASSET_DIR "/icons/material/cloud_download.svg"},
    {"Palette", PUFFERUI_ASSET_DIR "/icons/material/palette.svg"},
    {"Settings", PUFFERUI_ASSET_DIR "/icons/material/settings.svg"},
};

// Load a bounded local file before preparing the painter. load_svg copies the XML.
inline vector_result load_svg_file(const char *filename)
{
    const svg_load_opts limits{};
    const std::string_view name(filename);
    const std::filesystem::path path(std::u8string(name.begin(), name.end()));
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const std::streamoff length = file.tellg();
    if (length < 0) return {nullptr, {vector_error_code::INVALID_SVG, 0, "cannot open SVG file"}};
    if (static_cast<u64>(length) > limits.max_bytes)
        return {nullptr, {vector_error_code::LIMIT, 0, "SVG file exceeds the byte limit"}};
    std::string xml(static_cast<usize>(length), '\0');
    if (!file.seekg(0) || !file.read(xml.data(), static_cast<std::streamsize>(length)))
        return {nullptr, {vector_error_code::INVALID_SVG, 0, "cannot read SVG file"}};
    return load_svg(xml, limits);
}
} // namespace pui::example_assets
