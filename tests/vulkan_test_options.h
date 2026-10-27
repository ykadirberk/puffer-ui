#pragma once
// Existing diagnostic fixtures keep their original renderer and independent
// feature switches as the public API's defaults evolve. Assertions and pixel
// oracles remain fixed; separate default-policy tests exercise the normal API.
namespace pui::test_detail
{
inline vulkan_device_opts reference_options()
{
    return {.tile_cache = false,
            .whole_program_cache = false,
            .painter = vulkan_painter_mode::REFERENCE,
            .vector_text = false};
}
} // namespace pui::test_detail
