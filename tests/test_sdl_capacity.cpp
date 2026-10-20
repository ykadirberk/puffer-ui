#include "test_util.h"
#include "../src/sdl_d3d11_buffer.h"

PUI_TEST(sdl_d3d11_capacity_reuses_storage_and_bounds_growth)
{
    CHECK(pui_d3d11_vertex_capacity(0, 0) == 0);
    CHECK(pui_d3d11_vertex_capacity(0, 1) == 65536);
    CHECK(pui_d3d11_vertex_capacity(65536, 65536) == 65536);
    CHECK(pui_d3d11_vertex_capacity(65536, 65537) == 262144);
    CHECK(pui_d3d11_vertex_capacity(131072, 1) == 131072);
    CHECK(pui_d3d11_vertex_capacity(UINT_MAX / 2 + 1u, UINT_MAX) == UINT_MAX);
    if (sizeof(usize) > sizeof(unsigned))
        CHECK(pui_d3d11_vertex_capacity(0, static_cast<usize>(UINT_MAX) + 1) == 0);
    // Actual increasing payloads observed in the 1150-tile Spiral insertion.
    // Warmed capacity comes from the first full tile batch, not from exact
    // upstream capacities. The whole insertion peak must fit that allocation.
    usize capacity = pui_d3d11_vertex_capacity(65536, 35576448), growths = 0;
    for (usize bytes :
         {42578880u, 48390336u, 56165952u, 59356800u, 61844352u, 66244992u, 70053120u, 71625408u})
    {
        const usize next = pui_d3d11_vertex_capacity(capacity, bytes);
        CHECK(next >= bytes && next >= capacity);
        CHECK(next <= capacity * 4);
        if (next != capacity) ++growths;
        capacity = next;
    }
    CHECK(growths == 0);
}
