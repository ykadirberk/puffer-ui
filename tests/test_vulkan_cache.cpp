#include <pufferui/vulkan.h>
#include <cstdio>
#include <vector>
using namespace pui;
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static i32 errors = 0;
static void diagnostic(i32 code, const char *text, void *)
{
    std::fprintf(stderr, "Vulkan %d: %s\n", code, text);
    ++errors;
}
static void quad(render_device *d, texture_handle texture, rect r, color c)
{
    const vertex v[] = {{r.x, r.y, 0, 0, c},
                        {r.right(), r.y, 1, 0, c},
                        {r.right(), r.bottom(), 1, 1, c},
                        {r.x, r.bottom(), 0, 1, c}};
    const i32 idx[] = {0, 1, 2, 0, 2, 3};
    d->draw(texture, v, 4, idx, 6);
}
int main()
{
    vulkan_device_opts opts;
    opts.validation = true;
    opts.error_callback = diagnostic;
    render_device *d = create_vulkan_device(opts);
    REQUIRE(d);
    render_surface *s[2] = {d->create_surface(), d->create_surface()};
    REQUIRE(s[0] && s[1]);
    const u8 blue[] = {0, 0, 255, 255}, green[] = {0, 255, 0, 255};
    texture_handle picture[2] = {d->create_texture(1, 1, blue), d->create_texture(1, 1, blue)};
    texture_handle half[2] = {d->create_target(64, 64), d->create_target(64, 64)};
    std::vector<u8> pixels[2];
    for (auto &p : pixels) p.resize(640 * 480 * 4);
    for (i32 f = 0; f < 40; ++f)
    {
        for (i32 variant = 0; variant < 2; ++variant)
        {
            vulkan_set_cache(d, variant == 1);
            s[variant]->make_current(*d);
            if (f < 30 || f >= 35)
                d->clear(f == 9 ? color{40, 20, 10, 255} : color{10, 20, 40, 255});
            const float move = f >= 2 && f < 7 ? 12.25f : 0;
            quad(d, nullptr, rect::make(20 + move, 20, 35, 27), {255, 0, 0, 150});
            if (f != 5) quad(d, picture[variant], rect::make(28, 24, 25, 19), {255, 255, 255, 190});
            if (f == 12)
            {
                u8 prefix[4]{};
                REQUIRE(d->read_pixels(s[variant]->scene_target(), 22, 22, 1, 1, prefix));
            }
            if (f == 13) d->update_texture(picture[variant], 0, 0, 1, 1, green);
            if (f == 14) d->set_texture_filter(picture[variant], true);
            if (f >= 20 && f < 25)
            {
                // Read an early prefix, then write the scene again: the cache
                // must never substitute last frame's final scene for this read.
                const rect source = rect::make(0, 0, 128, 128);
                d->blit(s[variant]->scene_target(), &source, half[variant], nullptr);
                d->set_target(s[variant]->scene_target());
                quad(d, half[variant], rect::make(120, 20, 90, 90), {255, 255, 255, 200});
            }
            quad(d, nullptr, rect::make(200, 200, 50, 30), {0, 255, 40, 128});
            s[variant]->present();
            REQUIRE(
                d->read_pixels(s[variant]->scene_target(), 0, 0, 640, 480, pixels[variant].data()));
            REQUIRE(vulkan_last_result(d) == VK_SUCCESS);
        }
        REQUIRE(pixels[0] == pixels[1]);
    }
    REQUIRE(vulkan_statistics(d).cache_hits > 0);
    REQUIRE(vulkan_statistics(d).cache_misses > 0);
    // Repeated large full misses must back off to the complete reference path,
    // rather than continually serializing a giant changing draw. Unused input
    // vertices are legal and deliberately give this draw full-scene bounds.
    std::vector<vertex> large(50000);
    const i32 triangle[] = {0, 1, 2};
    const u64 bypasses = vulkan_statistics(d).cache_bypasses;
    for (i32 f = 0; f < 3; ++f)
    {
        for (auto &v : large) v = {0, 0, 0, 0, {static_cast<u8>(160 + f), 20, 30, 255}};
        large[0].x = large[0].y = 10;
        large[1].x = 40;
        large[1].y = 10;
        large[2].x = 10;
        large[2].y = 40;
        large.back().x = 640;
        large.back().y = 480;
        for (i32 variant = 0; variant < 2; ++variant)
        {
            vulkan_set_cache(d, variant == 1);
            s[variant]->make_current(*d);
            d->clear({10, 20, 40, 255});
            d->draw(nullptr, large.data(), static_cast<i32>(large.size()), triangle, 3);
            s[variant]->present();
            REQUIRE(
                d->read_pixels(s[variant]->scene_target(), 0, 0, 640, 480, pixels[variant].data()));
        }
        REQUIRE(pixels[0] == pixels[1]);
    }
    REQUIRE(vulkan_statistics(d).cache_bypasses > bypasses);
    REQUIRE(vulkan_statistics(d).peak_cache_metadata_bytes <= opts.cache_budget_bytes);
    REQUIRE(vulkan_statistics(d).peak_upload_allocated_bytes <= opts.upload_budget_bytes);
    for (i32 i = 0; i < 2; ++i)
    {
        d->destroy_texture(picture[i]);
        d->destroy_target(half[i]);
        d->destroy_surface(s[i]);
    }
    REQUIRE(vulkan_wait_idle(d));
    REQUIRE(vulkan_statistics(d).validation_errors == 0);
    destroy_vulkan_device(d);
    REQUIRE(errors == 0);
    std::puts("all 43 Vulkan cache/reference frames matched exactly");
    return 0;
}
