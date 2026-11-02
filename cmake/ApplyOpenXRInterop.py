"""Apply the pinned ReXGlue Vulkan interop patch to a disposable SDK copy."""
from pathlib import Path
import sys


def apply(sdk):
    if (sdk / ".git").exists():
        raise ValueError("Use a disposable SDK copy (REXSDK_DIR), not the source submodule")
    def edit(name, old, new):
        path = sdk / name
        text = path.read_text(encoding="utf-8")
        if new in text:
            return
        if text.count(old) != 1:
            raise ValueError(f"Pinned SDK patch anchor mismatch: {name}: {old[:65]}")
        path.write_text(text.replace(old, new), encoding="utf-8")

    edit("include/rex/ui/overlay/simple_settings_overlay.h",
         "  bool renderer_native_ = true;",
         "  bool renderer_native_ = true;\n"
         "  int vr_mode_ = 0;\n"
         "  float vr_window_width_ = 2.4f, vr_window_distance_ = 2.0f, vr_window_height_ = 0.0f;")
    edit("src/ui/overlay/simple_settings_overlay.cpp",
         "constexpr std::array<std::string_view, 24> kOptionalSimpleSettingsCvars = {",
         "constexpr std::array<std::string_view, 28> kOptionalSimpleSettingsCvars = {")
    edit("src/ui/overlay/simple_settings_overlay.cpp",
         '    "user_language"};',
         '    "user_language",\n'
         '    "skate3_vr_mode",\n'
         '    "skate3_vr_window_width",\n'
         '    "skate3_vr_window_distance",\n'
         '    "skate3_vr_window_height"};')
    edit("src/ui/overlay/simple_settings_overlay.cpp",
         '    case 0: {  // Video\n      header("Display");',
         '''    case 0: {  // Video
      if (HasCvar("skate3_vr_mode") && HasCvar("skate3_vr") &&
          rex::cvar::Query<bool>("skate3_vr")) {
        vr_mode_ = rex::cvar::Query<int32_t>("skate3_vr_mode");
        RowSpec row;
        row.label = "VR View";
        row.desc = "Choose immersive VR or a floating stereoscopic window. Room view "
                   "requires passthrough support from your headset and PC runtime. "
                   "Ctrl+F11 cycles modes; Ctrl+F8 places the window in front of you.";
        if (HasCvar("skate3_vr_status"))
          row.desc_extra = rex::cvar::Query<std::string>("skate3_vr_status");
        row.options = {"Immersive VR", "3D window", "3D window + room view"};
        row.index = &vr_mode_;
        row.on_enum_change = [this](int value) {
          vr_mode_ = value;
          rex::cvar::SetFlagByName("skate3_vr_mode", std::to_string(value));
          SaveSimpleSettingsConfig(config_path_);
        };
        rows.push_back(std::move(row));
        auto window_slider = [&](const char* label, const char* name, float* value,
                                 float minimum, float maximum) {
          *value = float(rex::cvar::Query<double>(name));
          RowSpec slider;
          slider.kind = RowSpec::kSlider;
          slider.label = label;
          slider.desc = "Applies immediately to the floating window. Ctrl+F8 recenters it.";
          slider.enabled = vr_mode_ != 0;
          slider.value = value;
          slider.min = minimum; slider.max = maximum;
          slider.step = 0.1f; slider.fmt = "%.1f m";
          slider.on_value_change = [this, name, value] {
            rex::cvar::SetFlagByName(name, std::to_string(*value));
            SaveSimpleSettingsConfig(config_path_);
          };
          rows.push_back(std::move(slider));
        };
        window_slider("VR Window Width", "skate3_vr_window_width", &vr_window_width_, 0.5f, 6.0f);
        window_slider("VR Window Distance", "skate3_vr_window_distance", &vr_window_distance_, 0.5f, 8.0f);
        window_slider("VR Window Height", "skate3_vr_window_height", &vr_window_height_, -2.0f, 2.0f);
      }
      header("Display");''')

    edit("include/rex/ui/vulkan/instance.h", "class VulkanInstance {", """// OpenXR interop: installed before graphics creation, immutable while in use.
struct RuntimeHooks {
  void* user = nullptr;
  VkResult (*create_instance)(void*, PFN_vkGetInstanceProcAddr, const VkInstanceCreateInfo*, VkInstance*) = nullptr;
  VkPhysicalDevice (*select_gpu)(void*, VkInstance) = nullptr;
  VkResult (*create_device)(void*, PFN_vkGetInstanceProcAddr, VkPhysicalDevice, const VkDeviceCreateInfo*, VkDevice*) = nullptr;
};
void SetRuntimeHooks(RuntimeHooks hooks);
RuntimeHooks GetRuntimeHooks();

class VulkanInstance {""")
    edit("src/ui/vulkan/vulkan_instance.cpp", "std::unique_ptr<VulkanInstance> VulkanInstance::Create", """namespace { RuntimeHooks runtime_hooks; }
void SetRuntimeHooks(RuntimeHooks hooks) { runtime_hooks = hooks; }
RuntimeHooks GetRuntimeHooks() { return runtime_hooks; }

std::unique_ptr<VulkanInstance> VulkanInstance::Create""")
    edit("src/ui/vulkan/vulkan_instance.cpp", "  VkResult instance_create_result =\n      ifn.vkCreateInstance(&instance_create_info, nullptr, &vulkan_instance->instance_);", """  const auto create_instance = [&]() {
    const auto hooks = GetRuntimeHooks();
    return hooks.create_instance
        ? hooks.create_instance(hooks.user, ifn.vkGetInstanceProcAddr, &instance_create_info, &vulkan_instance->instance_)
        : ifn.vkCreateInstance(&instance_create_info, nullptr, &vulkan_instance->instance_);
  };
  VkResult instance_create_result = create_instance();""")
    edit("src/ui/vulkan/vulkan_instance.cpp", "    instance_create_result =\n        ifn.vkCreateInstance(&instance_create_info, nullptr, &vulkan_instance->instance_);", "    instance_create_result = create_instance();")
    edit("src/ui/vulkan/vulkan_provider.cpp", "  provider->vulkan_instance_->EnumeratePhysicalDevices(physical_devices);", """  provider->vulkan_instance_->EnumeratePhysicalDevices(physical_devices);
  const auto runtime_hooks = GetRuntimeHooks();
  if (runtime_hooks.select_gpu) {
    const auto selected = runtime_hooks.select_gpu(runtime_hooks.user, provider->vulkan_instance_->instance());
    if (!selected || std::find(physical_devices.begin(), physical_devices.end(), selected) == physical_devices.end()) {
      REXLOG_ERROR("OpenXR runtime GPU is unavailable");
      return nullptr;
    }
    physical_devices = {selected};
  }""")
    edit("src/ui/vulkan/vulkan_device.cpp", "  const VkResult device_create_result =\n      ifn.vkCreateDevice(physical_device, &device_create_info, nullptr, &device->device_);", """  const auto runtime_hooks = GetRuntimeHooks();
  // Meta adds a timeline feature node: use individual 1.2 nodes to avoid
  // an invalid aggregate Vulkan12Features + timeline feature combination.
  VkPhysicalDeviceUniformBufferStandardLayoutFeatures xr_uniform{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES};
  VkPhysicalDeviceScalarBlockLayoutFeatures xr_scalar{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES};
  VkPhysicalDeviceTimelineSemaphoreFeatures xr_timeline{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  if (runtime_hooks.create_device) {
    xr_uniform.uniformBufferStandardLayout = features_1_2.enabled.uniformBufferStandardLayout;
    xr_scalar.scalarBlockLayout = features_1_2.enabled.scalarBlockLayout;
    xr_timeline.timelineSemaphore = features_1_2.supported.timelineSemaphore;
    xr_uniform.pNext = const_cast<void*>(device_create_info.pNext);
    xr_scalar.pNext = &xr_uniform;
    xr_timeline.pNext = &xr_scalar;
    device_create_info.pNext = &xr_timeline;
  }
  const VkResult device_create_result = runtime_hooks.create_device
      ? runtime_hooks.create_device(runtime_hooks.user, ifn.vkGetInstanceProcAddr, physical_device, &device_create_info, &device->device_)
      : ifn.vkCreateDevice(physical_device, &device_create_info, nullptr, &device->device_);""")
    edit("src/ui/vulkan/vulkan_device.cpp",
         "      features_1_2.Link(supported_features_2, device_create_info);",
         "      features_1_2.Link(supported_features_2, device_create_info);\n"
         "      if (GetRuntimeHooks().create_device) device_create_info.pNext = features_1_2.enabled.pNext;")
    edit("src/ui/vulkan/vulkan_device.cpp",
         "    XE_UI_VULKAN_LOCAL_PROMOTED_EXTENSION(KHR_sampler_mirror_clamp_to_edge, 1, 2)",
         "    XE_UI_VULKAN_LOCAL_PROMOTED_EXTENSION(KHR_sampler_mirror_clamp_to_edge, 1, 2)\n"
         "    if (GetRuntimeHooks().create_device) requested_extensions.emplace(\"VK_KHR_sampler_mirror_clamp_to_edge\", &ext_1_2_KHR_sampler_mirror_clamp_to_edge);")
    edit("src/ui/vulkan/vulkan_device.cpp",
         "    XE_UI_VULKAN_FEATURE(sampleRateShading)",
         "    XE_UI_VULKAN_FEATURE(sampleRateShading)\n"
         "    enabled_features.shaderStorageImageMultisample = supported_features.shaderStorageImageMultisample;")
    edit("include/rex/graphics/vulkan/command_processor.h", "  // Returns the deferred drawing command list", """  // OpenXR image release requires all recorded writes to have completed.
  bool FlushNativeForExternal() {
    if (!EndSubmission(false) || !AwaitAllQueueOperationsCompletion()) return false;
    return BeginSubmission(false);
  }

  // Returns the deferred drawing command list""")
    edit("include/rex/graphics/vulkan/native_rhi_vulkan.h", "class VulkanCommandProcessor;", """class VulkanCommandProcessor;
struct NativeVulkanImage { VkImage image; VkImageView view; };
NativeVulkanImage NativeRhiGetImage(nrhi::Texture* texture);
bool NativeRhiFlushForExternal(nrhi::Device* device);
void* NativeRhiGetVulkanDevice(nrhi::Device* device);""")
    edit("src/graphics/vulkan/native_rhi_vulkan.cpp", "nrhi::Device* CreateNativeRhiDevice(VulkanCommandProcessor* command_processor) {", """NativeVulkanImage NativeRhiGetImage(nrhi::Texture* texture) {
  auto* t = static_cast<NrTextureVulkan*>(texture);
  return {t->image, t->attachment_view};
}
bool NativeRhiFlushForExternal(nrhi::Device* device) {
  auto* d = static_cast<NrDeviceVulkan*>(device);
  d->EndFrame();
  return d->cp()->FlushNativeForExternal();
}
void* NativeRhiGetVulkanDevice(nrhi::Device* device) {
  return static_cast<NrDeviceVulkan*>(device)->cp()->GetVulkanDevice();
}

nrhi::Device* CreateNativeRhiDevice(VulkanCommandProcessor* command_processor) {""")
    edit("src/graphics/vulkan/native_rhi_vulkan.cpp",
         "  nrhi::PrimitiveTopology topology_ = nrhi::PrimitiveTopology::kTriangleList;",
         "  nrhi::PrimitiveTopology topology_ = nrhi::PrimitiveTopology::kTriangleList;\n"
         "  uint32_t index_capacity_ = 0;")
    edit("src/graphics/vulkan/native_rhi_vulkan.cpp",
         "void NrCmdVulkan::SetIndexBuffer(nrhi::Buffer* buffer, uint64_t offset, uint32_t size_bytes) {\n  (void)size_bytes;",
         "void NrCmdVulkan::SetIndexBuffer(nrhi::Buffer* buffer, uint64_t offset, uint32_t size_bytes) {\n"
         "  const uint64_t available = offset < buffer->size() ? buffer->size() - offset : 0;\n"
         "  index_capacity_ = uint32_t(std::min<uint64_t>(size_bytes, available) / sizeof(uint16_t));\n"
         "  if (!index_capacity_) return;")
    edit("src/graphics/vulkan/native_rhi_vulkan.cpp",
         "void NrCmdVulkan::ResetFrameState() {\n  layout_ = nullptr;",
         "void NrCmdVulkan::ResetFrameState() {\n  index_capacity_ = 0;\n  layout_ = nullptr;")
    edit("include/rex/graphics/vulkan/native_rhi_vulkan.h",
         "void NativeRhiEndFrame(nrhi::Device* device);",
         "void NativeRhiEndFrame(nrhi::Device* device);\n"
         "void NativeRhiTimestamp(nrhi::Device* device, VkQueryPool pool, uint32_t index, bool begin);")
    edit("src/graphics/vulkan/native_rhi_vulkan.cpp",
         "void NativeRhiEndFrame(nrhi::Device* device) {",
         """void NativeRhiTimestamp(nrhi::Device* device, VkQueryPool pool, uint32_t index, bool begin) {
  auto* d = static_cast<NrDeviceVulkan*>(device);
  d->EndFrame();
  auto& cmd = d->cp()->deferred_command_buffer();
  if (begin) cmd.CmdVkResetQueryPool(pool, index, 2);
  cmd.CmdVkWriteTimestamp(begin ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                          pool, index);
}

void NativeRhiEndFrame(nrhi::Device* device) {""")
    edit("src/graphics/vulkan/native_rhi_vulkan.cpp",
         "void NrCmdVulkan::DrawIndexed(uint32_t index_count, uint32_t start_index, int32_t base_vertex) {\n  if (!EnsureDrawState()) return;",
         """void NrCmdVulkan::DrawIndexed(uint32_t index_count, uint32_t start_index, int32_t base_vertex) {
  // A streamed mesh may still have its previous index buffer until decode commits.
  if (uint64_t(start_index) + index_count > index_capacity_) {
    static uint64_t rejected = 0;
    if (++rejected <= 8 || rejected % 1024 == 0)
      REXLOG_WARN("Native Vulkan: rejected stale mesh draw start={} count={} capacity={} (n={})",
                  start_index, index_count, index_capacity_, rejected);
    return;
  }
  if (!EnsureDrawState()) return;""")


if __name__ == "__main__":
    apply(Path(sys.argv[1]))
