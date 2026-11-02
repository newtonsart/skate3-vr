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
