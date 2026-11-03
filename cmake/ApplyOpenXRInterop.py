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

    edit("include/rex/ui/overlay/install_wizard_overlay.h",
         " protected:\n  void OnClose() override;",
         """  void BeginInstall(std::filesystem::path source, bool finish_when_done) {
    finish_when_done_ = finish_when_done;
    if (!source.empty()) StartInstall(std::move(source));
  }
 protected:
  void OnClose() override;""")
    edit("include/rex/ui/overlay/install_wizard_overlay.h",
         "  int launch_frames_ = -1;",
         "  int launch_frames_ = -1;\n  bool finish_when_done_ = false;")
    edit("src/ui/overlay/install_wizard_overlay.cpp",
         "void InstallWizardDialog::OnDraw(ImGuiIO& io) {\n  FinishInstallIfNeeded();",
         """void InstallWizardDialog::OnDraw(ImGuiIO& io) {
  FinishInstallIfNeeded();
  if (finish_when_done_ && state_ == State::kInstalled && launch_frames_ < 0)
    launch_frames_ = 1;""")
    edit("include/rex/ui/overlay/simple_settings_overlay.h",
         "  bool visible() const { return visible_; }",
         "  bool visible() const { return visible_; }\n"
         "  void SetLiveEditor(std::function<bool(ImGuiIO&)> draw) { live_editor_ = std::move(draw); }")
    edit("include/rex/ui/overlay/simple_settings_overlay.h",
         "  bool visible_ = false;",
         "  bool visible_ = false;\n  std::function<bool(ImGuiIO&)> live_editor_;")
    edit("src/ui/overlay/simple_settings_overlay.cpp",
         "void SimpleSettingsDialog::NavigateBack() {",
         """void SimpleSettingsDialog::NavigateBack() {
  if (HasCvar("skate3_vr_edit_mode") && rex::cvar::Query<int32_t>("skate3_vr_edit_mode") != 0) {
    rex::cvar::SetFlagByName("skate3_vr_edit_mode", "0");
    return;
  }""")
    edit("src/ui/overlay/simple_settings_overlay.cpp",
         "void SimpleSettingsDialog::OnDraw(ImGuiIO& io) {\n  if (!visible_) {\n    return;\n  }",
         """void SimpleSettingsDialog::OnDraw(ImGuiIO& io) {
  if (!visible_) {
    return;
  }
  if (live_editor_ && live_editor_(io)) {
    // Keep input edges current while the compact editor owns the controls.
    (void)GatherInput(io);
    just_shown_ = true;
    return;
  }""")
    edit("include/rex/ui/overlay/simple_settings_overlay.h",
         "  bool renderer_native_ = true;",
         "  bool renderer_native_ = true;\n"
         "  int vr_mode_ = 0;\n"
         "  int vr_resolution_index_ = 0;\n"
         "  float vr_window_width_ = 2.4f, vr_window_distance_ = 2.0f, vr_window_height_ = 0.0f;\n"
         "  float vr_camera_distance_ = 0, vr_camera_height_ = 0, vr_world_scale_ = 1;\n"
         "  float vr_hud_width_ = 1.6f, vr_hud_distance_ = 2.0f;\n"
         "  bool vr_pause_in_world_ = true;\n"
         "  int vr_window_edges_index_ = 0;")
    edit("src/ui/overlay/simple_settings_overlay.cpp",
         "constexpr std::array<std::string_view, 24> kOptionalSimpleSettingsCvars = {",
         "constexpr std::array<std::string_view, 36> kOptionalSimpleSettingsCvars = {")
    edit("src/ui/overlay/simple_settings_overlay.cpp",
         '    "user_language"};',
         '    "user_language",\n'
         '    "skate3_vr_mode",\n'
         '    "skate3_vr_window_width",\n'
         '    "skate3_vr_window_distance",\n'
         '    "skate3_vr_window_height",\n'
         '    "skate3_vr_camera_distance",\n'
         '    "skate3_vr_camera_height",\n'
         '    "skate3_vr_world_scale",\n'
         '    "skate3_vr_hud_width",\n'
         '    "skate3_vr_hud_distance",\n'
         '    "skate3_vr_pause_in_world",\n'
         '    "skate3_vr_render_scale",\n'
         '    "skate3_vr_window_feather"};')
    edit("src/ui/overlay/simple_settings_overlay.cpp",
         '    case 0: {  // Video\n      header("Display");',
         '''    case 0: {  // Video
      if (HasCvar("skate3_vr_mode") && HasCvar("skate3_vr") &&
          (rex::cvar::Query<bool>("skate3_vr") ||
           rex::cvar::Query<bool>("skate3_vr_settings_test"))) {
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
        row.reset = [this] {
          vr_mode_ = 0;
          rex::cvar::SetFlagByName("skate3_vr_mode", "0");
          SaveSimpleSettingsConfig(config_path_);
        };
        rows.push_back(std::move(row));
        RowSpec editor;
        editor.kind = RowSpec::kAction;
        editor.label = vr_mode_ == 0 ? "Adjust camera" : "Move window";
        editor.desc = vr_mode_ == 0
            ? "Preview the world while adjusting the camera. Left stick: height; right stick: distance. A applies; B cancels."
            : "Place the window in space. Left stick: move/height; right stick: distance; LB/RB: size. A places; B cancels.";
        editor.action = [this] { rex::cvar::SetFlagByName("skate3_vr_edit_mode", vr_mode_ == 0 ? "2" : "1"); };
        rows.push_back(std::move(editor));
        static constexpr std::array<double, 4> vr_scales = {0.0, 0.75, 1.0, 1.25};
        const double scale = rex::cvar::Query<double>("skate3_vr_render_scale");
        vr_resolution_index_ = 0;
        for (int i = 1; i < 4; ++i)
          if (std::abs(vr_scales[i] - scale) < std::abs(vr_scales[vr_resolution_index_] - scale))
            vr_resolution_index_ = i;
        RowSpec quality;
        quality.label = "VR Render Resolution";
        quality.desc = "Render directly at the headset's recommended resolution instead of stretching the desktop image. "
                       "HMD 100% gives one source pixel per recommended eye pixel. Higher values cost more GPU time. "
                       "Desktop restores the previous rendering path. Applies after the next frame.";
        quality.options = {"Desktop (compatibility)", "HMD 75%", "HMD 100%", "HMD 125%"};
        quality.index = &vr_resolution_index_;
        quality.on_enum_change = [this](int value) {
          vr_resolution_index_ = value;
          rex::cvar::SetFlagByName("skate3_vr_render_scale", std::to_string(vr_scales[value]));
          SaveSimpleSettingsConfig(config_path_);
        };
        quality.reset = [this] {
          vr_resolution_index_ = 0;
          rex::cvar::SetFlagByName("skate3_vr_render_scale", "0");
          SaveSimpleSettingsConfig(config_path_);
        };
        rows.push_back(std::move(quality));
        auto vr_slider = [&](const char* label, const char* name, float* value,
                             float minimum, float maximum, bool enabled,
                             const char* desc, const char* fmt = "%.1f m") {
          *value = float(rex::cvar::Query<double>(name));
          RowSpec slider;
          slider.kind = RowSpec::kSlider;
          slider.label = label;
          slider.desc = desc;
          slider.enabled = enabled;
          slider.value = value;
          slider.min = minimum; slider.max = maximum;
          slider.step = 0.1f; slider.fmt = fmt;
          slider.on_value_change = [this, name, value] {
            rex::cvar::SetFlagByName(name, std::to_string(*value));
            SaveSimpleSettingsConfig(config_path_);
          };
          slider.reset = [this, name, value] {
            *value = float(CvarDefaultDouble(name, 0.0));
            rex::cvar::SetFlagByName(name, std::to_string(*value));
            SaveSimpleSettingsConfig(config_path_);
          };
          rows.push_back(std::move(slider));
        };
        const char* window_desc = "Applies immediately to the floating window. Ctrl+F8 recenters it.";
        vr_slider("VR Window Width", "skate3_vr_window_width", &vr_window_width_, 0.5f, 6.0f, vr_mode_ != 0, window_desc);
        vr_slider("VR Window Distance", "skate3_vr_window_distance", &vr_window_distance_, 0.5f, 8.0f, vr_mode_ != 0, window_desc);
        vr_slider("VR Window Height", "skate3_vr_window_height", &vr_window_height_, -2.0f, 2.0f, vr_mode_ != 0, window_desc);
        static constexpr std::array<double, 3> edge_widths = {0.0, 0.04, 0.08};
        const double feather = rex::cvar::Query<double>("skate3_vr_window_feather");
        vr_window_edges_index_ = 0;
        for (int i = 1; i < 3; ++i)
          if (std::abs(edge_widths[i] - feather) < std::abs(edge_widths[vr_window_edges_index_] - feather))
            vr_window_edges_index_ = i;
        RowSpec edges;
        edges.label = "VR Window Edges";
        edges.desc = "Fade the window edges into the room. The centre stays opaque. Requires active passthrough; the outer HUD may fade too.";
        edges.options = {"Sharp", "Soft", "Wide"};
        edges.index = &vr_window_edges_index_; edges.enabled = vr_mode_ == 2;
        edges.on_enum_change = [this](int value) {
          vr_window_edges_index_ = value;
          rex::cvar::SetFlagByName("skate3_vr_window_feather", std::to_string(edge_widths[value]));
          SaveSimpleSettingsConfig(config_path_);
        };
        edges.reset = [this] {
          vr_window_edges_index_ = 0;
          rex::cvar::SetFlagByName("skate3_vr_window_feather", "0");
          SaveSimpleSettingsConfig(config_path_);
        };
        rows.push_back(std::move(edges));
        header("VR Camera and HUD");
        vr_pause_in_world_ = rex::cvar::Query<bool>("skate3_vr_pause_in_world");
        RowSpec pause;
        pause.label = "VR Pause View";
        pause.desc = "Keep the live stereo world and head tracking behind the pause menu. Flat panel is available for compatibility. Loading screens still use their own panel.";
        pause.options = {"Flat panel", "Menu over 3D world"}; pause.flag = &vr_pause_in_world_;
        pause.on_enum_change = [this](int value) {
          vr_pause_in_world_ = value != 0;
          rex::cvar::SetFlagByName("skate3_vr_pause_in_world", value ? "true" : "false");
          SaveSimpleSettingsConfig(config_path_);
        };
        pause.reset = [this] {
          vr_pause_in_world_ = true;
          rex::cvar::SetFlagByName("skate3_vr_pause_in_world", "true");
          SaveSimpleSettingsConfig(config_path_);
        };
        rows.push_back(std::move(pause));
        vr_slider("VR Camera Distance", "skate3_vr_camera_distance", &vr_camera_distance_, -2.0f, 8.0f, vr_mode_ == 0,
                  "Moves the immersive camera farther behind the skater. Zero keeps the original follow-camera position. Negative values move closer.");
        vr_slider("VR Camera Height", "skate3_vr_camera_height", &vr_camera_height_, -2.0f, 4.0f, vr_mode_ == 0,
                  "Raises or lowers the immersive camera relative to the game's original camera. Zero keeps the original height.");
        vr_slider("VR World Scale", "skate3_vr_world_scale", &vr_world_scale_, 0.1f, 3.0f, true,
                  "Game units per physical meter. Changes perceived world size and the response to positional head movement.", "%.1f");
        vr_slider("VR HUD Width", "skate3_vr_hud_width", &vr_hud_width_, 0.5f, 3.0f, vr_mode_ == 0,
                  "Width of the immersive HUD. In window mode, the HUD fits the window instead.");
        vr_slider("VR HUD Distance", "skate3_vr_hud_distance", &vr_hud_distance_, 0.5f, 5.0f, vr_mode_ == 0,
                  "Comfortable convergence distance of the immersive HUD in front of you.");
        RowSpec recenter;
        recenter.kind = RowSpec::kAction; recenter.label = "Recenter VR";
        recenter.desc = "Look forward and confirm to recenter head tracking and place the window in front of you.";
        recenter.action = [] { rex::cvar::SetFlagByName("skate3_vr_recenter_requested", "true"); };
        rows.push_back(std::move(recenter));
      }
      header("Display");''')

    edit("include/rex/ui/imgui_drawer.h", "  void Draw(UIDrawContext& ui_draw_context) override;", """  void Draw(UIDrawContext& ui_draw_context) override;
  void SetDrawMirror(std::function<UIDrawContext*(UIDrawContext&)> begin,
                     std::function<void()> end) {
    mirror_begin_ = std::move(begin); mirror_end_ = std::move(end);
  }""")
    edit("include/rex/ui/imgui_drawer.h", "  Presenter* presenter_ = nullptr;", """  Presenter* presenter_ = nullptr;
  std::function<UIDrawContext*(UIDrawContext&)> mirror_begin_;
  std::function<void()> mirror_end_;""")
    edit("src/ui/imgui_drawer.cpp", "    RenderDrawLists(draw_data, ui_draw_context);", """    RenderDrawLists(draw_data, ui_draw_context);
    if (mirror_begin_ && mirror_end_) {
      if (auto* mirror = mirror_begin_(ui_draw_context)) {
        RenderDrawLists(draw_data, *mirror);
        mirror_end_();
      }
    }""")
    edit("include/rex/ui/vulkan/presenter.h", "class VulkanPresenter final : public Presenter {\n public:", """class VulkanPresenter final : public Presenter {
 public:
  struct UiMirrorHooks {
    void* user = nullptr;
    VkCommandBuffer (*commands)(void*) = nullptr;
    void (*submitted)(void*, VkFence, VkResult) = nullptr;
  };
  // UI thread only. The extra pass shares the parent's upload lifetime/fence.
  static void SetUiMirrorHooks(UiMirrorHooks hooks);""")
    edit("src/ui/vulkan/vulkan_presenter.cpp", "struct VulkanPresentTimingStats {", """VulkanPresenter::UiMirrorHooks ui_mirror_hooks;
struct VulkanPresentTimingStats {""")
    edit("src/ui/vulkan/vulkan_presenter.cpp", "bool VulkanPresenter::CaptureGuestOutput(RawImage& image_out)", """void VulkanPresenter::SetUiMirrorHooks(UiMirrorHooks hooks) { ui_mirror_hooks = hooks; }

bool VulkanPresenter::CaptureGuestOutput(RawImage& image_out)""")
    edit("src/ui/vulkan/vulkan_presenter.cpp", "  VkCommandBuffer command_buffers[2];", "  VkCommandBuffer command_buffers[3];")
    edit("src/ui/vulkan/vulkan_presenter.cpp", "  command_buffers[command_buffer_count++] = draw_command_buffer;", """  command_buffers[command_buffer_count++] = draw_command_buffer;
  VkCommandBuffer mirror_command = execute_ui_drawers && ui_mirror_hooks.commands
      ? ui_mirror_hooks.commands(ui_mirror_hooks.user) : VK_NULL_HANDLE;
  if (mirror_command) command_buffers[command_buffer_count++] = mirror_command;""")
    edit("src/ui/vulkan/vulkan_presenter.cpp", "    if (submit_result != VK_SUCCESS) {\n      REXLOG_ERROR(\"VulkanPresenter: Failed to submit command buffers ({})\",", """    if (mirror_command && ui_mirror_hooks.submitted)
      ui_mirror_hooks.submitted(ui_mirror_hooks.user, fence_acqusition.fence(), submit_result);
    if (submit_result != VK_SUCCESS) {
      REXLOG_ERROR("VulkanPresenter: Failed to submit command buffers ({})",""")

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

    edit("include/rex/graphics/vulkan/native_rhi_vulkan.h",
         "}  // namespace rex::graphics::vulkan",
         "void NativeRhiSetOutputSizeOverride(uint32_t width, uint32_t height);\n"
         "void NativeRhiApplyOutputSizeOverride(uint32_t& width, uint32_t& height);\n\n"
         "uint32_t NativeRhiOriginalOutputHeight();\n\n"
         "}  // namespace rex::graphics::vulkan")
    edit("src/graphics/vulkan/native_rhi_vulkan.cpp", "#include <chrono>", "#include <chrono>\n#include <atomic>")
    edit("src/graphics/vulkan/native_rhi_vulkan.cpp",
         "}  // namespace rex::graphics::vulkan", """namespace {
std::atomic<uint64_t> native_output_override{0};
std::atomic<uint32_t> native_output_original_height{0};
}
void NativeRhiSetOutputSizeOverride(uint32_t width, uint32_t height) {
  const uint64_t size = width && height ? (uint64_t(width) << 32) | height : 0;
  if (native_output_override.exchange(size, std::memory_order_relaxed) != size)
    REXLOG_INFO("Native Vulkan: output override {}x{} (0 disables)", width, height);
}
void NativeRhiApplyOutputSizeOverride(uint32_t& width, uint32_t& height) {
  native_output_original_height.store(height, std::memory_order_relaxed);
  const auto size = native_output_override.load(std::memory_order_relaxed);
  if (size) { width = uint32_t(size >> 32); height = uint32_t(size); }
}
uint32_t NativeRhiOriginalOutputHeight() {
  return native_output_original_height.load(std::memory_order_relaxed);
}

}  // namespace rex::graphics::vulkan""")
    edit("src/graphics/vulkan/command_processor.cpp",
         "  presenter->RefreshGuestOutput(\n      guest_output_width, guest_output_height, display_width, display_height,",
         "  NativeRhiApplyOutputSizeOverride(guest_output_width, guest_output_height);\n"
         "  presenter->RefreshGuestOutput(\n      guest_output_width, guest_output_height, display_width, display_height,")


if __name__ == "__main__":
    apply(Path(sys.argv[1]))
