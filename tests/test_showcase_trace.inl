// Additive tests of the actual showcase's optional recorder.
#include <filesystem>
#include <fstream>

namespace
{
struct trace_test_file
{
    std::string path;
    trace_test_file()
    {
        char name[96];
        std::snprintf(name, sizeof(name), "pui-trace-%llu-%p.csv",
                      static_cast<unsigned long long>(SDL_GetPerformanceCounter()),
                      static_cast<void *>(this));
        path = (std::filesystem::temp_directory_path() / name).string();
    }
    ~trace_test_file() { std::filesystem::remove(path); }
};

PUI_TEST(showcase_trace_keeps_initial_and_latest_frames_in_order)
{
    trace_test_file output;
    showcase_frame_trace trace;
    CHECK(trace.start({.path = output.path.c_str(), .capacity = 8, .initial = 2}));
    showcase state;
    example_app app;
    trace.observe(state, app, 0); // establish the first boundary
    for (i32 i = 0; i < 20; ++i)
    {
        state.page = i % PAGE_COUNT;
        state.scroll_target[PAGE_LAYOUT] = static_cast<f32>(i * 7);
        state.build_ms = static_cast<f32>(i) * 0.25f;
        app.width = 640 + i;
        app.height = 480;
        trace.observe(state, app, static_cast<u64>(i + 1) * SDL_GetPerformanceFrequency());
    }
    CHECK(trace.count == 20);
    CHECK(trace.finish());
    std::ifstream file(output.path);
    std::string line;
    CHECK(static_cast<bool>(std::getline(file, line)));
    CHECK(line.starts_with("frame,loop_ms,build_ms,width,height,render_scale,native"));
    CHECK(line.find("scroll_layout") != std::string::npos);
    for (i32 frame : {0, 1, 14, 15, 16, 17, 18, 19})
    {
        CHECK(static_cast<bool>(std::getline(file, line)));
        const std::string prefix = std::to_string(frame) + ",1000,";
        CHECK(line.starts_with(prefix));
        const usize index =
            frame < 2 ? static_cast<usize>(frame) : 2 + static_cast<usize>((frame - 2) % 6);
        CHECK(trace.rows[index].page == frame % PAGE_COUNT);
        CHECK(trace.rows[index].scroll_layout == frame * 7);
        CHECK(trace.rows[index].width == 640 + frame);
        CHECK(trace.rows[index].build_ms == frame * 0.25);
    }
    CHECK(!std::getline(file, line));
}

PUI_TEST(showcase_trace_observation_does_not_allocate_or_write_the_file)
{
    trace_test_file output;
    showcase_frame_trace trace;
    CHECK(trace.start({.path = output.path.c_str(), .capacity = 8, .initial = 2}));
    showcase state;
    example_app app;
    const auto *storage = trace.rows.data();
    allocations = 0;
    track_allocations = true;
    for (u64 i = 0; i < 10000; ++i) trace.observe(state, app, i * 1000);
    track_allocations = false;
    CHECK(allocations == 0);
    CHECK(trace.rows.data() == storage && trace.rows.size() == 8);
    CHECK(std::filesystem::file_size(output.path) == 0);
    CHECK(trace.finish());
}

PUI_TEST(showcase_trace_preserves_existing_files_and_handles_empty_recordings)
{
    trace_test_file output;
    {
        std::ofstream file(output.path);
        file << "existing recording\n";
    }
    showcase_frame_trace trace;
    CHECK(!trace.start({.path = output.path.c_str(), .capacity = 4, .initial = 1}));
    {
        std::ifstream file(output.path);
        std::string line;
        CHECK(static_cast<bool>(std::getline(file, line)) && line == "existing recording");
    }
    CHECK(std::filesystem::remove(output.path));
    CHECK(!trace.start({.path = output.path.c_str(), .capacity = 4, .initial = 4}));
    CHECK(!std::filesystem::exists(output.path));
    CHECK(trace.start({.path = output.path.c_str(), .capacity = 4, .initial = 1}));
    CHECK(trace.finish()); // --help or a failed initialization can have zero frames
    CHECK(std::filesystem::file_size(output.path) > 0);
    CHECK(!trace.finish());
    CHECK(std::filesystem::remove(output.path));
    CHECK(trace.start({.path = output.path.c_str(), .capacity = 4, .initial = 1}));
    CHECK(trace.count == 0 && !trace.have_previous);
    CHECK(trace.finish());
}

PUI_TEST(showcase_trace_marks_available_loop_stages_and_keeps_unavailable_fields_explicit)
{
    trace_test_file output;
    showcase_frame_trace trace;
    CHECK(trace.start({.path = output.path.c_str(), .capacity = 8, .initial = 2}));
    showcase state;
    example_app app;
    app.diagnostics.enabled = true;
    app.diagnostics.ticks[example_frame_diagnostics::PUMP] = 123;
    app.diagnostics.ticks[example_frame_diagnostics::BEGIN] = 456;
    app.diagnostics.ticks[example_frame_diagnostics::END] = 789;
    trace.observe(state, app, 0);
    trace.observe(state, app, 1000);
    CHECK(!trace.rows[0].stages_valid && !trace.rows[0].window_flags_valid);
    for (auto &valid : app.diagnostics.valid) valid = true;
    trace.observe(state, app, 2000);
    const auto &row = trace.rows[1];
    const f64 ms_per_tick = 1000.0 / static_cast<f64>(SDL_GetPerformanceFrequency());
    CHECK(row.stages_valid && row.pump_ms == 123 * ms_per_tick);
    CHECK(row.begin_ms == 456 * ms_per_tick && row.end_ms == 789 * ms_per_tick);
    CHECK(!row.window_flags_valid); // no window; zero is not proof of hidden/unfocused
#if defined(_WIN32)
    CHECK(row.thread_valid && row.thread_cycles > 0);
#else
    CHECK(!row.thread_valid);
#endif
    CHECK(trace.finish());
    std::ifstream file(output.path);
    std::string header;
    CHECK(static_cast<bool>(std::getline(file, header)));
    CHECK(header.find("trace_schema,stages_valid,pump_ms,begin_ms,end_ms") != std::string::npos);
    CHECK(header.find("window_flags_valid,focused,visible,validation_requested") !=
          std::string::npos);
}

PUI_TEST(example_stage_diagnostics_preserve_callbacks_with_collection_disabled_and_enabled)
{
    example_frame_diagnostics timing;
    i32 calls = 0;
    timing.ticks[example_frame_diagnostics::PUMP] = 123;
    timing.measure(example_frame_diagnostics::PUMP, [&] { ++calls; });
    CHECK(calls == 1 && timing.ticks[example_frame_diagnostics::PUMP] == 123);
    CHECK(!timing.valid[example_frame_diagnostics::PUMP]);
    timing.enabled = true;
    timing.measure(example_frame_diagnostics::PUMP, [&] { ++calls; });
    CHECK(calls == 2 && timing.valid[example_frame_diagnostics::PUMP]);
    CHECK(!timing.valid[example_frame_diagnostics::BEGIN]);
    CHECK(!timing.valid[example_frame_diagnostics::END]);
}
} // namespace
