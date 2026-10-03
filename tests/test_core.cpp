// test_core.cpp - the runner (r88 split): registers every test from the
// per-area files, supports `--list` and `--filter NAME` (substring), and
// keeps the suite contract: unfiltered + zero failures prints
// "all core tests passed" and exits 0.
#include "test_util.h"
#include <cstring>

int main(int argc, char **argv)
{
    const char *filter = nullptr;
    bool list_only = false;
    for (i32 i = 1; i < argc; ++i)
    {
        if (std::strncmp(argv[i], "--filter", 8) == 0)
        {
            if (argv[i][8] == '=')
                filter = argv[i] + 9;
            else if (i + 1 < argc)
                filter = argv[++i];
        }
        else if (std::strcmp(argv[i], "--list") == 0)
            list_only = true;
    }

    i32 run = 0;
    const std::vector<test_entry> &TESTS = test_registry();
    for (const auto &t : TESTS)
    {
        if (filter && (!t.name || !std::strstr(t.name, filter))) continue;
        if (list_only)
        {
            std::printf("%s\n", t.name);
            continue;
        }
        t.fn();
        ++run;
    }
    if (list_only) return 0;

    if (g_failures == 0)
    {
        if (run == static_cast<i32>(TESTS.size()))
            std::printf("all core tests passed\n");
        else
            std::printf("%d test(s) passed (filtered)\n", run);
        return 0;
    }
    std::printf("%d core test(s) failed\n", g_failures);
    return 1;
}