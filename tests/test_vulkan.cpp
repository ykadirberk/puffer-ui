#include <pufferui/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace pui;
static i32 errors = 0;
static void diagnostic(i32 code, const char *message, void *)
{
    std::fprintf(stderr, "Vulkan diagnostic %d: %s\n", code, message);
    ++errors;
}
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                      \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void quad(render_device *d, texture_handle texture, rect r, color c)
{
    const vertex v[] = {{r.x, r.y, 0, 0, c},
                        {r.right(), r.y, 1, 0, c},
                        {r.right(), r.bottom(), 1, 1, c},
                        {r.x, r.bottom(), 0, 1, c}};
    const i32 idx[] = {0, 1, 2, 0, 2, 3};
    d->draw(texture, v, 4, idx, 6);
}
int run(bool staged)
{
    vulkan_device_opts opts;
    opts.validation = true;
    opts.upload_mode = staged ? vulkan_upload_mode::STAGED : vulkan_upload_mode::DIRECT;
    opts.error_callback = diagnostic;
    render_device *d = create_vulkan_device(opts);
    REQUIRE(d);
    REQUIRE(is_vulkan_device(d));
    const auto handles = vulkan_handles(d);
    std::printf("Vulkan device initialized, queue family %u\n", handles.queue_family);
    texture_handle target = d->create_target(64, 64);
    REQUIRE(target);
    d->set_target(target);
    d->clear({10, 20, 30, 255});
    quad(d, nullptr, rect::make(8, 8, 16, 16), {255, 0, 0, 255});
    u8 pixel[4]{};
    REQUIRE(d->read_pixels(target, 12, 12, 1, 1, pixel));
    REQUIRE(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255);
    REQUIRE(d->read_pixels(target, 0, 0, 1, 1, pixel));
    REQUIRE(pixel[0] == 10 && pixel[1] == 20 && pixel[2] == 30 && pixel[3] == 255);
    const u8 blue[4] = {0, 0, 255, 255}, green[4] = {0, 255, 0, 255};
    texture_handle image = d->create_texture(1, 1, blue);
    REQUIRE(image);
    d->set_target(target);
    quad(d, image, rect::make(0, 0, 16, 16), {255, 255, 255, 255});
    d->update_texture(image, 0, 0, 1, 1, green);
    quad(d, image, rect::make(16, 0, 16, 16), {255, 255, 255, 255});
    REQUIRE(d->read_pixels(target, 4, 4, 1, 1, pixel));
    REQUIRE(pixel[2] == 255 && pixel[1] == 0);
    REQUIRE(d->read_pixels(target, 20, 4, 1, 1, pixel));
    REQUIRE(pixel[1] == 255 && pixel[2] == 0);
    // Every byte channel survives the float-color upload and UNORM attachment.
    d->set_target(target);
    d->clear({0, 0, 0, 0});
    for (i32 n = 0; n < 256; ++n)
        quad(d, nullptr,
             rect::make(static_cast<float>(n % 16) * 4, static_cast<float>(n / 16) * 4, 4, 4),
             {static_cast<u8>(n), static_cast<u8>(255 - n), static_cast<u8>(n), 255});
    std::vector<u8> pixels(64 * 64 * 4);
    REQUIRE(d->read_pixels(target, 0, 0, 64, 64, pixels.data()));
    for (i32 n = 0; n < 256; ++n)
    {
        const usize at = static_cast<usize>((n / 16 * 4 + 2) * 64 + n % 16 * 4 + 2) * 4;
        REQUIRE(pixels[at] == n && pixels[at + 1] == 255 - n && pixels[at + 2] == n &&
                pixels[at + 3] == 255);
    }
    d->clear({0, 0, 0, 255});
    const rect clip = rect::make(-3.8f, -3.8f, 10.9f, 10.9f);
    d->set_clip(&clip);
    quad(d, nullptr, rect::make(0, 0, 32, 32), {0, 255, 0, 255});
    REQUIRE(d->read_pixels(target, 6, 6, 1, 1, pixel));
    REQUIRE(pixel[1] == 255);
    REQUIRE(d->read_pixels(target, 7, 7, 1, 1, pixel));
    REQUIRE(pixel[1] == 0);
    const rect empty = rect::make(0, 0, 0, 0);
    d->set_clip(&empty);
    quad(d, nullptr, rect::make(0, 0, 32, 32), {255, 0, 0, 255});
    REQUIRE(d->read_pixels(target, 6, 6, 1, 1, pixel));
    REQUIRE(pixel[0] == 0);
    d->set_clip(nullptr);
    const u8 translucent[] = {255, 0, 0, 128};
    texture_handle alpha = d->create_texture(1, 1, translucent);
    REQUIRE(alpha);
    d->blit(alpha, nullptr, target, nullptr);
    REQUIRE(d->read_pixels(target, 20, 20, 1, 1, pixel));
    REQUIRE(pixel[0] == 255 && pixel[3] == 128);
    // SDL views retain scissor per target: a scene clip must not clip blur's
    // small scratch targets, and selecting the scene restores its own clip.
    d->set_target(target);
    d->clear({0, 0, 0, 255});
    const rect corner = rect::make(60, 60, 4, 4);
    d->set_clip(&corner);
    texture_handle scratch = d->create_target(8, 8);
    REQUIRE(scratch);
    d->blit(alpha, nullptr, scratch, nullptr);
    REQUIRE(d->read_pixels(scratch, 0, 0, 1, 1, pixel));
    REQUIRE(pixel[0] == 255 && pixel[3] == 128);
    d->set_target(target);
    quad(d, nullptr, rect::make(0, 0, 64, 64), {0, 255, 0, 255});
    REQUIRE(d->read_pixels(target, 0, 0, 1, 1, pixel));
    REQUIRE(pixel[1] == 0);
    REQUIRE(d->read_pixels(target, 62, 62, 1, 1, pixel));
    REQUIRE(pixel[1] == 255);
    d->set_clip(nullptr);
    d->destroy_target(scratch);
    // Nearest and linear sampling remain distinct through both upload paths.
    const u8 pair[] = {255, 0, 0, 255, 0, 0, 255, 255};
    texture_handle sampled = d->create_texture(2, 1, pair);
    REQUIRE(sampled);
    d->set_target(target);
    d->clear({0, 0, 0, 255});
    d->set_texture_filter(sampled, false);
    quad(d, sampled, rect::make(0, 0, 4, 1), {255, 255, 255, 255});
    REQUIRE(d->read_pixels(target, 1, 0, 1, 1, pixel));
    REQUIRE(pixel[0] == 255 && pixel[2] == 0);
    d->set_texture_filter(sampled, true);
    quad(d, sampled, rect::make(0, 1, 4, 1), {255, 255, 255, 255});
    REQUIRE(d->read_pixels(target, 1, 1, 1, 1, pixel));
    REQUIRE(pixel[0] > 150 && pixel[0] < 220 && pixel[2] > 30 && pixel[2] < 100);
    // Copy has no blending: every alpha byte survives an RGBA8 round trip.
    std::vector<u8> channel_bytes(256 * 4);
    for (i32 n = 0; n < 256; ++n)
        for (i32 channel = 0; channel < 4; ++channel)
            channel_bytes[static_cast<usize>(n * 4 + channel)] = static_cast<u8>(n);
    texture_handle channels = d->create_texture(256, 1, channel_bytes.data());
    texture_handle channel_target = d->create_target(256, 1);
    REQUIRE(channels && channel_target);
    d->blit(channels, nullptr, channel_target, nullptr);
    std::vector<u8> channel_result(256 * 4);
    REQUIRE(d->read_pixels(channel_target, 0, 0, 256, 1, channel_result.data()));
    REQUIRE(channel_result == channel_bytes);
    d->destroy_target(channel_target);
    d->destroy_texture(channels);
    d->destroy_texture(sampled);
    d->destroy_texture(alpha);
    d->destroy_texture(image);
    d->destroy_target(target);
    REQUIRE(vulkan_wait_idle(d));
    REQUIRE(vulkan_statistics(d).validation_errors == 0);
    REQUIRE(vulkan_last_result(d) == VK_SUCCESS);
    destroy_vulkan_device(d);
    REQUIRE(errors == 0);
    // Device destruction must not destroy borrowed instance/device/queue.
    d = create_vulkan_device(opts);
    REQUIRE(d);
    const auto borrowed = vulkan_handles(d);
    vulkan_device_opts engine_opts;
    engine_opts.native = &borrowed;
    engine_opts.error_callback = diagnostic;
    render_device *engine = create_vulkan_device(engine_opts);
    REQUIRE(engine);
    texture_handle e = engine->create_target(8, 8);
    REQUIRE(e);
    engine->set_target(e);
    engine->clear({7, 8, 9, 255});
    REQUIRE(engine->read_pixels(e, 0, 0, 1, 1, pixel));
    REQUIRE(pixel[0] == 7 && pixel[1] == 8 && pixel[2] == 9);
    engine->destroy_target(e);
    destroy_vulkan_device(engine);
    REQUIRE(vulkan_wait_idle(d));
    destroy_vulkan_device(d);
    REQUIRE(errors == 0);
    std::puts("all Vulkan conformance tests passed");
    return 0;
}
int main()
{
    if (run(false)) return 1;
    return run(true);
}
