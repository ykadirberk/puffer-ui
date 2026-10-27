// An invisible vector label must not shape, prepare new outlines or evict a
// visible run before the renderer's established vertical rejection.
#if defined(PUFFERUI_ENABLE_HARFBUZZ)
PUI_TEST(vector_clipped_labels_do_not_displace_visible_runs_or_prepare_outlines)
{
    auto options = preparation_options(false);
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.vector_text = true;
    prepared_scene scene(options);
    if (!scene.ctx) return;
    const auto font = load_font(scene.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    auto theme = default_dark();
    theme.font = font;
    theme.text_size = 16;
    set_theme(scene.ctx, theme);
    begin_frame(scene.ctx, *scene.win, 0, 1.0 / 60.0);
    {
        ui u(scene.ctx);
        constexpr std::string_view visible = "Home ffi 123";
        u.text({4, 4, 124, 24}, visible, color::white());
        auto &cache = scene.ctx->ts->layout_cache;
        const u64 visible_key = detail::layout_key(scene.ctx, font, 16, visible);
        CHECK(cache.find(visible_key));
        char label[64]{};
        u64 key = 0;
        for (i32 i = 0; i < 131072; ++i)
        {
            std::snprintf(label, sizeof(label), "Offscreen WXYZ label %d", i);
            key = detail::layout_key(scene.ctx, font, 16, label);
            if (&cache.slot_for(key) == &cache.slot_for(visible_key)) break;
        }
        CHECK(&cache.slot_for(key) == &cache.slot_for(visible_key));
        auto &font_state = *scene.ctx->ts->fonts[static_cast<usize>(font)].harfbuzz;
        const usize outlines = font_state.outlines.size();
        u.text({4, 10000, 124, 24}, label, color::white());
        CHECK(cache.find(visible_key) && !cache.find(key));
        CHECK(font_state.outlines.size() == outlines);
        {
            auto empty = u.region({0, 0, 0, 0}, "empty_vector_clip"_id);
            u.text({4, 4, 124, 24}, label, color::white());
        }
        CHECK(cache.find(visible_key) && !cache.find(key));
    }
    end_frame(scene.ctx);
    CHECK(!violation_count(scene.ctx));
}

PUI_TEST(vector_custom_shapers_keep_their_clipped_run_invocation_contract)
{
    struct counting_shaper final : text_shaper
    {
        i32 calls = 0;
        bool shape(context *, font_handle, f32, std::string_view,
                   function_ref<void(const shaped_glyph &)> emit) override
        {
            ++calls;
            emit({'A', 0});
            return true;
        }
    } shaper;
    auto options = preparation_options(false);
    options.painter = vulkan_painter_mode::SDF_RASTER;
    options.vector_text = true;
    prepared_scene scene(options);
    if (!scene.ctx) return;
    auto theme = default_dark();
    theme.font = load_font(scene.ctx, PUFFERUI_ASSET_DIR "/fonts/DejaVuSans.ttf");
    set_theme(scene.ctx, theme);
    set_text_shaper(scene.ctx, &shaper);
    begin_frame(scene.ctx, *scene.win, 0, 1.0 / 60.0);
    {
        ui u(scene.ctx);
        u.text({4, 10000, 124, 24}, "clipped custom run", color::white());
        CHECK(shaper.calls == 1);
        u.text({4, 4, 124, 24}, "clipped custom run", color::white());
        CHECK(shaper.calls == 1); // the same primary cache hit as before
    }
    end_frame(scene.ctx);
    CHECK(!violation_count(scene.ctx));
    set_text_shaper(scene.ctx, nullptr);
}
#endif
