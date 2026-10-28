// Optional bounded diagnostics for the actual showcase. Include after showcase.
#pragma once
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma push_macro("near")
#pragma push_macro("far")
#pragma push_macro("small")
#include <windows.h>
#pragma pop_macro("small")
#pragma pop_macro("far")
#pragma pop_macro("near")
#endif

#define PUI_SHOWCASE_TRACE_MODEL(X)                                                                \
    X(page, state.page);                                                                           \
    X(tile_count, state.tiles.size());                                                             \
    X(card_count, state.cards.size());                                                             \
    X(particle_count, state.particles.size());                                                     \
    X(pulse_count, state.pulses.size());                                                           \
    X(ripple_count, state.ripples.size());                                                         \
    X(theme_mode, state.theme_mode);                                                               \
    X(density, state.density);                                                                     \
    X(accent, state.accent);                                                                       \
    X(ambient, state.ambient);                                                                     \
    X(reduced_motion, state.reduced_motion);                                                       \
    X(sidebar_open, state.sidebar_open);                                                           \
    X(drawer_open, state.drawer_open);                                                             \
    X(palette_open, state.palette_open);                                                           \
    X(notes_open, state.notes_open);                                                               \
    X(transition, state.transition);                                                               \
    X(lab_mode, state.lab_mode);                                                                   \
    X(lab_size, state.lab_size);                                                                   \
    X(lab_gap, state.lab_gap);                                                                     \
    X(lab_selected, state.lab_selected);                                                           \
    X(radial_open, state.radial_open);                                                             \
    X(lens_blur, state.lens_blur);                                                                 \
    X(lens_x, state.lens.x);                                                                       \
    X(lens_y, state.lens.y);                                                                       \
    X(loaded, state.loaded);                                                                       \
    X(stiffness, state.stiff);                                                                     \
    X(damping, state.damp);                                                                        \
    X(mass, state.mass);                                                                           \
    X(preset, state.preset);                                                                       \
    X(bar_range, state.bar_range);                                                                 \
    X(chart_range, state.chart_range);                                                             \
    X(vsync, state.vsync);                                                                         \
    X(vsync_applied, state.vsync_applied);                                                         \
    X(hud_compact, state.hud_compact);                                                             \
    X(scroll_overview, state.scroll_target[0]);                                                    \
    X(scroll_layout, state.scroll_target[1]);                                                      \
    X(scroll_board, state.scroll_target[2]);                                                       \
    X(scroll_effects, state.scroll_target[3]);                                                     \
    X(scroll_motion, state.scroll_target[4]);

#define PUI_SHOWCASE_TRACE_COUNTERS(X)                                                             \
    X(requested_draws);                                                                            \
    X(requested_vertices);                                                                         \
    X(requested_indices);                                                                          \
    X(draws);                                                                                      \
    X(upload_bytes);                                                                               \
    X(glyph_upload_bytes);                                                                         \
    X(glyph_resident_batches);                                                                     \
    X(gpu_sdf_batches);                                                                            \
    X(gpu_sdf_pixels);                                                                             \
    X(whole_program_hits);                                                                         \
    X(whole_program_probes);                                                                       \
    X(gpu_samples);                                                                                \
    X(completed_gpu_ms);                                                                           \
    X(slot_wait_ms);                                                                               \
    X(acquire_ms);                                                                                 \
    X(present_ms);                                                                                 \
    X(submit_ms);                                                                                  \
    X(draw_memory_allocations);                                                                    \
    X(draw_resource_creations);
#define PUI_SHOWCASE_TRACE_GAUGES(X)                                                               \
    X(live_textures);                                                                              \
    X(image_bytes);                                                                                \
    X(descriptor_sets);                                                                            \
    X(upload_allocated_bytes);                                                                     \
    X(gpu_metadata_allocated_bytes);                                                               \
    X(retired_bytes);

struct showcase_trace_row
{
    u64 frame = 0;
    f64 loop_ms = 0, build_ms = 0;
    i32 width = 0, height = 0;
    f32 render_scale = 1;
    bool native = false;
    i32 painter = -1, present_mode = -1;
    f64 pump_ms = 0, begin_ms = 0, end_ms = 0;
    f64 thread_cpu_ms = 0, thread_cycles = 0;
    bool stages_valid = false, thread_valid = false, window_flags_valid = false;
    bool focused = false, visible = false, validation_requested = false;
#define PUI_TRACE_MODEL_FIELD(name, value) f64 name = 0
    PUI_SHOWCASE_TRACE_MODEL(PUI_TRACE_MODEL_FIELD)
#undef PUI_TRACE_MODEL_FIELD
#define PUI_TRACE_METRIC_FIELD(name) f64 name = 0
    PUI_SHOWCASE_TRACE_COUNTERS(PUI_TRACE_METRIC_FIELD)
    PUI_SHOWCASE_TRACE_GAUGES(PUI_TRACE_METRIC_FIELD)
#undef PUI_TRACE_METRIC_FIELD
};

