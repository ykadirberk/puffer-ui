// pufferui Vulkan implementation. Included inside namespace pui.
// One implementation TU; Vulkan headers live in the optional native header.
// The first four lines are retained as the slice banner for amalgamation.
// Device-owned state; borrowed Vulkan handles are never destroyed here.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
// Native swapchain images are first accessed by the final scene blit. Keep the
// acquire wait and its layout transition in the same dependency chain.
constexpr VkPipelineStageFlags2 PRESENT_BLIT_STAGE = VK_PIPELINE_STAGE_2_BLIT_BIT;
struct device;
struct sdf_gpu_job;
struct sdf_slot_resources;
struct rgba8_blend_lut;
struct native_function_cache;
struct primitive_recorder : paint_detail::primitive_sink
{
    device *owner = nullptr;
    paint_detail::command_tape tape;
    paint_detail::primitive_input stack[8]{};
    u32 depth = 0;
    bool raster_only = false;
    bool reported_overflow = false;
    u32 pending_shape = 0xffffffffu;
    void invalidate();
    void reset();
    void enter(const paint_detail::primitive_input &input) override;
    void leave() override;
    void mesh(texture_handle sample, const vertex *v, i32 vc, const i32 *idx, i32 ic) override;
    bool shape(context *c, const paint_detail::primitive_input &input) override;
    bool shape_room(context *c);
    bool vector_text() const override;
    bool glyph(context *c, font_handle font, u32 index, vec2 baseline, f32 scale,
               color tint) override;
};
struct alignas(16) sdf_shape
{
    rect bounds{};
    corner_radii radii{};
    f32 feather = 1, reserved[3]{};
    u32 corners[4]{};
};
static_assert(sizeof(sdf_shape) == 64);
struct sdf_span
{
    usize vertex = 0;
    u32 shape = 0;
};
struct cache_record
{
    std::vector<u8> canonical;
    rect bounds{};
    u64 hash_a = 0, hash_b = 0;
};
struct texture
{
    sdf_gpu_job *producer = nullptr;
    u64 producer_token = 0;
    device *owner = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags usage = 0;
    VkDescriptorSet descriptors[2]{};
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    i32 width = 0, height = 0;
    bool external = false, owns_view = true, linear = false, target = false, dead = false;
    bool clipped = false, intrinsic = false;
    rect clip{};
    VkSemaphore ready = VK_NULL_HANDLE;
    u64 ready_value = 0;
    u64 last_use = 0, generation = 0, revision = 0;
    u64 allocation = 0;
};
struct page
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkBuffer device_buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceMemory device_memory = VK_NULL_HANDLE;
    u8 *mapped = nullptr;
    VkDeviceSize capacity = 0, allocation = 0, used = 0;
    bool coherent = false;
    bool staged = false;
    VkDeviceSize device_allocation = 0;
    VkDeviceSize offset = 0; // non-owning metadata arena view; zero for owned pages
    bool borrowed = false;
    VkMemoryPropertyFlags memory_flags = 0;
};
struct image_allocation_opts
{
    i32 width = 0, height = 0;
    bool render_target = false;
    const u8 *rgba = nullptr;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags extra_usage = 0;
    u64 allocation_budget = ~0ull;
};
struct sdf_slot_resources
{
    static constexpr u32 MAX_JOBS = 32;
    device *dev = nullptr;
    page banks[2], inputs[2], readable[2], metadata;
    sdf_gpu_job *job_storage = nullptr; // one contiguous owned block; jobs are stable views
    std::vector<sdf_gpu_job *> jobs;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    u32 bank = 0, width = 0, height = 0;
    u64 required_bytes = 0, peak_required_bytes = 0;
    u64 role_peaks[3]{};
    u64 budget_stamp = 0;
    bool cache_requested = false;
    bool pending = true;
    ~sdf_slot_resources();
};
// All backend graphics pipelines share one layout and dynamic viewport/scissor.
// This is command-buffer state, independent of compute bindings and image data.
struct graphics_binding
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkRect2D scissor{};
    VkDescriptorSet sample = VK_NULL_HANDLE;
    u32 routed = 0, shape_base = 0, vertex_words = 0;
};
struct frame_slot
{
    page glyph_requests;
    u64 recording_token = 0;
    VkDescriptorPool kawase_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> kawase_sets;
    usize kawase_cursor = 0;
    std::vector<sdf_gpu_job *> sdf_jobs;
    usize sdf_job_cursor = 0;
    sdf_slot_resources *sdf_resources = nullptr;
    std::vector<VkDescriptorPool> sdf_pools;
    std::vector<VkDescriptorSet> sdf_descriptors;
    usize sdf_descriptor_cursor = 0;
    u64 sdf_sets = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkQueryPool queries = VK_NULL_HANDLE;
    u64 query_serial = 0;
    u64 serial = 0;
    bool recording = false;
    std::vector<page> pages;
    std::vector<VkSemaphoreSubmitInfo> waits;
    graphics_binding graphics{};
};
struct cache_event
{
    enum kind
    {
        CLEAR,
        DRAW,
        COPY
    } type = CLEAR;
    texture *target = nullptr, *sample = nullptr;
    color clear_color{};
    bool clipped = false;
    rect clip{}, source_rect{}, destination_rect{};
    std::vector<vertex> vertices;
    std::vector<i32> indices;
};
// Fixed metadata and prepared CPU payload arenas. Comparing cached CPU bytes
// avoids slow reads from mapped GPU uploads and never allocates in a draw.
struct retained_draw
{
    u64 key[12]{};
    const u8 *vertices = nullptr, *indices = nullptr, *shapes = nullptr;
    const u8 *spans = nullptr;
    u32 vertex_count = 0, index_bytes = 0, shape_bytes = 0, stride = 0;
    u32 span_bytes = 0;
};
struct retained_texture
{
    texture *image = nullptr;
    u64 generation = 0, revision = 0, final_revision = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    u32 writer = 0;
    bool read_before_write = false, reset = false;
};
struct retained_program
{
    static constexpr usize MAX_DRAWS = 512, MAX_IMAGES = 128;
    retained_draw draws[MAX_DRAWS]{};
    retained_texture images[MAX_IMAGES]{};
    usize draw_count = 0, image_count = 0;
    u8 *payload = nullptr;
    usize payload_capacity = 0, payload_used = 0;
    ~retained_program() { delete[] payload; }
    u64 serial = 0;
    u64 draws_before = 0, vertices_before = 0, indices_before = 0, batches_before = 0;
    u64 kawase_before = 0, kawase_pixels_before = 0;
    bool valid = false;
};
struct program_cache_resources
{
    struct probe
    {
        page control;
        u64 serial = 0, epoch = 0;
    } probes[8];
    struct image_copy
    {
        texture *image = nullptr;
        u64 offset = 0;
    } images[retained_program::MAX_IMAGES];
    device *dev = nullptr;
    page before;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet sets[8][retained_program::MAX_IMAGES]{};
    u32 slots = 0, width = 0, height = 0, allocated_sets = 0;
    usize count = 0;
    u64 epoch = 1, confirmed = 0;
    u32 changing_frames = 0, retry_in = 0;
    u32 invalid_observations = 0;
    bool recording = false, warm = false, partial_reuse = false;
    ~program_cache_resources();
};
struct surface : render_surface, dispatch
{
    program_cache_resources *program_cache = nullptr;
    retained_program *retained[2]{};
    u32 retained_bank = 0;
    bool program_compute_preparation_pending = false;
    device *dev = nullptr;
    void *window = nullptr;
    VkSurfaceKHR native = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> images;
    std::vector<VkSemaphore> finished;
    std::vector<bool> initialized;
    std::vector<frame_slot> slots;
    texture *scene = nullptr;
    texture *alpha_scratch = nullptr;
    texture *kawase[4]{};
    u32 slot = 0, image_index = 0;
    bool acquired = false, presented = true, dirty = true;
    bool suspended = true;
    bool sdf_compute_requested = false, sdf_preparation_pending = false;
    bool history_valid = false;
    u32 cache_full_misses = 0, cache_cooldown = 0;
    u64 history_bytes = 0;
    std::vector<cache_record> history;
    void make_current(render_device &) override;
    texture_handle default_target() override { return scene; }
    texture_handle scene_target() override { return scene; }
    void output_size(i32 &w, i32 &h) override;
    void resize() override { dirty = true; }
    void present() override;
    bool rebuild();
    void release_swapchain();
};
struct pipeline_pair
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkPipeline blend = VK_NULL_HANDLE, copy = VK_NULL_HANDLE;
    VkPipeline sdf = VK_NULL_HANDLE, packed = VK_NULL_HANDLE;
};
struct program_draw
{
    texture *sample = nullptr;
    VkBuffer geometry = VK_NULL_HANDLE;
    VkDeviceSize offset = 0, vertex_bytes = 0;
    VkDescriptorSet shapes = VK_NULL_HANDLE;
    VkRect2D scissor{};
    u32 vertices = 0, indices = 0;
    u32 shape_base = 0;
    u32 vertex_words = 0;
    bool packed = false;
};
struct device : render_device, dispatch
{
    retained_program *retaining = nullptr, *retained_previous = nullptr;
    bool retained_equal = false, retained_deferred = false;
    bool can_compute = false;
    void retained_begin(surface &);
    void retained_abort()
    {
        retained_materialize();
        if (retaining) retaining->valid = false;
    }
    void retained_materialize();
    retained_texture *retained_touch(texture *);
    void retained_add(retained_draw, texture *output, texture *input, bool replaces = false);
    bool retained_finish(surface &);
    bool prepare_program_cache(surface &);
    void release_program_cache(surface &);
    void retained_probe_begin(surface &);
    bool retained_probe_end(surface &);
    bool retained_confirmed(surface &);
    VkDescriptorSetLayout program_compare_set = VK_NULL_HANDLE;
    VkPipelineLayout program_compare_layout = VK_NULL_HANDLE;
    VkPipeline program_compare_pipeline = VK_NULL_HANDLE;
    program_draw program_draws[64]{};
    usize program_count = 0;
    texture *program_target = nullptr;
    bool program_flushing = false;
    usize program_shape_end = 0, program_command_end = 0, program_payload_end = 0;
    bool command_clip_valid = false;
    VkRect2D command_clip{};
    bool flush_program();
    sdf_gpu_job *program_producers[64]{};
    bool live_producer(texture *) const;
    u32 producer_role(texture *) const;
    bool native_only_program() const
    {
        // Observe a program with native operands first. Re-encoding and running
        // the tile interpreter merely to obtain a whole-image certificate is
        // duplicate work. Use fine-grained reuse when an actual GPU comparison
        // proves that unchanged commands still have changing LOAD dependencies.
        // The certificate path itself uses independent native-output snapshots.
        return options.painter == vulkan_painter_mode::INTEGRATED && options.whole_program_cache &&
               current_surface &&
               (!current_surface->program_cache || !current_surface->program_cache->partial_reuse ||
                current_surface->program_compute_preparation_pending || !retaining ||
                retained_deferred);
    }
    bool program_batching() const
    {
        return !initializing && current_surface && !program_flushing &&
               paint_detail::compute_painter(options.painter) && !options.adaptive_compute &&
               !native_only_program();
    }
    std::span<const sdf_shape> shape_view() const
    {
        return {sdf_shapes.data(), program_flushing ? program_shape_end : sdf_shapes.size()};
    }
    std::span<const paint_detail::command> command_view() const
    {
        return {sdf_tape.commands.data(),
                program_flushing ? program_command_end : sdf_tape.commands.size()};
    }
    bool native_scissor(VkRect2D &) const;
    void seal_program_metadata(texture *, const VkRect2D &, u32 indices);
    std::span<const u8> command_payload_view() const
    {
        return {sdf_tape.payload.data(),
                program_flushing ? program_payload_end : sdf_tape.payload.size()};
    }
    render_painter *create_painter(context &c) override
    {
        auto *p = new renderer_detail::mesh_painter(c);
        p->native =
            options.painter == vulkan_painter_mode::REFERENCE ? nullptr : &primitive_commands;
        p->vectors = options.vector_text;
        p->native_device = this;
        p->native_blur = paint_detail::vulkan_blur;
        if (options.painter == vulkan_painter_mode::INTEGRATED)
        {
            p->native_scene = [](render_device *base, render_surface *s) -> texture_handle
            {
                auto *d = static_cast<device *>(base);
                return d->alpha_context ? d->current_surface->alpha_scratch : s->scene_target();
            };
            p->native_end = [](context *c)
            {
                auto *d = static_cast<device *>(c->device);
                if (!d->alpha_context || d->alpha_context != c) return;
                PUFFERUI_CHECK(VIOL_SCOPE_UNBALANCED, false, "Vulkan alpha barrier outlived frame");
                d->end_alpha(c);
                detail::dl_flush(c);
            };
        }
        if (paint_detail::sdf_painter(options.painter))
            p->native_gradient = [](context *c, const paint_rounded_gradient &g, color mid)
            {
                ui u(c);
                return vulkan_try_rounded_gradient(u, {.area = g.area,
                                                       .radii = corner_radii::all(g.radius),
                                                       .underlay = mid,
                                                       .top_left = g.top_left,
                                                       .top_right = g.top_right,
                                                       .bottom_right = g.bottom_right,
                                                       .bottom_left = g.bottom_left});
            };
        return p;
    }
    vulkan_device_opts options{};
    vulkan_host *owned_host = nullptr;
    vulkan_native_device native{};
    bool owns_native = false, vsync = true;
    VkResult result = VK_SUCCESS;
    std::string error;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
    VkSemaphore timeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout sdf_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkSampler samplers[2]{};
    VkShaderModule vertex_shader = VK_NULL_HANDLE, fragment_shader = VK_NULL_HANDLE;
    VkShaderModule sdf_vertex_shader = VK_NULL_HANDLE, sdf_fragment_shader = VK_NULL_HANDLE;
    VkShaderModule packed_vertex_shader = VK_NULL_HANDLE;
    std::vector<sdf_shape> sdf_shapes;
    std::vector<sdf_span> sdf_spans;
    std::vector<u8> slug_data;
    page slug_storage; // append-only staging + device-local outlines, shared by slots
    VkDeviceSize slug_committed = 0;
    paint_detail::command_tape sdf_tape;
    rgba8_blend_lut *sdf_blend = nullptr;
    native_function_cache *sdf_functions = nullptr;
    struct glyph_source_entry
    {
        sdf_shape shape{};
        u64 hash = 0, generation = 0, last_use = 0, fill_token = 0;
        u8 alpha = 0;
        bool ready = false;
    } glyph_sources[128];
    texture *glyph_source_image = nullptr;
    VkPipelineLayout glyph_source_layout = VK_NULL_HANDLE;
    VkPipeline glyph_source_fill = VK_NULL_HANDLE, glyph_source_publish = VK_NULL_HANDLE;
    u32 glyph_source_capacity = 0;
    u64 glyph_source_generation = 0, recording_generation = 0, known_completed = 0;
    bool glyph_source_filling = false;
    u64 glyph_source_hits = 0, glyph_source_misses = 0, glyph_source_fills = 0;
    u64 integrated_counts[4]{}; // AA pixels, glyph pixels, function misses, float-blended pixels
    u64 integrated_alpha_pixels = 0;
    u64 glyph_source_gpu_hits = 0;
    VkDescriptorSetLayout bin_descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout sdf_image_layout = VK_NULL_HANDLE;
    VkPipelineLayout bin_pipeline_layout = VK_NULL_HANDLE;
    VkPipelineLayout sdf_compute_layout = VK_NULL_HANDLE;
    VkPipeline bin_pipelines[4]{}, sdf_pipelines[6]{};
    VkPipeline native_cache_pipelines[2]{};
    VkPipeline native_replay_pipeline = VK_NULL_HANDLE;
    u64 native_replay_counts[2]{}; // GPU packets with/without native vertex invocations
    VkDescriptorSetLayout kawase_descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout kawase_layout = VK_NULL_HANDLE;
    VkPipeline kawase_pipeline = VK_NULL_HANDLE;
    u32 paint_depth = 0;
    bool initializing = true;
    u32 sdf_cache_faults = 0; // private fault-injection hooks used only by regression tests
    u32 sdf_adaptive_cooldown = 0;
    std::vector<VkDescriptorPool> descriptor_pools;
    std::vector<pipeline_pair> pipelines;
    std::vector<texture *> textures, retired;
    std::vector<surface *> surfaces;
    frame_slot utility;
    frame_slot *frame = nullptr;
    surface *current_surface = nullptr;
    texture *target = nullptr, *white = nullptr, *rendering_target = nullptr;
    context *alpha_context = nullptr;
    texture *alpha_destination = nullptr;
    texture *alpha_sample = nullptr;
    rect alpha_clip{};
    f32 alpha_opacity = 1;
    bool alpha_clipped = false;
    bool prepare_alpha(surface &);
    bool begin_alpha(context *, f32);
    void end_alpha(context *);
    bool has_clip = false;
    rect clip{};
    u64 next_serial = 1, generation = 1;
    u32 timestamp_bits = 0;
    vulkan_stats stats{};
    primitive_recorder primitive_commands;
    bool cache_active = false, replay_clipped = false;
    rect replay_clip{};
    u64 cache_bytes = 0;
    surface *cache_pending = nullptr;
    std::vector<cache_event> events;
    std::vector<cache_record> records;
    std::vector<texture *> cache_written;
    void cache_begin(surface *s);
    bool cache_add(cache_event::kind type, texture *sample = nullptr, const vertex *v = nullptr,
                   i32 vc = 0, const i32 *idx = nullptr, i32 ic = 0, color c = {},
                   const rect *sr = nullptr, const rect *dr = nullptr);
    void cache_flush(bool final);
    void cache_commit(bool success);
    u64 cache_usage() const;
    bool cache_reserve(u64 bytes);
    bool start_rendering(texture *t);
    bool initialize(const vulkan_device_opts &opts);
    u64 new_generation();
    ~device() override;
    bool check(VkResult r, const char *operation);
    bool fail(const char *operation) { return check(VK_ERROR_INITIALIZATION_FAILED, operation); }
    u64 completed();
    bool wait(u64 serial);
    bool begin(frame_slot &slot);
    u64 submit(VkSemaphore acquired = VK_NULL_HANDLE, VkSemaphore finished = VK_NULL_HANDLE);
    void end_rendering();
    void collect();
    void harvest(frame_slot &slot, u64 done);
    void destroy_slot(frame_slot &slot);
    bool allocate_buffer(page &p, VkDeviceSize bytes, VkBufferUsageFlags usage);
    bool allocate_device_buffer(page &p);
    bool reserve_mapped(VkDeviceSize bytes);
    void free_page(page &p);
    bool flush_page(page &p);
    page *upload(VkDeviceSize bytes, VkDeviceSize &offset, bool geometry = false);
    u32 memory_type(u32 bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred);
    texture *make_texture(const image_allocation_opts &opts);
    texture *make_texture(i32 w, i32 h, bool render_target, const u8 *rgba)
    {
        return make_texture(
            {.width = w, .height = h, .render_target = render_target, .rgba = rgba});
    }
    void free_texture(texture *t);
    bool descriptors(texture *t);
    bool valid(texture *t) const;
    bool commands();
    void transition(texture *t, VkImageLayout layout);
    VkPipeline pipeline(VkFormat format, bool blend, bool sdf = false);
    VkPipeline packed_pipeline(VkFormat format) const;
    void bind_graphics(const graphics_binding &binding);
    VkDescriptorSet sdf_descriptor(const VkDescriptorBufferInfo (&buffers)[4]);
    bool initialize_sdf_compute();
    bool initialize_sdf_compute_kernels();
    bool initialize_sdf_cache_kernels();
    bool initialize_bin_kernels();
    bool prepare_sdf_surface(surface &s);
    bool initialize_kawase();
    bool prepare_kawase(surface &s);
    bool kawase_blur(texture *, rect, f32, paint_detail::blur_result &);
    bool begin_sdf_slot(frame_slot &slot);
    bool prepare_sdf_descriptors(frame_slot &slot);
    bool upload_slug_data();
    bool initialize_glyph_source();
    bool prepare_glyph_source(frame_slot &);
    void cache_glyph_source(sdf_shape &, color);
    bool fill_glyph_sources();
    void submitted_glyph_sources(u64 token);
    void release_glyph_source();
    bool prepare_geometry(const geometry_capacity &capacity) override;
    void release_sdf_compute();
    void destroy_sdf_jobs(frame_slot &slot);
    sdf_gpu_job *prepare_sdf_compute();
    VkDescriptorBufferInfo sdf_route(sdf_gpu_job *job);
    VkDescriptorBufferInfo sdf_shape_data(sdf_gpu_job *job);
    VkDescriptorBufferInfo sdf_lut();
    bool finish_sdf_compute(sdf_gpu_job *, VkDescriptorSet);
    void replay_sdf_native(sdf_gpu_job *, u32 packet);
    bool execute_sdf_compute(sdf_gpu_job *job, VkDescriptorSet shapes,
                             const VkDescriptorBufferInfo &shape_data);
    void harvest_sdf_stats();
    void paint(texture *sample, const vertex *v, i32 vc, const i32 *idx, i32 ic, bool blend,
               const retained_draw *snapshot = nullptr);
    backend_caps caps() const override
    {
        return backend_caps::SCISSOR | backend_caps::RENDER_TARGETS |
               backend_caps::STREAMING_TEXTURES | backend_caps::READ_PIXELS |
               backend_caps::SHARED_DEVICE;
    }
    render_surface *create_surface(void *window) override;
    render_surface *make_surface(void *window, i32 width, i32 height);
    void destroy_surface(render_surface *s) override;
    void begin_frame() override { collect(); }
    void set_vsync(bool enabled) override;
    texture_handle create_texture(i32 w, i32 h, const u8 *rgba) override
    {
        return make_texture(w, h, false, rgba);
    }
    texture_handle create_target(i32 w, i32 h) override
    {
        return make_texture(w, h, true, nullptr);
    }
    void destroy_texture(texture_handle handle) override;
    void update_texture(texture_handle handle, i32 x, i32 y, i32 w, i32 h, const u8 *rgba) override;
    void set_texture_filter(texture_handle handle, bool linear) override;
    void set_target(texture_handle handle) override;
    texture_handle current_target() const override { return target; }
    void clear(color c) override;
    void set_clip(const rect *r) override
    {
        command_clip_valid = false;
        has_clip = r != nullptr;
        if (r) clip = *r;
        if (target)
        {
            target->clipped = has_clip;
            if (r) target->clip = *r;
        }
    }
    void set_cursor(cursor c) override
    {
        if (options.host) options.host->set_cursor(c);
    }
    void draw(texture_handle tex, const vertex *v, i32 vc, const i32 *idx, i32 ic) override;
    void blit(texture_handle src, const rect *sr, texture_handle dst, const rect *dr) override;
    bool read_pixels(texture_handle t, i32 x, i32 y, i32 w, i32 h, u8 *rgba) override;
};
bool device::check(VkResult r, const char *operation)
{
    if (r == VK_SUCCESS) return true;
    if (result == VK_SUCCESS)
    {
        result = r;
        error = operation;
        if (options.error_callback)
            options.error_callback(static_cast<i32>(r), operation, options.user);
        else
            std::fprintf(stderr, "PufferUI Vulkan error %d: %s\n", static_cast<i32>(r), operation);
    }
    return false;
}
static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
              const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    auto *d = static_cast<device *>(user);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        ++d->stats.validation_errors;
        if (d->options.error_callback)
            d->options.error_callback(VK_ERROR_VALIDATION_FAILED_EXT, data->pMessage,
                                      d->options.user);
        else
            std::fprintf(stderr, "PufferUI Vulkan validation: %s\n", data->pMessage);
    }
    return VK_FALSE;
}
bool device::initialize(const vulkan_device_opts &opts)
{
    options = opts;
    if (opts.vector_text && !paint_detail::sdf_painter(opts.painter))
        return fail("GPU vector text requires the SDF painter");
#if !defined(PUFFERUI_ENABLE_HARFBUZZ)
    if (opts.vector_text) return fail("HarfBuzz GPU text was not enabled at build time");
#endif
    if (!opts.frames_in_flight || opts.frames_in_flight > 8 || !opts.upload_budget_bytes)
        return fail("invalid Vulkan frame count or upload budget");
    if (opts.upload_mode != vulkan_upload_mode::DIRECT &&
        opts.upload_mode != vulkan_upload_mode::STAGED)
        return fail("invalid Vulkan upload mode");
    if (opts.painter != vulkan_painter_mode::REFERENCE &&
        opts.painter != vulkan_painter_mode::COMMAND_SHADOW &&
        !paint_detail::sdf_painter(opts.painter))
        return fail("invalid or unimplemented Vulkan painter mode");
    if (opts.native)
    {
        native = *opts.native;
        if (!native.instance || !native.physical_device || !native.device || !native.queue ||
            !native.dynamic_rendering || !native.synchronization2 || !native.timeline_semaphore)
            return fail(
                "borrowed Vulkan device requires enabled rendering/sync2/timeline features");
        load_global(native.get_instance_proc_addr);
        load_instance(native.instance);
        load_device(native.device, native.get_device_proc_addr);
    }
    else
    {
        owns_native = true;
        load_global(opts.host ? opts.host->instance_proc_addr() : nullptr);
        std::vector<const char *> extensions;
        if (opts.host)
            for (const char *name : opts.host->instance_extensions()) extensions.push_back(name);
        const char *layer = "VK_LAYER_KHRONOS_validation";
        if (opts.validation)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "PufferUI";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = static_cast<u32>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();
        ci.enabledLayerCount = opts.validation ? 1u : 0u;
        ci.ppEnabledLayerNames = &layer;
        VkValidationFeatureEnableEXT sync_validation =
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        validation.enabledValidationFeatureCount = 1;
        validation.pEnabledValidationFeatures = &sync_validation;
        if (opts.validation) ci.pNext = &validation;
        if (!check(vkCreateInstance(&ci, nullptr, &native.instance), "vkCreateInstance"))
            return false;
        load_instance(native.instance);
        if (opts.validation)
        {
            VkDebugUtilsMessengerCreateInfoEXT dc{
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            dc.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dc.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dc.pfnUserCallback = debug_message;
            dc.pUserData = this;
            auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(native.instance, "vkCreateDebugUtilsMessengerEXT"));
            if (!create || !check(create(native.instance, &dc, nullptr, &debug),
                                  "vkCreateDebugUtilsMessengerEXT"))
                return false;
        }
        u32 count = 0;
        if (!check(vkEnumeratePhysicalDevices(native.instance, &count, nullptr),
                   "vkEnumeratePhysicalDevices"))
            return false;
        std::vector<VkPhysicalDevice> devices(count);
        if (!check(vkEnumeratePhysicalDevices(native.instance, &count, devices.data()),
                   "vkEnumeratePhysicalDevices"))
            return false;
        i32 best_score = -1;
        for (u32 index = 0; index < count; ++index)
        {
            if (opts.physical_device_index != 0xffffffffu && opts.physical_device_index != index)
                continue;
            const VkPhysicalDevice physical = devices[index];
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(physical, &props);
            if (props.apiVersion < VK_API_VERSION_1_3) continue;
            VkPhysicalDeviceVulkan13Features f13{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceVulkan12Features f12{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            f12.pNext = &f13;
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &f12;
            vkGetPhysicalDeviceFeatures2(physical, &features);
            if (!f13.dynamicRendering || !f13.synchronization2 || !f12.timelineSemaphore) continue;
            u32 families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
            std::vector<VkQueueFamilyProperties> qp(families);
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, qp.data());
            for (u32 i = 0; i < families; ++i)
                if (qp[i].queueCount && (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                {
                    const i32 score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
                                          ? 3
                                          : 1;
                    if (score > best_score)
                    {
                        best_score = score;
                        native.physical_device = physical;
                        native.queue_family = i;
                    }
                    break;
                }
        }
        if (!native.physical_device)
            return fail("Vulkan 1.3 graphics device with required features unavailable");
        float priority = 1;
        VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qc.queueFamilyIndex = native.queue_family;
        qc.queueCount = 1;
        qc.pQueuePriorities = &priority;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        f13.dynamicRendering = VK_TRUE;
        f13.synchronization2 = VK_TRUE;
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        f12.timelineSemaphore = VK_TRUE;
        f12.pNext = &f13;
        const char *swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dc.pNext = &f12;
        dc.queueCreateInfoCount = 1;
        dc.pQueueCreateInfos = &qc;
        dc.enabledExtensionCount = opts.host ? 1u : 0u;
        dc.ppEnabledExtensionNames = &swapchain;
        if (!check(vkCreateDevice(native.physical_device, &dc, nullptr, &native.device),
                   "vkCreateDevice"))
            return false;
        load_device(native.device, nullptr);
        vkGetDeviceQueue(native.device, native.queue_family, 0, &native.queue);
        native.dynamic_rendering = native.synchronization2 = native.timeline_semaphore = true;
    }
    native.get_instance_proc_addr = vkGetInstanceProcAddr;
    native.get_device_proc_addr = vkGetDeviceProcAddr;
    vkGetPhysicalDeviceProperties(native.physical_device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3) return fail("Vulkan 1.3 device required");
    VkFormatProperties rgba_format{};
    vkGetPhysicalDeviceFormatProperties(native.physical_device, VK_FORMAT_R8G8B8A8_UNORM,
                                        &rgba_format);
    constexpr VkFormatFeatureFlags required_format =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((rgba_format.optimalTilingFeatures & required_format) != required_format)
        return fail("Vulkan RGBA8 render/linear sampling/transfer support required");
    vkGetPhysicalDeviceMemoryProperties(native.physical_device, &memory_properties);
    u32 queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(native.physical_device, &queue_count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(native.physical_device, &queue_count, queues.data());
    if (native.queue_family >= queue_count ||
        !(queues[native.queue_family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        return fail("invalid Vulkan graphics queue family");
    can_compute = (queues[native.queue_family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    timestamp_bits = queues[native.queue_family].timestampValidBits;
    stats.timestamps_supported = timestamp_bits > 0 && properties.limits.timestampPeriod > 0;
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    si.pNext = &type;
    if (!check(vkCreateSemaphore(native.device, &si, nullptr, &timeline),
               "vkCreateSemaphore(timeline)"))
        return false;
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorCount = 1;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 1;
    dl.pBindings = &binding;
    if (!check(vkCreateDescriptorSetLayout(native.device, &dl, nullptr, &descriptor_layout),
               "vkCreateDescriptorSetLayout"))
        return false;
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    range.size = sizeof(float) * 2;
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkDescriptorSetLayout layouts[2] = {descriptor_layout, VK_NULL_HANDLE};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = layouts;
    if (paint_detail::sdf_painter(opts.painter))
    {
        VkDescriptorSetLayoutBinding sdf_bindings[6] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
             nullptr},
            {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        for (auto &b : sdf_bindings) b.stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
        if (opts.painter == vulkan_painter_mode::INTEGRATED)
            sdf_bindings[3].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        dl.bindingCount = opts.painter == vulkan_painter_mode::INTEGRATED ? 6 : 4;
        dl.pBindings = sdf_bindings;
        if (!check(vkCreateDescriptorSetLayout(native.device, &dl, nullptr, &sdf_layout),
                   "vkCreateDescriptorSetLayout(SDF)"))
            return false;
        layouts[1] = sdf_layout;
        pl.setLayoutCount = 2;
    }
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    if (paint_detail::sdf_painter(opts.painter))
    {
        range.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        range.size = opts.painter == vulkan_painter_mode::INTEGRATED ? 20 : 16;
    }
    if (!check(vkCreatePipelineLayout(native.device, &pl, nullptr, &pipeline_layout),
               "vkCreatePipelineLayout"))
        return false;
    for (u32 i = 0; i < 2; ++i)
    {
        VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sc.magFilter = sc.minFilter = i ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sc.maxLod = 0;
        if (!check(vkCreateSampler(native.device, &sc, nullptr, &samplers[i]), "vkCreateSampler"))
            return false;
    }
    const u8 pixel[4] = {255, 255, 255, 255};
    white = make_texture(1, 1, false, pixel);
    const bool ready = white && pipeline(VK_FORMAT_R8G8B8A8_UNORM, true) &&
                       pipeline(VK_FORMAT_R8G8B8A8_UNORM, false) && initialize_sdf_compute() &&
                       initialize_kawase() &&
                       (!paint_detail::sdf_painter(options.painter) ||
                        pipeline(VK_FORMAT_R8G8B8A8_UNORM, true, true));
    if (ready && paint_detail::compute_painter(options.painter))
    {
        primitive_commands.owner = this;
        primitive_commands.reset();
    }
    initializing = false;
    return ready;
}
device::~device()
{
    if (native.device)
    {
        if (frame && frame->recording)
        {
            if (result == VK_SUCCESS)
                submit();
            else
            {
                end_rendering();
                vkResetCommandPool(native.device, frame->pool, 0);
                frame->recording = false;
            }
        }
        if (timeline) wait(next_serial - 1);
        release_sdf_compute();
        if (kawase_pipeline) vkDestroyPipeline(native.device, kawase_pipeline, nullptr);
        if (kawase_layout) vkDestroyPipelineLayout(native.device, kawase_layout, nullptr);
        if (kawase_descriptor_layout)
            vkDestroyDescriptorSetLayout(native.device, kawase_descriptor_layout, nullptr);
        for (surface *s : surfaces)
        {
            release_program_cache(*s);
            s->release_swapchain();
            for (auto &slot : s->slots) destroy_slot(slot);
            if (s->native) vkDestroySurfaceKHR(native.instance, s->native, nullptr);
            delete s;
        }
        for (texture *t : textures) free_texture(t);
        for (texture *t : retired) free_texture(t);
        destroy_slot(utility);
        for (auto &p : pipelines)
        {
            vkDestroyPipeline(native.device, p.blend, nullptr);
            vkDestroyPipeline(native.device, p.copy, nullptr);
            if (p.sdf) vkDestroyPipeline(native.device, p.sdf, nullptr);
            if (p.packed) vkDestroyPipeline(native.device, p.packed, nullptr);
        }
        for (auto pool : descriptor_pools) vkDestroyDescriptorPool(native.device, pool, nullptr);
        if (vertex_shader) vkDestroyShaderModule(native.device, vertex_shader, nullptr);
        if (fragment_shader) vkDestroyShaderModule(native.device, fragment_shader, nullptr);
        if (sdf_vertex_shader) vkDestroyShaderModule(native.device, sdf_vertex_shader, nullptr);
        if (sdf_fragment_shader) vkDestroyShaderModule(native.device, sdf_fragment_shader, nullptr);
        if (packed_vertex_shader)
            vkDestroyShaderModule(native.device, packed_vertex_shader, nullptr);
        for (auto sampler : samplers)
            if (sampler) vkDestroySampler(native.device, sampler, nullptr);
        if (program_compare_pipeline)
            vkDestroyPipeline(native.device, program_compare_pipeline, nullptr);
        if (program_compare_layout)
            vkDestroyPipelineLayout(native.device, program_compare_layout, nullptr);
        if (program_compare_set)
            vkDestroyDescriptorSetLayout(native.device, program_compare_set, nullptr);
        if (pipeline_layout) vkDestroyPipelineLayout(native.device, pipeline_layout, nullptr);
        if (descriptor_layout)
            vkDestroyDescriptorSetLayout(native.device, descriptor_layout, nullptr);
        if (sdf_layout) vkDestroyDescriptorSetLayout(native.device, sdf_layout, nullptr);
        if (timeline) vkDestroySemaphore(native.device, timeline, nullptr);
        if (owns_native) vkDestroyDevice(native.device, nullptr);
    }
    if (debug)
    {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(native.instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(native.instance, debug, nullptr);
    }
    if (owns_native && native.instance) vkDestroyInstance(native.instance, nullptr);
    delete owned_host;
}
} // namespace vk_detail
render_device *create_vulkan_device(const vulkan_device_opts &opts)
{
    auto *d = new vk_detail::device();
    if (!d->initialize(opts))
    {
        delete d;
        return nullptr;
    }
    return d;
}
void destroy_vulkan_device(render_device *d)
{
    delete d;
}
bool is_vulkan_device(render_device *d)
{
    return dynamic_cast<vk_detail::device *>(d) != nullptr;
}
vulkan_painter_mode vulkan_painter(render_device *d)
{
    auto *native = dynamic_cast<vk_detail::device *>(d);
    return native ? native->options.painter : vulkan_painter_mode::REFERENCE;
}
vulkan_native_device vulkan_handles(render_device *d)
{
    auto *v = dynamic_cast<vk_detail::device *>(d);
    return v ? v->native : vulkan_native_device{};
}
VkSemaphore vulkan_completion_semaphore(render_device *d)
{
    auto *v = dynamic_cast<vk_detail::device *>(d);
    return v ? v->timeline : VK_NULL_HANDLE;
}
bool vulkan_wait(render_device *d, u64 value)
{
    auto *v = dynamic_cast<vk_detail::device *>(d);
    return v && v->wait(value);
}
bool vulkan_wait_idle(render_device *d)
{
    auto *v = dynamic_cast<vk_detail::device *>(d);
    if (!v) return false;
    if (v->frame && v->frame->recording && !v->submit()) return false;
    const bool okay = v->result == VK_SUCCESS && v->wait(v->next_serial - 1);
    if (okay) v->collect();
    return okay;
}
VkResult vulkan_last_result(render_device *d)
{
    auto *v = dynamic_cast<vk_detail::device *>(d);
    return v ? v->result : VK_ERROR_INITIALIZATION_FAILED;
}
const char *vulkan_last_error(render_device *d)
{
    auto *v = dynamic_cast<vk_detail::device *>(d);
    return v ? v->error.c_str() : "not a Vulkan device";
}
vulkan_stats vulkan_statistics(render_device *d)
{
    auto *v = dynamic_cast<vk_detail::device *>(d);
    if (!v) return {};
    v->harvest_sdf_stats();
    auto stats = v->stats;
    stats.cache_metadata_bytes = v->cache_usage();
    return stats;
}
void vulkan_set_cache(render_device *d, bool enabled)
{
    if (auto *v = dynamic_cast<vk_detail::device *>(d))
    {
        if (v->options.tile_cache == enabled) return;
        v->options.tile_cache = enabled;
        if (paint_detail::compute_painter(v->options.painter))
            for (auto *s : v->surfaces) s->sdf_preparation_pending = true;
    }
}
#else
render_device *create_vulkan_device(const vulkan_device_opts &)
{
    return nullptr;
}
void destroy_vulkan_device(render_device *d)
{
    delete d;
}
bool is_vulkan_device(render_device *)
{
    return false;
}
vulkan_painter_mode vulkan_painter(render_device *)
{
    return vulkan_painter_mode::REFERENCE;
}
render_surface *create_vulkan_surface(render_device *, const vulkan_surface_opts &)
{
    return nullptr;
}
#endif
