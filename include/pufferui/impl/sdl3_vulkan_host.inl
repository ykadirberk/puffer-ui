// SDL window/input policy is shared with the existing SDL_Renderer backend.
// This adapter supplies native Vulkan surfaces; it does not create a renderer.
// Vulkan remains optional and never changes the SDL-only default.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_SDL3) && defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
struct sdl_host : vulkan_host
{
    PFN_vkGetInstanceProcAddr instance_proc_addr() override
    {
        return reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    }
    SDL_Cursor *cursors[5]{};
    ~sdl_host() override
    {
        for (auto *c : cursors)
            if (c) SDL_DestroyCursor(c);
    }
    std::span<const char *const> instance_extensions() override
    {
        Uint32 count = 0;
        const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
        return {names, count};
    }
    VkSurfaceKHR create_surface(VkInstance instance, void *window) override
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window *>(window), instance, nullptr,
                                      &surface))
            return VK_NULL_HANDLE;
        return surface;
    }
    void output_size(void *window, i32 &w, i32 &h) override
    {
        auto *sdl = static_cast<SDL_Window *>(window);
        if (SDL_GetWindowFlags(sdl) & SDL_WINDOW_MINIMIZED)
        {
            w = h = 0;
            return;
        }
        SDL_GetWindowSizeInPixels(sdl, &w, &h);
    }
    void set_cursor(cursor c) override
    {
        const i32 i = static_cast<i32>(c);
        if (i < 0 || i >= 5) return;
        if (!cursors[i])
        {
            SDL_SystemCursor kind = SDL_SYSTEM_CURSOR_DEFAULT;
            if (c == cursor::IBEAM)
                kind = SDL_SYSTEM_CURSOR_TEXT;
            else if (c == cursor::HAND)
                kind = SDL_SYSTEM_CURSOR_POINTER;
            else if (c == cursor::HRESIZE)
                kind = SDL_SYSTEM_CURSOR_EW_RESIZE;
            else if (c == cursor::VRESIZE)
                kind = SDL_SYSTEM_CURSOR_NS_RESIZE;
            cursors[i] = SDL_CreateSystemCursor(kind);
        }
        if (cursors[i]) SDL_SetCursor(cursors[i]);
    }
};
} // namespace vk_detail
render_device *create_sdl3_vulkan_device(const vulkan_device_opts &opts)
{
    auto *host = new vk_detail::sdl_host();
    vulkan_device_opts copy = opts;
    copy.host = host;
    render_device *base = create_vulkan_device(copy);
    if (base)
        static_cast<vk_detail::device *>(base)->owned_host = host;
    else
        delete host;
    return base;
}
#else
render_device *create_sdl3_vulkan_device(const vulkan_device_opts &)
{
    return nullptr;
}
#endif
bool sdl3_vulkan_app_init(sdl3_app &app, const sdl3_vulkan_app_opts &opts)
{
#if defined(PUFFERUI_ENABLE_SDL3) && defined(PUFFERUI_ENABLE_VULKAN)
    app.use_vulkan = true;
    app.vulkan = opts.device;
    return sdl3_app_init(app, opts.title, opts.width, opts.height, 0, nullptr);
#else
    (void)app;
    (void)opts;
    return false;
#endif
}
