#pragma once
// Existing diagnostic fixtures keep their original renderer and independent
// feature switches as the public API's defaults evolve. Assertions and pixel
// oracles remain fixed; separate default-policy tests exercise the normal API.
namespace pui::test_detail
{
// Archived implementation trees can still be profiled with the same fixture.
// The old native-observation contract is explicit in diagnostic cases only.
template <class Options> inline void select_native_observation(Options &options, bool enabled)
{
    if constexpr (requires { options.native_observation = enabled; })
        options.native_observation = enabled;
}
inline vulkan_device_opts reference_options()
{
    vulkan_device_opts options{.tile_cache = false,
                               .whole_program_cache = false,
                               .painter = vulkan_painter_mode::REFERENCE,
                               .vector_text = false};
    select_native_observation(options, true);
    return options;
}
} // namespace pui::test_detail
