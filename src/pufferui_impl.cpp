// The one translation unit that compiles the PufferUI implementation.
#define PUFFERUI_IMPLEMENTATION
#include <pufferui/pufferui.h>

#if defined(PUFFERUI_INTERNAL_TESTS)
#include "../tests/test_performance.inl"
#include "../tests/test_geometry_performance.inl"
#include "../tests/test_core_performance.inl"
#endif

#if defined(PUFFERUI_CORE_PERF_BENCH)
#include "../tests/bench_core_performance.inl"
#endif
