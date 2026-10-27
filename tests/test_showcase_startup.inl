PUI_TEST(example_environment_flags_respect_explicit_false_values)
{
    CHECK(!example_environment_flag(nullptr));
    for (const char *value : {"", "0", "false", "FALSE", "fAlSe", "off", "OFF", "no", "NO"})
        CHECK(!example_environment_flag(value));
    for (const char *value : {"1", "true", "TRUE", "yes", "on", "debug"})
        CHECK(example_environment_flag(value));
}

PUI_TEST(showcase_layout_startup_is_explicit_and_preserves_defaults)
{
    showcase_layout_opts options;
    const char *defaults[] = {"showcase", "--selftest", "--frames", "12"};
    CHECK(options.parse(static_cast<i32>(std::size(defaults)), defaults));
    CHECK(options.tiles == 14 && options.mode == 0 && options.size == 112);
    const char *masonry[] = {"showcase", "--tiles", "500", "--lab-mode", "2", "--lab-size", "64"};
    CHECK(options.parse(7, masonry));
    showcase state;
    seed(state);
    options.apply(state);
    CHECK(state.tiles.size() == 500 && state.lab_mode == 2 && state.lab_size == 64);
    options.apply(state);
    CHECK(state.tiles.size() == 500);
}

PUI_TEST(showcase_layout_startup_rejects_invalid_inputs_without_partial_updates)
{
    for (const char *name : {"--tiles", "--lab-mode", "--lab-size"})
        for (const char *value : {"", "oops", "1x", "-1", "1e30", "nan", "inf"})
        {
            showcase_layout_opts options;
            const char *args[] = {"showcase", name, value};
            CHECK(!options.parse(3, args));
            CHECK(options.tiles == 14 && options.mode == 0 && options.size == 112);
        }
    showcase_layout_opts options;
    const char *missing[] = {"showcase", "--tiles"};
    CHECK(!options.parse(2, missing));
    const char *partial[] = {"showcase", "--lab-mode", "2", "--tiles", "5001"};
    CHECK(!options.parse(5, partial));
    CHECK(options.tiles == 14 && options.mode == 0 && options.size == 112);
}
