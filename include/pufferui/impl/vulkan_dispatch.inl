// Per-instance/per-device dispatch. No process-global mutable dispatch state.
// Members intentionally use Vulkan names: member lookup selects this device.
// Borrowed engines can provide their own loader; tests can wrap error paths.
// Included inside namespace pui by the single implementation TU.
#if defined(PUFFERUI_ENABLE_VULKAN)
namespace vk_detail
{
#define PUI_VK_INSTANCE_FUNCTIONS(X)                                                               \
    X(DestroyInstance)                                                                             \
    X(EnumeratePhysicalDevices)                                                                    \
    X(GetPhysicalDeviceProperties)                                                                 \
    X(GetPhysicalDeviceFeatures2)                                                                  \
    X(GetPhysicalDeviceMemoryProperties)                                                           \
    X(GetPhysicalDeviceQueueFamilyProperties)                                                      \
    X(GetPhysicalDeviceFormatProperties)                                                           \
    X(CreateDevice)                                                                                \
    X(GetDeviceProcAddr)                                                                           \
    X(DestroySurfaceKHR)                                                                           \
    X(GetPhysicalDeviceSurfaceSupportKHR)                                                          \
    X(GetPhysicalDeviceSurfaceCapabilitiesKHR)                                                     \
    X(GetPhysicalDeviceSurfaceFormatsKHR)                                                          \
    X(GetPhysicalDeviceSurfacePresentModesKHR)
#define PUI_VK_DEVICE_FUNCTIONS(X)                                                                 \
    X(DestroyDevice)                                                                               \
    X(GetDeviceQueue)                                                                              \
    X(CreateSemaphore)                                                                             \
    X(DestroySemaphore)                                                                            \
    X(GetSemaphoreCounterValue)                                                                    \
    X(WaitSemaphores)                                                                              \
    X(CreateCommandPool)                                                                           \
    X(DestroyCommandPool)                                                                          \
    X(AllocateCommandBuffers)                                                                      \
    X(BeginCommandBuffer)                                                                          \
    X(EndCommandBuffer)                                                                            \
    X(ResetCommandPool)                                                                            \
    X(QueueSubmit2)                                                                                \
    X(QueueWaitIdle)                                                                               \
    X(CreateBuffer)                                                                                \
    X(DestroyBuffer)                                                                               \
    X(GetBufferMemoryRequirements)                                                                 \
    X(AllocateMemory)                                                                              \
    X(FreeMemory)                                                                                  \
    X(BindBufferMemory)                                                                            \
    X(MapMemory)                                                                                   \
    X(UnmapMemory)                                                                                 \
    X(FlushMappedMemoryRanges)                                                                     \
    X(InvalidateMappedMemoryRanges)                                                                \
    X(CreateImage)                                                                                 \
    X(DestroyImage)                                                                                \
    X(GetImageMemoryRequirements)                                                                  \
    X(BindImageMemory)                                                                             \
    X(CreateImageView)                                                                             \
    X(DestroyImageView)                                                                            \
    X(CreateSampler)                                                                               \
    X(DestroySampler)                                                                              \
    X(CreateDescriptorSetLayout)                                                                   \
    X(DestroyDescriptorSetLayout)                                                                  \
    X(CreateDescriptorPool)                                                                        \
    X(DestroyDescriptorPool)                                                                       \
    X(AllocateDescriptorSets)                                                                      \
    X(FreeDescriptorSets)                                                                          \
    X(ResetDescriptorPool)                                                                         \
    X(UpdateDescriptorSets)                                                                        \
    X(CreatePipelineLayout)                                                                        \
    X(DestroyPipelineLayout)                                                                       \
    X(CreateShaderModule)                                                                          \
    X(DestroyShaderModule)                                                                         \
    X(CreateGraphicsPipelines)                                                                     \
    X(CreateComputePipelines)                                                                      \
    X(DestroyPipeline)                                                                             \
    X(CmdBeginRendering)                                                                           \
    X(CmdEndRendering)                                                                             \
    X(CmdPipelineBarrier2)                                                                         \
    X(CmdClearColorImage)                                                                          \
    X(CmdClearAttachments)                                                                         \
    X(CmdCopyBufferToImage)                                                                        \
    X(CmdCopyImageToBuffer)                                                                        \
    X(CmdCopyImage)                                                                                \
    X(CmdCopyBuffer)                                                                               \
    X(CmdBlitImage)                                                                                \
    X(CmdBindPipeline)                                                                             \
    X(CmdBindVertexBuffers)                                                                        \
    X(CmdBindIndexBuffer)                                                                          \
    X(CmdBindDescriptorSets)                                                                       \
    X(CmdPushConstants)                                                                            \
    X(CmdSetViewport)                                                                              \
    X(CmdSetScissor)                                                                               \
    X(CmdDrawIndexed)                                                                              \
    X(CmdDrawIndirect)                                                                             \
    X(CmdDrawIndexedIndirect)                                                                      \
    X(CmdDispatch)                                                                                 \
    X(CmdDispatchIndirect)                                                                         \
    X(CmdFillBuffer)                                                                               \
    X(CreateSwapchainKHR)                                                                          \
    X(DestroySwapchainKHR)                                                                         \
    X(GetSwapchainImagesKHR)                                                                       \
    X(AcquireNextImageKHR)                                                                         \
    X(QueuePresentKHR)                                                                             \
    X(CreateQueryPool)                                                                             \
    X(DestroyQueryPool)                                                                            \
    X(CmdResetQueryPool)                                                                           \
    X(CmdWriteTimestamp2)                                                                          \
    X(GetQueryPoolResults)
struct dispatch
{
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = ::vkGetInstanceProcAddr;
    PFN_vkCreateInstance vkCreateInstance = ::vkCreateInstance;
#define PUI_VK_FIELD(name) PFN_vk##name vk##name = ::vk##name;
    PUI_VK_INSTANCE_FUNCTIONS(PUI_VK_FIELD)
    PUI_VK_DEVICE_FUNCTIONS(PUI_VK_FIELD)
#undef PUI_VK_FIELD
    void load_global(PFN_vkGetInstanceProcAddr loader)
    {
        if (loader) vkGetInstanceProcAddr = loader;
        vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
            vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    }
    void load_instance(VkInstance instance)
    {
#define PUI_VK_LOAD(name)                                                                          \
    vk##name = reinterpret_cast<PFN_vk##name>(vkGetInstanceProcAddr(instance, "vk" #name));
        PUI_VK_INSTANCE_FUNCTIONS(PUI_VK_LOAD)
#undef PUI_VK_LOAD
    }
    void load_device(VkDevice device, PFN_vkGetDeviceProcAddr loader)
    {
        if (loader) vkGetDeviceProcAddr = loader;
#define PUI_VK_LOAD(name)                                                                          \
    vk##name = reinterpret_cast<PFN_vk##name>(vkGetDeviceProcAddr(device, "vk" #name));
        PUI_VK_DEVICE_FUNCTIONS(PUI_VK_LOAD)
#undef PUI_VK_LOAD
    }
};
#undef PUI_VK_INSTANCE_FUNCTIONS
#undef PUI_VK_DEVICE_FUNCTIONS
} // namespace vk_detail
#endif
