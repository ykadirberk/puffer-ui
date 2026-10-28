# Optional native renderer, independent of SDL_Renderer's Vulkan implementation.
if(PUFFERUI_WITH_VULKAN)
  find_package(Vulkan 1.3 REQUIRED)
  if(PUFFERUI_WITH_SDL3)
    pui_add_library(pufferui_vulkan SDL3)
  else()
    pui_add_library(pufferui_vulkan)
  endif()
  target_compile_definitions(pufferui_vulkan PUBLIC PUFFERUI_ENABLE_VULKAN)
  target_link_libraries(pufferui_vulkan PUBLIC Vulkan::Vulkan)
  if(PUFFERUI_WITH_HARFBUZZ)
    target_compile_definitions(pufferui_vulkan PUBLIC PUFFERUI_ENABLE_HARFBUZZ)
    target_link_libraries(pufferui_vulkan PUBLIC
      $<BUILD_INTERFACE:harfbuzz-gpu> $<INSTALL_INTERFACE:harfbuzz::harfbuzz-gpu>)
  endif()
  set_target_properties(pufferui_vulkan PROPERTIES EXPORT_NAME vulkan)
  add_library(pufferui::vulkan ALIAS pufferui_vulkan)
  install(TARGETS pufferui_vulkan EXPORT pufferuiVulkanTargets)
  install(FILES "docs/licenses/harfbuzz-slug.txt"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/pufferui/licenses")
  install(EXPORT pufferuiVulkanTargets NAMESPACE pufferui::
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pufferui")
endif()
