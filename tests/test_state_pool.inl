// Count upstream requests including aligned requests; ordinary operator new
// interception alone cannot verify a polymorphic allocator's heap use.
namespace
{
struct state_counting_resource final : std::pmr::memory_resource
{
    usize allocations = 0, live_bytes = 0, fail_after = static_cast<usize>(-1);
    void *do_allocate(usize bytes, usize alignment) override
    {
        if (allocations >= fail_after) throw std::bad_alloc();
        void *result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++allocations;
        live_bytes += bytes;
        return result;
    }
    void do_deallocate(void *pointer, usize bytes, usize alignment) override
    {
        live_bytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
    {
        return this == &other;
    }
};
} // namespace

PUI_TEST(retained_pool_handles_alignment_large_requests_and_failed_growth)
{
    state_counting_resource upstream;
    {
        retained_pool_resource pool(65536, &upstream);
        for (usize bytes : {usize(0), usize(1), usize(17), usize(257), usize(4099), usize(65536)})
        {
            void *pointer = pool.allocate(bytes, alignof(std::max_align_t));
            CHECK(reinterpret_cast<uintptr_t>(pointer) % alignof(std::max_align_t) == 0);
            if (bytes) std::memset(pointer, 0x5a, bytes);
            pool.deallocate(pointer, bytes, alignof(std::max_align_t));
            const usize before = upstream.allocations;
            void *again = pool.allocate(bytes, alignof(std::max_align_t));
            CHECK(again == pointer && upstream.allocations == before);
            pool.deallocate(again, bytes, alignof(std::max_align_t));
        }
        for (const auto request : {std::pair<usize, usize>{65537, 8}, {73, 64}})
        {
            const usize live = upstream.live_bytes;
            void *pointer = pool.allocate(request.first, request.second);
            CHECK(reinterpret_cast<uintptr_t>(pointer) % request.second == 0);
            pool.deallocate(pointer, request.first, request.second);
            CHECK(upstream.live_bytes == live);
        }
        upstream.fail_after = upstream.allocations;
        bool rejected = false;
        try
        {
            (void)pool.allocate(600, 8);
        }
        catch (const std::bad_alloc &)
        {
            rejected = true;
        }
        CHECK(rejected);
        upstream.fail_after = static_cast<usize>(-1);
        void *pointer = pool.allocate(600, 8);
        pool.deallocate(pointer, 600, 8);
    }
    CHECK(upstream.live_bytes == 0);
}

PUI_TEST(state_pool_batches_cold_nodes_and_reuses_erased_storage)
{
    state_counting_resource upstream;
    {
        pooled_state_map<anim_store::entry> map(&upstream);
        map.reserve(10000);
        const usize before = upstream.allocations;
        for (uiid i = 0; i < 10000; ++i) map[i].current = static_cast<f32>(i);
        CHECK(upstream.allocations - before < 100);
        auto *retained = &map.at(9999);
        map.reserve(20000);
        CHECK(&map.at(9999) == retained && retained->current == 9999);
        const usize warmed = upstream.allocations;
        for (uiid i = 0; i < 9999; ++i) map.erase(i);
        for (uiid i = 10000; i < 19999; ++i) map[i].current = static_cast<f32>(i);
        CHECK(upstream.allocations == warmed);
        CHECK(map.size() == 10000 && map.at(19998).current == 19998);
    }
    CHECK(upstream.live_bytes == 0);
}

PUI_TEST(frame_boundary_prepares_hits_for_previously_clipped_identities)
{
    perf_env env;
    ui u(env.c);
    env.begin();
    for (uiid i = 1; i <= 1000; ++i)
    {
        u.interact(id_child(i, "hit"_id), {-20, -20, 1, 1}, true, false);
    }
    u.interact("visible"_id, {5, 5, 2, 2}, true, false);
    end_frame(env.c);
    env.begin(1);
    CHECK(env.c->st->caption_hits.capacity() >= 1000);
    allocation_count = 0;
    count_allocations = true;
    for (uiid i = 1; i <= 1000; ++i)
    {
        u.interact(id_child(i, "hit"_id),
                   {static_cast<f32>(i % 100) * 2, static_cast<f32>(i / 100) * 2, 1, 1}, true,
                   false);
    }
    count_allocations = false;
    CHECK(allocation_count == 0 && env.c->st->caption_hits.size() == 1000);
    end_frame(env.c);
}

PUI_TEST(renderer_prepares_bounded_sector_variations_before_drawing)
{
    perf_env env;
    ui u(env.c);
    env.begin();
    u.draw_sector({100, 100}, 3, 9, 0, PI, {70, 90, 130, 180});
    end_frame(env.c);
    env.begin(1);
    CHECK(u.prepare_geometry({.vertices = 10000, .indices = 20000}));
    allocation_count = 0;
    count_allocations = true;
    for (i32 n = 0; n < 10; ++n)
        u.draw_sector({100, 100}, 3.0f + n, 9.0f + n, 0, PI * 2, {70, 90, 130, 180});
    count_allocations = false;
    CHECK(allocation_count == 0);
    end_frame(env.c);
}

PUI_TEST(renderer_pool_allocator_moves_storage_and_returns_it_to_its_owner)
{
    state_counting_resource upstream;
    {
        retained_pool_resource pool(65536, &upstream);
        text_store::text_layout source(&pool), destination;
        source.gs.resize(61);
        source.gs[30].kern_in = -1.25f;
        auto *address = source.gs.data();
        const usize before = upstream.allocations;
        destination = std::move(source);
        CHECK(destination.gs.data() == address && destination.gs[30].kern_in == -1.25f);
        CHECK(upstream.allocations == before);
        text_store::text_layout second;
        second = std::move(destination);
        CHECK(second.gs.data() == address && upstream.allocations == before);
        CHECK(second.gs.get_allocator().source == &pool);
    }
    CHECK(upstream.live_bytes == 0);
    bool rejected = false;
    try
    {
        renderer_pool_allocator<text_store::cached_glyph> allocator(&upstream);
        auto *pointer = allocator.allocate(std::numeric_limits<usize>::max());
        allocator.deallocate(pointer, std::numeric_limits<usize>::max());
    }
    catch (const std::bad_array_new_length &)
    {
        rejected = true;
    }
    CHECK(rejected && upstream.live_bytes == 0);
}

PUI_TEST(state_pool_rect_nodes_keep_values_addresses_and_context_isolation)
{
    state_counting_resource upstream;
    {
        pooled_state_map<rect_store::entry> first(&upstream), second(&upstream);
        first.reserve(6000);
        second.reserve(6000);
        const usize before = upstream.allocations;
        auto *retained = &first[42];
        retained->x[2] = 42.25f;
        retained->last_used = 9.75;
        for (uiid i = 0; i < 5000; ++i)
        {
            first[i].origin = {static_cast<f32>(i), -1};
            second[i].origin = {-1, static_cast<f32>(i)};
        }
        CHECK(upstream.allocations - before < 100);
        CHECK(&first.at(42) == retained && first.at(42).x[2] == 42.25f);
        CHECK(first.at(42).last_used == 9.75);
        CHECK(first.at(42).origin.x == 42 && second.at(42).origin.x == -1);
        const usize warmed = upstream.allocations;
        first.clear();
        for (uiid i = 0; i < 5000; ++i) CHECK(first[i].last_used == 0);
        CHECK(upstream.allocations == warmed && second.at(42).origin.y == 42);
    }
    CHECK(upstream.live_bytes == 0);
}