struct showcase_trace_options
{
    const char *path = nullptr;
    usize capacity = 65536, initial = 2048;
};

struct showcase_frame_trace
{
    FILE *file = nullptr; // owned; opened exclusively before the UI starts
    std::vector<showcase_trace_row> rows;
    usize initial = 0;
    u64 count = 0, ticks_before = 0;
    bool have_previous = false;
    u64 previous_thread_ticks = 0, previous_thread_cycles = 0;
    bool previous_thread_valid = false;
#if defined(PUFFERUI_ENABLE_VULKAN)
    vulkan_stats previous{};
    render_device *previous_device = nullptr; // borrowed
#endif
    ~showcase_frame_trace()
    {
        if (file) std::fclose(file);
    }
    bool start(const showcase_trace_options &options)
    {
        if (file || !options.path || options.capacity <= options.initial) return false;
        file = std::fopen(options.path, "wx"); // preserve any previous recording
        if (!file) return false;
        rows.resize(options.capacity); // all storage is prepared before example_run
        initial = options.initial;
        count = ticks_before = 0;
        have_previous = false;
        previous_thread_valid = false;
        previous_thread_ticks = previous_thread_cycles = 0;
#if defined(PUFFERUI_ENABLE_VULKAN)
        previous = {};
        previous_device = nullptr;
#endif
        return true;
    }
    void observe(const showcase &state, const example_app &app, u64 ticks)
    {
        if (!file) return;
        u64 thread_ticks = 0, thread_cycles = 0;
        bool thread_valid = false;
#if defined(_WIN32)
        FILETIME creation{}, exit{}, kernel{}, user{};
        thread_valid = GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user) &&
                       QueryThreadCycleTime(GetCurrentThread(), &thread_cycles);
        thread_ticks = (static_cast<u64>(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime +
                       (static_cast<u64>(user.dwHighDateTime) << 32) + user.dwLowDateTime;
#endif
#if defined(PUFFERUI_ENABLE_VULKAN)
        const bool native = is_vulkan_device(app.device);
        const auto metrics = native ? vulkan_statistics(app.device) : vulkan_stats{};
#endif
        if (have_previous)
        {
            const usize index =
                count < initial
                    ? static_cast<usize>(count)
                    : initial + static_cast<usize>((count - initial) % (rows.size() - initial));
            auto &row = rows[index];
            row = {};
            row.frame = count++;
            row.loop_ms = static_cast<f64>(ticks - ticks_before) * 1000.0 /
                          static_cast<f64>(SDL_GetPerformanceFrequency());
            row.build_ms = state.build_ms;
            row.width = app.width;
            row.height = app.height;
            row.render_scale = app.ctx ? app.ctx->render_scale : 1;
            const auto &timing = app.diagnostics;
            row.stages_valid = timing.enabled;
            for (bool valid : timing.valid) row.stages_valid &= valid;
            const f64 ms_per_tick = 1000.0 / static_cast<f64>(SDL_GetPerformanceFrequency());
            row.pump_ms =
                static_cast<f64>(timing.ticks[example_frame_diagnostics::PUMP]) * ms_per_tick;
            row.begin_ms =
                static_cast<f64>(timing.ticks[example_frame_diagnostics::BEGIN]) * ms_per_tick;
            row.end_ms =
                static_cast<f64>(timing.ticks[example_frame_diagnostics::END]) * ms_per_tick;
            row.thread_valid = thread_valid && previous_thread_valid;
            if (row.thread_valid)
            {
                row.thread_cpu_ms =
                    static_cast<f64>(thread_ticks - previous_thread_ticks) / 10000.0;
                row.thread_cycles = static_cast<f64>(thread_cycles - previous_thread_cycles);
            }
            if (app.sdl_window)
            {
                // SDL maintains these flags in its window state; this does not
                // query the OS or modify focus/window policy.
                const auto flags = SDL_GetWindowFlags(app.sdl_window);
                row.window_flags_valid = true;
                row.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
                row.visible = (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0;
            }
#define PUI_TRACE_MODEL_CAPTURE(name, value) row.name = static_cast<f64>(value)
            PUI_SHOWCASE_TRACE_MODEL(PUI_TRACE_MODEL_CAPTURE)
#undef PUI_TRACE_MODEL_CAPTURE
#if defined(PUFFERUI_ENABLE_VULKAN)
            row.native = native;
            row.validation_requested = app.boot.use_vulkan && app.boot.vulkan.validation;
            if (native)
            {
                row.painter = static_cast<i32>(vulkan_painter(app.device));
                row.present_mode = static_cast<i32>(vulkan_present_mode(app.surface));
                if (previous_device == app.device)
                {
#define PUI_TRACE_COUNTER_CAPTURE(name)                                                            \
    row.name = static_cast<f64>(metrics.name) - static_cast<f64>(previous.name)
                    PUI_SHOWCASE_TRACE_COUNTERS(PUI_TRACE_COUNTER_CAPTURE)
#undef PUI_TRACE_COUNTER_CAPTURE
                }
#define PUI_TRACE_GAUGE_CAPTURE(name) row.name = static_cast<f64>(metrics.name)
                PUI_SHOWCASE_TRACE_GAUGES(PUI_TRACE_GAUGE_CAPTURE)
#undef PUI_TRACE_GAUGE_CAPTURE
            }
#endif
        }
        ticks_before = ticks;
        have_previous = true;
        previous_thread_ticks = thread_ticks;
        previous_thread_cycles = thread_cycles;
        previous_thread_valid = thread_valid;
#if defined(PUFFERUI_ENABLE_VULKAN)
        previous = metrics;
        previous_device = app.device;
#endif
    }
    bool finish()
    {
        if (!file) return false;
        std::fputs("frame,loop_ms,build_ms,width,height,render_scale,native,painter,present_mode",
                   file);
#define PUI_TRACE_MODEL_HEADER(name, value) std::fputs("," #name, file)
        PUI_SHOWCASE_TRACE_MODEL(PUI_TRACE_MODEL_HEADER)
#undef PUI_TRACE_MODEL_HEADER
#define PUI_TRACE_METRIC_HEADER(name) std::fputs("," #name, file)
        PUI_SHOWCASE_TRACE_COUNTERS(PUI_TRACE_METRIC_HEADER)
        PUI_SHOWCASE_TRACE_GAUGES(PUI_TRACE_METRIC_HEADER)
#undef PUI_TRACE_METRIC_HEADER
        std::fputs(",trace_schema,stages_valid,pump_ms,begin_ms,end_ms,thread_valid,thread_cpu_ms,"
                   "thread_cycles,window_flags_valid,focused,visible,validation_requested\n",
                   file);
        const auto write = [&](const showcase_trace_row &row)
        {
            std::fprintf(file, "%llu,%.9g,%.9g,%d,%d,%.9g,%d,%d,%d",
                         static_cast<unsigned long long>(row.frame), row.loop_ms, row.build_ms,
                         row.width, row.height, static_cast<double>(row.render_scale), row.native,
                         row.painter, row.present_mode);
#define PUI_TRACE_MODEL_VALUE(name, value) std::fprintf(file, ",%.9g", row.name)
            PUI_SHOWCASE_TRACE_MODEL(PUI_TRACE_MODEL_VALUE)
#undef PUI_TRACE_MODEL_VALUE
#define PUI_TRACE_METRIC_VALUE(name) std::fprintf(file, ",%.9g", row.name)
            PUI_SHOWCASE_TRACE_COUNTERS(PUI_TRACE_METRIC_VALUE)
            PUI_SHOWCASE_TRACE_GAUGES(PUI_TRACE_METRIC_VALUE)
#undef PUI_TRACE_METRIC_VALUE
            std::fprintf(file, ",2,%d,%.9g,%.9g,%.9g,%d,%.9g,%.9g,%d,%d,%d,%d\n", row.stages_valid,
                         row.pump_ms, row.begin_ms, row.end_ms, row.thread_valid, row.thread_cpu_ms,
                         row.thread_cycles, row.window_flags_valid, row.focused, row.visible,
                         row.validation_requested);
        };
        const u64 head = std::min<u64>(count, initial);
        for (u64 i = 0; i < head; ++i) write(rows[static_cast<usize>(i)]);
        const u64 tail = std::min<u64>(count - head, rows.size() - initial);
        for (u64 i = count - tail; i < count; ++i)
            write(rows[initial + static_cast<usize>((i - initial) % (rows.size() - initial))]);
        const bool okay = !std::ferror(file);
        const bool closed = std::fclose(file) == 0;
        file = nullptr;
        std::fprintf(stderr, "showcase trace rows=%llu dropped=%llu final_frame_unobserved=%d\n",
                     static_cast<unsigned long long>(head + tail),
                     static_cast<unsigned long long>(count - head - tail), have_previous);
        return okay && closed;
    }
};

#undef PUI_SHOWCASE_TRACE_MODEL
#undef PUI_SHOWCASE_TRACE_COUNTERS
#undef PUI_SHOWCASE_TRACE_GAUGES
