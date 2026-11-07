#define VK_ENABLE_BETA_EXTENSIONS
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "game_vr.h"
#include "openxr_context.h"
#include "openxr_session.h"
#include "openxr_gamepad.h"
#include "controller_driver.h"
#include "simulated_route.h"
#include "openxr_passthrough.h"
#include "settings_surface.h"
#include "editor_controls.h"
#include "editor.h"
#include "render_resolution.h"
#include "window_blend.h"
#include "window_facing.h"
#include "presentation.h"
#include "stereo_math.h"
#include "cull_frustum.h"
#include "gpu_timing.h"
#include "perf_trace.h"
#include "vr_color_spirv.h"
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/kernel/guest_presence.h>
#include <rex/kernel/xam/private.h>
#include <rex/graphics/vulkan/native_rhi_vulkan.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

REXCVAR_DEFINE_BOOL(skate3_vr, false, "VR", "Experimental native OpenXR stereo rendering")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_DOUBLE(skate3_vr_world_scale, 1.0, "VR", "Game units per physical meter");
REXCVAR_DEFINE_DOUBLE(skate3_vr_render_scale, 0.0, "VR", "Eye render scale relative to OpenXR recommendation; 0 keeps desktop resolution").range(0.0,1.25);
REXCVAR_DEFINE_BOOL(skate3_vr_resolution_test, false, "VR", "Desktop-only target-size diagnostic using the recorded 2272x2464 recommendation")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly).debug_only();
REXCVAR_DEFINE_DOUBLE(skate3_vr_camera_distance, 0.0, "VR", "Additional immersive camera distance in meters").range(-2.0,8.0);
REXCVAR_DEFINE_DOUBLE(skate3_vr_camera_height, 0.0, "VR", "Additional immersive camera height in meters").range(-2.0,4.0);
REXCVAR_DEFINE_BOOL(skate3_vr_recenter_requested, false, "VR", "Recenter on the next tracked frame");
REXCVAR_DEFINE_BOOL(skate3_vr_pause_in_world, true, "VR", "Keep stereo world rendering behind the pause menu");
REXCVAR_DEFINE_BOOL(skate3_vr_settings_test, false, "VR", "Open and exercise the VR settings surface without a headset")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly).debug_only();
REXCVAR_DEFINE_INT32(skate3_vr_mode, 0, "VR", "0: immersive, 1: stereo window, 2: stereo window with room view").range(0,2);
REXCVAR_DEFINE_DOUBLE(skate3_vr_window_width, 2.4, "VR", "Stereo window width in meters").range(0.5,6.0);
REXCVAR_DEFINE_DOUBLE(skate3_vr_window_distance, 2.0, "VR", "Stereo window distance in meters").range(0.5,8.0);
REXCVAR_DEFINE_DOUBLE(skate3_vr_window_height, 0.0, "VR", "Stereo window height relative to recenter in meters").range(-2.0,2.0);
REXCVAR_DEFINE_DOUBLE(skate3_vr_window_feather, 0.0, "VR", "Soft window edge width as a fraction of panel height; passthrough only").range(0.0,0.15);
REXCVAR_DEFINE_BOOL(skate3_vr_window_face_viewer,true,"VR","Face the viewer while placing the window; confirmation anchors its orientation");
REXCVAR_DEFINE_DOUBLE(skate3_vr_window_depth,1.0,"VR","Stereo strength for the 3D window; 0 flat, 0.5 reduced, 1 screen-calibrated").range(0.0,1.0);
REXCVAR_DEFINE_BOOL(skate3_vr_editor_comfort,true,"VR","Use the original photo camera and a readable window for photo/object editing");
REXCVAR_DEFINE_BOOL(skate3_vr_clear_hud_backdrop,true,"VR","Remove the captured loading fill/spinner group backdrop over the immersive world");
REXCVAR_DEFINE_STRING(skate3_vr_status, "", "VR", "Current room-view availability");
REXCVAR_DEFINE_BOOL(skate3_vr_capture, false, "VR", "Capture one rendered stereo scene to local out/vr-eye-*.ppm");
REXCVAR_DEFINE_BOOL(skate3_vr_capture_once,false,"VR","Capture the next available VR frame, including frontend/loading menus").debug_only();
REXCVAR_DEFINE_DOUBLE(skate3_vr_menu_distance, 2.0, "VR", "Menu panel distance in meters");
REXCVAR_DEFINE_DOUBLE(skate3_vr_menu_width, 2.4, "VR", "Menu panel width in meters");
REXCVAR_DEFINE_DOUBLE(skate3_vr_hud_width, 1.6, "VR", "Readable HUD panel width in meters").range(0.5,3.0);
REXCVAR_DEFINE_DOUBLE(skate3_vr_hud_distance, 2.0, "VR", "HUD convergence distance in meters").range(0.5,5.0);
REXCVAR_DEFINE_BOOL(skate3_vr_linear_color, true, "VR", "Decode display color before OpenXR composition");
REXCVAR_DEFINE_BOOL(skate3_vr_expand_game_frustum, true, "VR", "Expand the matched main-camera culling planes for head turns");
REXCVAR_DEFINE_BOOL(skate3_vr_share_frame_work, true, "VR", "Share the scene copy and light-space shadows between eyes");
REXCVAR_DEFINE_BOOL(skate3_vr_gpu_timing, false, "VR", "Measure GPU render and transfer times per eye")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(skate3_vr_perf_trace, false, "VR", "Log periodic VR frame timing summaries");
REXCVAR_DEFINE_BOOL(skate3_vr_compare_frame_work, false, "VR", "Compare separate/shared eye rendering; simulated eyes without a headset when skate3_vr=false")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(skate3_vr_simulate_route, false, "VR", "Drive a 90 second test route during the no-headset rendering benchmark")
    .debug_only().lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace skate3::vr {
namespace {
using namespace rex::graphics;
void XrCheck(XrResult r,const char* operation) {
  if(XR_FAILED(r)) throw std::runtime_error(std::string(operation)+": XR "+std::to_string(r));
}
void VkCheck(VkResult r,const char* operation) {
  if(r!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+": Vk "+std::to_string(r));
}
template<class T> T Function(XrInstance instance,const char* name) {
  PFN_xrVoidFunction p=nullptr; XrCheck(xrGetInstanceProcAddr(instance,name,&p),name);
  if(!p) throw std::runtime_error(name); return reinterpret_cast<T>(p);
}
struct EyeSwapchain {
  XrSwapchain handle=XR_NULL_HANDLE;
  uint32_t width=0,height=0;
  std::vector<XrSwapchainImageVulkan2KHR> images;
  VkImage color_image=VK_NULL_HANDLE;
  VkDeviceMemory color_memory=VK_NULL_HANDLE;
  VkImageView color_view=VK_NULL_HANDLE;
};
struct State {
  OpenXrContext xr;
  std::unique_ptr<OpenXrSession> session;
  std::unique_ptr<OpenXrGamepad> controllers;
  std::unique_ptr<OpenXrPassthrough> room;
  int last_mode=-1;
  bool settings_presented=false;
  bool settings_dialog=false;
  bool settings_fps_only=false;
  uint32_t settings_generation=0;
  FpsPanelLayout fps_crop;
  std::shared_ptr<native_scene::FrameScene> scene_copy;
  std::shared_ptr<native_scene::FrameScene> CopyScene(const native_scene::FrameScene& source) {
    // Both eyes finish before the next copy; retain nested vector capacity.
    if (!scene_copy) scene_copy=std::make_shared<native_scene::FrameScene>();
    *scene_copy=source;
    return scene_copy;
  }
  int settings_editor=0;
  float settings_aspect=16.0f/9;
  double render_scale=-1;
  void UpdateResolution(bool flat=false) {
    const double scale=flat ? 0.0 : REXCVAR_GET(skate3_vr_render_scale);
    if (scale==render_scale) return;
    const auto& views=xr.views();
    const auto extent=EyeRenderExtent(std::max(views[0].recommendedImageRectWidth,views[1].recommendedImageRectWidth),
        std::max(views[0].recommendedImageRectHeight,views[1].recommendedImageRectHeight),scale);
    vulkan::NativeRhiSetOutputSizeOverride(extent.width,extent.height);
    render_scale=scale;
    REXLOG_INFO("VR: render scale {} target={}x{}; logical panel aspect follows display dimensions",scale,extent.width,extent.height);
  }
  rex::ui::vulkan::VulkanDevice* gpu=nullptr;
  VkDevice device=VK_NULL_HANDLE;
  VkCommandPool pool=VK_NULL_HANDLE;
  VkCommandBuffer command=VK_NULL_HANDLE;
  VkFence fence=VK_NULL_HANDLE;
  VkQueryPool timing=VK_NULL_HANDLE;
  uint32_t timestamp_bits=0;
  float timestamp_period=0;
  double gpu_render_ms[2]{-1,-1},gpu_copy_ms[2]{-1,-1};
  double transfer_submit_ms[3]{},transfer_image_ms[3]{},transfer_fence_ms[3]{};
  uint32_t perf_samples=0;
  std::chrono::steady_clock::time_point route_start{};
  int route_log_second=-1;
  bool route_finished=false;
  PerfTrace trace;
  std::chrono::steady_clock::time_point trace_last_submit{};
  VkDescriptorSetLayout color_layout=VK_NULL_HANDLE;
  VkDescriptorPool color_pool=VK_NULL_HANDLE;
  VkDescriptorSet color_set=VK_NULL_HANDLE;
  VkSampler sampler=VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout=VK_NULL_HANDLE;
  VkPipeline pipeline=VK_NULL_HANDLE;
  EyeSwapchain eyes[3];
  XrPosef reference{};
  bool recentered=false;
  WindowFacing window_facing;
  EditorView last_editor_view=EditorView::Default;
  bool failed=false;
  uint64_t submitted=0;
  VkBuffer readback=VK_NULL_HANDLE;
  VkDeviceMemory readback_memory=VK_NULL_HANDLE;
  VkDeviceSize readback_size=0;
  bool capture_done=false;
  bool capture_menu=false;
  uint64_t eye_hashes[2]{};
  ~State() {
    if(device) {
      auto lock=gpu->AcquireQueue(gpu->queue_family_graphics_compute(),0);
      vkQueueWaitIdle(lock.queue());
      if(pipeline) vkDestroyPipeline(device,pipeline,nullptr);
      if(pipeline_layout) vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
      if(color_pool) vkDestroyDescriptorPool(device,color_pool,nullptr);
      if(color_layout) vkDestroyDescriptorSetLayout(device,color_layout,nullptr);
      if(sampler) vkDestroySampler(device,sampler,nullptr);
      for(auto& eye:eyes) {
        if(eye.color_view) vkDestroyImageView(device,eye.color_view,nullptr);
        if(eye.color_image) vkDestroyImage(device,eye.color_image,nullptr);
        if(eye.color_memory) vkFreeMemory(device,eye.color_memory,nullptr);
        if(eye.handle) xrDestroySwapchain(eye.handle);
      }
      if(fence) vkDestroyFence(device,fence,nullptr);
      if(timing) vkDestroyQueryPool(device,timing,nullptr);
      if(pool) vkDestroyCommandPool(device,pool,nullptr);
      if(readback) vkDestroyBuffer(device,readback,nullptr);
      if(readback_memory) vkFreeMemory(device,readback_memory,nullptr);
      PublishVrControls({});
      PublishWindowMotion({});
      controllers.reset();
      room.reset();
      session.reset();
    }
  }
  void InitializeTiming() {
    if (REXCVAR_GET(skate3_vr_gpu_timing) || REXCVAR_GET(skate3_vr_compare_frame_work)) {
      uint32_t families=0; vkGetPhysicalDeviceQueueFamilyProperties(gpu->physical_device(),&families,nullptr);
      std::vector<VkQueueFamilyProperties> properties(families);
      vkGetPhysicalDeviceQueueFamilyProperties(gpu->physical_device(),&families,properties.data());
      timestamp_bits=properties.at(gpu->queue_family_graphics_compute()).timestampValidBits;
      VkPhysicalDeviceProperties device_properties;
      vkGetPhysicalDeviceProperties(gpu->physical_device(),&device_properties);
      timestamp_period=device_properties.limits.timestampPeriod;
      VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      queries.queryType=VK_QUERY_TYPE_TIMESTAMP; queries.queryCount=8;
      if (!timestamp_bits || vkCreateQueryPool(device,&queries,nullptr,&timing)!=VK_SUCCESS) {
        timing=VK_NULL_HANDLE;
        REXLOG_WARN("VR: GPU timestamp timing unavailable");
      }
    }
  }
  void Bind(rex::ui::vulkan::VulkanDevice* graphics) {
    gpu=graphics; device=gpu->device();
    session=std::make_unique<OpenXrSession>(xr);
    auto& queue=*gpu->queue_families()[gpu->queue_family_graphics_compute()].queues[0];
    session->Bind(gpu->vulkan_instance()->instance(),gpu->physical_device(),device,gpu->queue_family_graphics_compute(),&queue.mutex);
    try {
      controllers=std::make_unique<OpenXrGamepad>(xr.instance(),session->handle());
      REXLOG_INFO("VR: Touch controllers enabled as Xbox player 1 (Menu=Start, Menu+X=Back, left click+tilt=D-pad)");
    } catch(const std::exception& error) { REXLOG_WARN("VR: controller input unavailable: {}",error.what()); }
    room=std::make_unique<OpenXrPassthrough>(xr,session->handle());
    uint32_t count=0;
    XrCheck(xrEnumerateSwapchainFormats(session->handle(),0,&count,nullptr),"Enumerate XR formats");
    std::vector<int64_t> formats(count);
    XrCheck(xrEnumerateSwapchainFormats(session->handle(),count,&count,formats.data()),"Read XR formats");
    VkFormat format=VK_FORMAT_UNDEFINED;
    for(auto candidate:{VK_FORMAT_R8G8B8A8_UNORM}) {
      VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(gpu->physical_device(),candidate,&properties);
      if(std::find(formats.begin(),formats.end(),int64_t(candidate))!=formats.end() &&
          (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {format=candidate;break;}
    }
    if(format==VK_FORMAT_UNDEFINED) throw std::runtime_error("No XR swapchain linear storage format");
    for(int e=0;e<3;++e) {
      auto& eye=eyes[e];
      eye.width=e==2 ? 1920 : xr.views()[e].recommendedImageRectWidth;
      eye.height=e==2 ? 1080 : xr.views()[e].recommendedImageRectHeight;
      XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
      info.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
      info.format=format; info.sampleCount=1; info.width=eye.width; info.height=eye.height;
      info.faceCount=1; info.arraySize=1; info.mipCount=1;
      XrCheck(xrCreateSwapchain(session->handle(),&info,&eye.handle),"Create eye swapchain");
      XrCheck(xrEnumerateSwapchainImages(eye.handle,0,&count,nullptr),"Count eye images");
      eye.images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
      XrCheck(xrEnumerateSwapchainImages(eye.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())),"Read eye images");
      // Meta's XR images lack STORAGE usage even when requested. Compute into
      // our own storage image, then transfer into the runtime-owned image.
      VkImageCreateInfo color{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      color.imageType=VK_IMAGE_TYPE_2D; color.format=format;
      color.extent={eye.width,eye.height,1}; color.mipLevels=1; color.arrayLayers=1;
      color.samples=VK_SAMPLE_COUNT_1_BIT; color.tiling=VK_IMAGE_TILING_OPTIMAL;
      color.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      VkCheck(vkCreateImage(device,&color,nullptr,&eye.color_image),"Create VR color intermediate");
      VkMemoryRequirements memory; vkGetImageMemoryRequirements(device,eye.color_image,&memory);
      VkPhysicalDeviceMemoryProperties types; vkGetPhysicalDeviceMemoryProperties(gpu->physical_device(),&types);
      uint32_t type=0;
      while(type<types.memoryTypeCount && (!(memory.memoryTypeBits&(1u<<type)) ||
          !(types.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) ++type;
      if(type==types.memoryTypeCount) throw std::runtime_error("No VR color device memory");
      VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocation.allocationSize=memory.size; allocation.memoryTypeIndex=type;
      VkCheck(vkAllocateMemory(device,&allocation,nullptr,&eye.color_memory),"Allocate VR color intermediate");
      VkCheck(vkBindImageMemory(device,eye.color_image,eye.color_memory,0),"Bind VR color intermediate");
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image=eye.color_image; view.viewType=VK_IMAGE_VIEW_TYPE_2D; view.format=format;
      view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      VkCheck(vkCreateImageView(device,&view,nullptr,&eye.color_view),"Create VR color view");
    }
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool_info.queueFamilyIndex=gpu->queue_family_graphics_compute();
    VkCheck(vkCreateCommandPool(device,&pool_info,nullptr,&pool),"Create XR command pool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool=pool; alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount=1;
    VkCheck(vkAllocateCommandBuffers(device,&alloc,&command),"Allocate XR transfer commands");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkCheck(vkCreateFence(device,&fi,nullptr,&fence),"Create XR transfer fence");
    InitializeTiming();
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0]={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    bindings[1]={1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout.bindingCount=2; layout.pBindings=bindings;
    VkCheck(vkCreateDescriptorSetLayout(device,&layout,nullptr,&color_layout),"Create VR color layout");
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
    VkDescriptorPoolCreateInfo descriptor_pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; descriptor_pool.maxSets=1; descriptor_pool.poolSizeCount=2; descriptor_pool.pPoolSizes=sizes;
    VkCheck(vkCreateDescriptorPool(device,&descriptor_pool,nullptr,&color_pool),"Create VR color descriptor pool");
    VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; set.descriptorPool=color_pool; set.descriptorSetCount=1; set.pSetLayouts=&color_layout;
    VkCheck(vkAllocateDescriptorSets(device,&set,&color_set),"Allocate VR color descriptors");
    VkSamplerCreateInfo sampling{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sampling.magFilter=sampling.minFilter=VK_FILTER_LINEAR;
    sampling.addressModeU=sampling.addressModeV=sampling.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkCheck(vkCreateSampler(device,&sampling,nullptr,&sampler),"Create VR color sampler");
    VkPipelineLayoutCreateInfo pipeline_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pipeline_info.setLayoutCount=1; pipeline_info.pSetLayouts=&color_layout;
    VkPushConstantRange color_parameters{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(ColorParameters)};
    pipeline_info.pushConstantRangeCount=1; pipeline_info.pPushConstantRanges=&color_parameters;
    VkCheck(vkCreatePipelineLayout(device,&pipeline_info,nullptr,&pipeline_layout),"Create VR color pipeline layout");
    VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; shader_info.codeSize=sizeof(kVrColorSpirv); shader_info.pCode=kVrColorSpirv;
    VkShaderModule shader=VK_NULL_HANDLE; VkCheck(vkCreateShaderModule(device,&shader_info,nullptr,&shader),"Create VR color shader");
    VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; compute.layout=pipeline_layout;
    compute.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; compute.stage.module=shader; compute.stage.pName="main";
    const auto result=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline);
    vkDestroyShaderModule(device,shader,nullptr); VkCheck(result,"Create VR color pipeline");
    REXLOG_INFO("VR: bound to ReXGlue Vulkan device {}; eye swapchains {}x{} format {}",gpu->properties().deviceName,eyes[0].width,eyes[0].height,int(format));
  }
  void PrepareCapture(VkDeviceSize bytes) {
    if(readback && readback_size>=bytes) return;
    if(readback) vkDestroyBuffer(device,readback,nullptr);
    if(readback_memory) vkFreeMemory(device,readback_memory,nullptr);
    readback=VK_NULL_HANDLE; readback_memory=VK_NULL_HANDLE;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size=bytes; info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkCheck(vkCreateBuffer(device,&info,nullptr,&readback),"Create stereo readback");
    VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device,readback,&requirements);
    VkPhysicalDeviceMemoryProperties properties; vkGetPhysicalDeviceMemoryProperties(gpu->physical_device(),&properties);
    uint32_t type=0;
    constexpr auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while(type<properties.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<type)) || (properties.memoryTypes[type].propertyFlags&flags)!=flags)) ++type;
    if(type==properties.memoryTypeCount) throw std::runtime_error("No coherent stereo readback memory");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=type;
    VkCheck(vkAllocateMemory(device,&allocation,nullptr,&readback_memory),"Allocate stereo readback");
    VkCheck(vkBindBufferMemory(device,readback,readback_memory,0),"Bind stereo readback"); readback_size=bytes;
  }
  void SaveCapture(uint32_t width,uint32_t height,int eye) {
    void* data=nullptr; VkCheck(vkMapMemory(device,readback_memory,0,readback_size,0,&data),"Map stereo capture");
    std::vector<char> rgb(size_t(width)*height*3);
    const auto* pixels=static_cast<const uint32_t*>(data); uint64_t hash=1469598103934665603ull;
    for(size_t i=0;i<size_t(width)*height;++i) {
      const uint32_t value=pixels[i]; hash=(hash^value)*1099511628211ull;
      rgb[i*3+0]=char((value&1023)*255/1023);
      rgb[i*3+1]=char(((value>>10)&1023)*255/1023);
      rgb[i*3+2]=char(((value>>20)&1023)*255/1023);
    }
    vkUnmapMemory(device,readback_memory);
    std::filesystem::create_directories("out");
    const auto name=std::string(capture_menu?"out/vr-menu-":"out/vr-eye-")+(eye?"right.ppm":"left.ppm");
    std::ofstream file(name,std::ios::binary); file<<"P6\n"<<width<<" "<<height<<"\n255\n";
    file.write(rgb.data(),std::streamsize(rgb.size()));
    if(!file) throw std::runtime_error("Cannot save stereo capture"); eye_hashes[eye]=hash;
    REXLOG_INFO("VR: saved {} {}x{} pixel-hash={:016X}",name,width,height,hash);
  }
  double ReadGpuDuration(uint32_t first) {
    uint64_t ticks[2];
    const auto result=vkGetQueryPoolResults(device,timing,first,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT);
    return result==VK_SUCCESS ? GpuDurationMs(ticks[0],ticks[1],timestamp_bits,timestamp_period) : -1.0;
  }
  bool BenchmarkDesktop(const NativeGuestOutputRenderContext& input,
                        std::shared_ptr<const native_scene::FrameScene> scene,
                        NativeGuestOutputRenderer render) {
    const bool route=REXCVAR_GET(skate3_vr_simulate_route) && !REXCVAR_GET(skate3_vr);
    if (!REXCVAR_GET(skate3_vr_compare_frame_work) || !timing || (!route && perf_samples>=600) ||
        !scene || scene->generation<=300 || scene->items.size()<=100)
      return render(input,nullptr);
    if (route) {
      if (rex::kernel::guest_presence::GameplayContextValue()!=1) {
        PublishVrControls({});
        return render(input,nullptr);
      }
      const auto now=std::chrono::steady_clock::now();
      if (!route_start.time_since_epoch().count()) route_start=now;
      const double seconds=std::chrono::duration<double>(now-route_start).count();
      PublishVrControls(SimulatedRouteControls(seconds));
      if (seconds>=kSimulatedRouteSeconds) {
        if (!route_finished) REXLOG_INFO("VR simulated route complete: samples={}",perf_samples);
        route_finished=true;
        return render(input,nullptr);
      }
      if (int(seconds)!=route_log_second) {
        route_log_second=int(seconds);
        const auto player=std::find_if(scene->items.begin(),scene->items.end(),[](const auto& item) {
          return item.char_family==2 && item.bones.size()>=12;
        });
        REXLOG_INFO("VR simulated route: second={} camera=({:.2f},{:.2f},{:.2f}) player-known={} player=({:.2f},{:.2f},{:.2f}) items={}",
            route_log_second,scene->cam_pos[0],scene->cam_pos[1],scene->cam_pos[2],player!=scene->items.end(),
            player!=scene->items.end()?player->bones[3]:0,
            player!=scene->items.end()?player->bones[7]:0,
            player!=scene->items.end()?player->bones[11]:0,scene->items.size());
      }
    }
    // Without XR use recorded eye views; the XR-idle fallback duplicates the desktop camera.
    // Neither path measures compositor work or headset frame rate.
    auto context=input;
    const auto image=vulkan::NativeRhiGetImage(input.guest_output);
    StereoShadows shadows;
    const bool share=perf_samples%2==0;
    double cpu_ms=0,copy_ms=0,gpu_ms=0,begin_ms=0,flush_ms=0,query_ms=0;
    const auto copy_start=std::chrono::steady_clock::now();
    auto render_scene=share ? CopyScene(*scene) : nullptr;
    if (share) cpu_ms=copy_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-copy_start).count();
    for (int eye=0;eye<2;++eye) {
      if (eye) {
        const auto begin_start=std::chrono::steady_clock::now();
        context.cmd=vulkan::NativeRhiBeginFrame(context.device,image.image,image.view,true,
            context.guest_output_width,context.guest_output_height,&context.guest_output);
        begin_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin_start).count();
      }
      EyeCamera camera; camera.scene=scene; camera.eye_index=eye;
      camera.render_scene=render_scene;
      camera.override_camera=true; camera.shadows=share?&shadows:nullptr;
      std::copy(scene->view_proj,scene->view_proj+16,camera.view_proj);
      std::copy(scene->proj,scene->proj+16,camera.proj);
      std::copy(scene->cam_pos,scene->cam_pos+3,camera.position);
      if (!REXCVAR_GET(skate3_vr)) {
        // Recorded Quest FOV with nominal 64 mm IPD; no compositor or Link work.
        XrView view{XR_TYPE_VIEW};
        view.pose.orientation.w=1;
        view.pose.position.x=eye ? 0.032f : -0.032f;
        view.fov={eye ? -0.6981f : -0.9425f,eye ? 0.9425f : 0.6981f,0.7679f,-0.9599f};
        XrPosef reference{}; reference.orientation.w=1;
        MakeEyeCamera(scene->view_proj,scene->proj,scene->cam_pos,reference,view,
            float(REXCVAR_GET(skate3_vr_world_scale)),camera.view_proj,camera.proj,camera.position,
            float(REXCVAR_GET(skate3_vr_camera_distance)),float(REXCVAR_GET(skate3_vr_camera_height)));
      }
      const auto start=std::chrono::steady_clock::now();
      vulkan::NativeRhiTimestamp(context.device,timing,eye*2,true);
      const bool rendered=render(context,&camera);
      vulkan::NativeRhiTimestamp(context.device,timing,eye*2+1,false);
      cpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      copy_ms+=camera.scene_copy_ms;
      const auto flush_start=std::chrono::steady_clock::now();
      if (!vulkan::NativeRhiFlushForExternal(context.device)) throw std::runtime_error("Benchmark GPU submission failed");
      flush_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-flush_start).count();
      if (!rendered || !camera.override_camera) return rendered;
      const auto query_start=std::chrono::steady_clock::now();
      const double duration=ReadGpuDuration(eye*2);
      query_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-query_start).count();
      if (duration<0) return rendered;
      gpu_ms+=duration;
    }
    const auto release_start=std::chrono::steady_clock::now();
    render_scene.reset();
    cpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-release_start).count();
    const double pair_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-copy_start).count();
    REXLOG_INFO("VR perf sample: mode={} display={} items={} CPU-render={:.4f} CPU-scene-copy={:.4f} GPU-render={:.4f} GPU-copy=-1 pair-total={:.4f} CPU-begin={:.4f} CPU-flush={:.4f} CPU-query={:.4f}",
        share?"shared":"separate",REXCVAR_GET(skate3_vr)?"desktop":"simulated",scene->items.size(),cpu_ms,copy_ms,gpu_ms,pair_ms,begin_ms,flush_ms,query_ms);
    if (++perf_samples==600 && !route) REXLOG_INFO("VR: desktop shared-work comparison complete (600 samples)");
    return true;
  }
  void CompleteTransfer(int e, uint32_t capture_width=0, uint32_t capture_height=0) {
    const auto fence_start=std::chrono::steady_clock::now();
    VkCheck(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"Complete XR transfer");
    transfer_fence_ms[e]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-fence_start).count();
    if (timing && e<2) {
      gpu_render_ms[e]=ReadGpuDuration(e*2);
      gpu_copy_ms[e]=ReadGpuDuration(4+e*2);
    }
    if(capture_width) {
      try { SaveCapture(capture_width,capture_height,e); }
      catch(const std::exception& error) { REXLOG_WARN("VR: diagnostic capture failed: {}",error.what()); }
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    auto queue=gpu->AcquireQueue(gpu->queue_family_graphics_compute(),0);
    XrCheck(xrReleaseSwapchainImage(eyes[e].handle,&release),"Release eye image");
  }
  void Transfer(const NativeGuestOutputRenderContext& context,int e,bool capture,const SettingsImage* settings=nullptr,
                float feather=0,float aspect=1,WindowPointer pointer={},bool defer_completion=false) {
    auto& eye=eyes[e]; uint32_t index=0;
    const bool profile=timing && e<2;
    bool waited=false,native_pending=false;
    try {
      const auto submit_start=std::chrono::steady_clock::now();
      // Render to our own image while XR waits for a compositor-owned image.
      if (!settings) {
        if (!vulkan::NativeRhiFlushForExternal(context.device,false)) throw std::runtime_error("Game GPU submission failed");
        native_pending=true;
      }
      const auto image_start=std::chrono::steady_clock::now();
      transfer_submit_ms[e]=std::chrono::duration<double,std::milli>(image_start-submit_start).count();
      XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      {
        auto queue=gpu->AcquireQueue(gpu->queue_family_graphics_compute(),0);
        XrCheck(xrAcquireSwapchainImage(eye.handle,&acquire,&index),"Acquire eye image");
      }
      XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
      XrCheck(xrWaitSwapchainImage(eye.handle,&wait),"Wait eye image"); waited=true;
      transfer_image_ms[e]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-image_start).count();
      if(capture) PrepareCapture(VkDeviceSize(context.guest_output_width)*context.guest_output_height*4);
      auto image=settings ? vulkan::NativeVulkanImage{settings->image,settings->view} : vulkan::NativeRhiGetImage(context.guest_output);
      const uint32_t source_width=settings ? settings->width : context.guest_output_width;
      const uint32_t source_height=settings ? settings->height : context.guest_output_height;
      const bool linear=REXCVAR_GET(skate3_vr_linear_color) && !(settings && settings->srgb);
      const bool compute=linear || feather>0 || pointer.hand>=0;
      VkCheck(vkResetCommandBuffer(command,0),"Reset XR command buffer");
      VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      VkCheck(vkBeginCommandBuffer(command,&begin),"Begin XR transfer");
      if (profile) {
        vkCmdResetQueryPool(command,timing,4+e*2,2);
        vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timing,4+e*2);
      }
      VkImageMemoryBarrier barriers[2]{};
      for(auto& b:barriers) { b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; }
      barriers[0].image=image.image; barriers[0].oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barriers[0].newLayout=compute?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barriers[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT; barriers[0].dstAccessMask=compute?VK_ACCESS_SHADER_READ_BIT:VK_ACCESS_TRANSFER_READ_BIT;
      barriers[1].image=eye.images.at(index).image; barriers[1].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      barriers[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
      if(compute) {
      auto color_barrier=barriers[1];
      color_barrier.image=eye.color_image; color_barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
      color_barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
      color_barrier.srcAccessMask=0; color_barrier.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&color_barrier);
      VkDescriptorImageInfo image_infos[2]={{sampler,image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,eye.color_view,VK_IMAGE_LAYOUT_GENERAL}};
      VkWriteDescriptorSet writes[2]{};
      for(int i=0;i<2;++i) { writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet=color_set; writes[i].dstBinding=i; writes[i].descriptorCount=1; writes[i].pImageInfo=&image_infos[i]; }
      writes[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      vkUpdateDescriptorSets(device,2,writes,0,nullptr);
      vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&color_set,0,nullptr);
      ColorParameters parameters{uint32_t(linear),feather,aspect,0};
      parameters.premultiplied=settings && settings->fps_only;
      parameters.pointer_u=pointer.u; parameters.pointer_v=pointer.v;
      parameters.pointer_mode=pointer.hand<0 ? 0 : (pointer.holding ? 2 : 1);
      vkCmdPushConstants(command,pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters);
      vkCmdDispatch(command,(eye.width+15)/16,(eye.height+15)/16,1);
      color_barrier.oldLayout=VK_IMAGE_LAYOUT_GENERAL; color_barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      color_barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; color_barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&color_barrier);
      VkImageCopy copy{}; copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
      copy.dstSubresource=copy.srcSubresource; copy.extent={eye.width,eye.height,1};
      vkCmdCopyImage(command,eye.color_image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          eye.images[index].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
      } else {
        VkImageBlit blit{}; blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; blit.dstSubresource=blit.srcSubresource;
        blit.srcOffsets[1]={int32_t(source_width),int32_t(source_height),1};
        blit.dstOffsets[1]={int32_t(eye.width),int32_t(eye.height),1};
        vkCmdBlitImage(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,eye.images[index].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
      }
      if(capture) {
        if(compute) {
        barriers[0].oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT; barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,barriers);
        }
        VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={context.guest_output_width,context.guest_output_height,1};
        vkCmdCopyImageToBuffer(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; host.buffer=readback; host.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
      }
      barriers[0].oldLayout=(capture||!compute)?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barriers[0].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barriers[0].srcAccessMask=(capture||!compute)?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_SHADER_READ_BIT; barriers[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
      barriers[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barriers[1].newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barriers[1].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
      if (profile) vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timing,5+e*2);
      VkCheck(vkEndCommandBuffer(command),"End XR transfer");
      VkCheck(vkResetFences(device,1,&fence),"Reset XR fence");
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
      { auto queue=gpu->AcquireQueue(gpu->queue_family_graphics_compute(),0); VkCheck(vkQueueSubmit(queue.queue(),1,&submit,fence),"Submit XR transfer"); }
      if (!defer_completion) {
        CompleteTransfer(e,capture ? context.guest_output_width : 0,context.guest_output_height);
      }
    } catch(...) {
      // Early submission may still use the buffers the mono fallback rewrites.
      if (native_pending) {
        try { vulkan::NativeRhiFlushForExternal(context.device); } catch(...) {}
      }
      if(waited) {
        auto queue=gpu->AcquireQueue(gpu->queue_family_graphics_compute(),0);
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(eye.handle,&release);
      }
      throw;
    }
  }
};
std::mutex mutex;
std::unique_ptr<State> state;
std::atomic<bool> settings_visible{false};
std::mutex cull_camera_mutex;
float cull_camera[3]{};
std::chrono::steady_clock::time_point cull_camera_time{};

VkResult CreateInstance(void* user,PFN_vkGetInstanceProcAddr proc,const VkInstanceCreateInfo* info,VkInstance* output) {
  auto& s=*static_cast<State*>(user);
  try {
    XrVulkanInstanceCreateInfoKHR create{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    create.systemId=s.xr.system(); create.pfnGetInstanceProcAddr=proc; create.vulkanCreateInfo=info;
    VkResult result; XrCheck(Function<PFN_xrCreateVulkanInstanceKHR>(s.xr.instance(),"xrCreateVulkanInstanceKHR")(s.xr.instance(),&create,output,&result),"Create shared Vulkan instance");
    return result;
  } catch(const std::exception& error) { REXLOG_ERROR("VR: {}",error.what()); return VK_ERROR_INITIALIZATION_FAILED; }
}
VkPhysicalDevice SelectGpu(void* user,VkInstance instance) {
  auto& s=*static_cast<State*>(user);
  try {
    XrVulkanGraphicsDeviceGetInfoKHR info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR}; info.systemId=s.xr.system(); info.vulkanInstance=instance;
    VkPhysicalDevice gpu=VK_NULL_HANDLE;
    XrCheck(Function<PFN_xrGetVulkanGraphicsDevice2KHR>(s.xr.instance(),"xrGetVulkanGraphicsDevice2KHR")(s.xr.instance(),&info,&gpu),"Select shared GPU"); return gpu;
  } catch(const std::exception& error) { REXLOG_ERROR("VR: {}",error.what()); return VK_NULL_HANDLE; }
}
VkResult CreateDevice(void* user,PFN_vkGetInstanceProcAddr proc,VkPhysicalDevice gpu,const VkDeviceCreateInfo* info,VkDevice* output) {
  auto& s=*static_cast<State*>(user);
  try {
    XrVulkanDeviceCreateInfoKHR create{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    create.systemId=s.xr.system(); create.pfnGetInstanceProcAddr=proc; create.vulkanPhysicalDevice=gpu; create.vulkanCreateInfo=info;
    VkResult result; XrCheck(Function<PFN_xrCreateVulkanDeviceKHR>(s.xr.instance(),"xrCreateVulkanDeviceKHR")(s.xr.instance(),&create,output,&result),"Create shared Vulkan device"); return result;
  } catch(const std::exception& error) { REXLOG_ERROR("VR: {}",error.what()); return VK_ERROR_INITIALIZATION_FAILED; }
}
}

void InitializeGameVr() {
  if (REXCVAR_GET(skate3_vr_resolution_test)) {
    if (REXCVAR_GET(skate3_vr)) throw std::runtime_error("VR resolution diagnostic requires skate3_vr=false");
    const auto extent=EyeRenderExtent(2272,2464,REXCVAR_GET(skate3_vr_render_scale));
    vulkan::NativeRhiSetOutputSizeOverride(extent.width,extent.height);
    REXLOG_INFO("VR resolution diagnostic: desktop-only {}x{}, no headset/session validation",extent.width,extent.height);
    if (!REXCVAR_GET(skate3_vr_compare_frame_work)) return;
  }
  if(!REXCVAR_GET(skate3_vr)) {
    if (REXCVAR_GET(skate3_vr_compare_frame_work)) {
      std::lock_guard lock(mutex);
      state=std::make_unique<State>();
      REXLOG_INFO("VR: simulated rendering benchmark; no OpenXR runtime or headset pacing");
    }
    return;
  }
  std::lock_guard lock(mutex);
  state=std::make_unique<State>(); state->xr.Initialize(); state->xr.DetectHeadset();
  state->UpdateResolution(true);
  rex::cvar::SetFlagByName("skate3_native_render_scene_occlusion_cull", "false");
  rex::cvar::SetFlagByName("skate3_native_render_scene_occlusion_cull_guest", "false");
  rex::cvar::SetFlagByName("skate3_native_render_scene_ssao", "false");
  rex::cvar::SetFlagByName("skate3_native_render_scene_ssr", "false");
  rex::ui::vulkan::SetRuntimeHooks({state.get(),CreateInstance,SelectGpu,CreateDevice});
  REXLOG_INFO("VR: {} on {}",state->xr.headset(),state->xr.runtime());
}
void ShutdownGameVr() {
  std::lock_guard lock(mutex);
  ShutdownSettingsSurface(); settings_visible=false;
  vulkan::NativeRhiSetOutputSizeOverride(0,0);
  rex::ui::vulkan::SetRuntimeHooks({}); state.reset();
  PublishVrControls({}); PublishWindowMotion({});
}
void RecenterGameVr() {
  CancelVrEditor();
  std::lock_guard lock(mutex);
  rex::cvar::SetFlagByName("skate3_vr_window_horizontal","0");
  if(state) { state->recentered=false; state->capture_done=false; }
}
void CaptureGameVr() {
  rex::cvar::SetFlagByName("skate3_vr_capture_once","true");
  REXLOG_INFO("VR: next-frame capture requested");
}
void ToggleGameVrColor() {
  std::lock_guard lock(mutex);
  const bool enabled = !REXCVAR_GET(skate3_vr_linear_color);
  rex::cvar::SetFlagByName("skate3_vr_linear_color", enabled ? "true" : "false");
  REXLOG_INFO("VR: linear-color conversion {}", enabled ? "enabled" : "disabled");
}
bool GetGameCullCamera(float camera[3]) {
  std::lock_guard lock(cull_camera_mutex);
  if (std::chrono::steady_clock::now()-cull_camera_time>std::chrono::milliseconds(250)) return false;
  std::copy(cull_camera,cull_camera+3,camera);
  return true;
}
void ToggleGameVrCulling() {
  const bool enabled=!REXCVAR_GET(skate3_vr_expand_game_frustum);
  rex::cvar::SetFlagByName("skate3_vr_expand_game_frustum",enabled?"true":"false");
  REXLOG_INFO("VR: main-camera culling expansion {}",enabled?"enabled":"disabled");
}
void CycleGameVrMode() {
  CancelVrEditor();
  const int mode=(REXCVAR_GET(skate3_vr_mode)+1)%3;
  rex::cvar::SetFlagByName("skate3_vr_mode",std::to_string(mode));
}
bool GameVrEnabled() { return REXCVAR_GET(skate3_vr); }
uint32_t GameVrShadowReferenceHeight(uint32_t fallback) {
  if ((GameVrEnabled() || REXCVAR_GET(skate3_vr_resolution_test)) && REXCVAR_GET(skate3_vr_render_scale)>0) {
    const auto height=vulkan::NativeRhiOriginalOutputHeight();
    if (height) return height;
  }
  return fallback;
}
void SetGameVrSettingsVisible(bool visible) { settings_visible=visible; }
void PresentFlatMenu(const rex::graphics::NativeGuestOutputRenderContext& context) {
  RenderStereo(context, nullptr, [](const auto&, void*) { return true; });
}

bool RenderStereo(const rex::graphics::NativeGuestOutputRenderContext& input,
                  std::shared_ptr<const native_scene::FrameScene> scene,
                  rex::graphics::NativeGuestOutputRenderer render) {
  std::lock_guard lock(mutex);
  if(!state || state->failed) return render(input,nullptr);
  auto& s=*state;
  if(input.backend!=NativeGuestOutputBackend::kVulkan) return render(input,nullptr);
  if (scene) {
    std::lock_guard camera_lock(cull_camera_mutex);
    std::copy(scene->cam_pos,scene->cam_pos+3,cull_camera);
    cull_camera_time=std::chrono::steady_clock::now();
  }
  if (!REXCVAR_GET(skate3_vr)) {
    try {
      if (!s.gpu) {
        s.gpu=static_cast<rex::ui::vulkan::VulkanDevice*>(vulkan::NativeRhiGetVulkanDevice(input.device));
        s.device=s.gpu->device();
        s.InitializeTiming();
      }
      return s.BenchmarkDesktop(input,scene,render);
    } catch (const std::exception& error) {
      PublishVrControls({});
      s.failed=true;
      REXLOG_ERROR("VR: simulated benchmark stopped: {}",error.what());
      return render(input,nullptr);
    }
  }
  TrackingFrame frame; bool begun=false,left_pending=false;
  try {
    const int mode=REXCVAR_GET(skate3_vr_mode);
    const bool photo=scene && (scene->photo_fx.valid || native_scene::PhotoPresentationActive());
    const bool object_editor=scene && std::any_of(scene->items.begin(),scene->items.end(),[](const auto& item) { return item.selected; });
    const auto editor_view=SelectEditorView(REXCVAR_GET(skate3_vr_editor_comfort),photo,object_editor);
    if(editor_view!=s.last_editor_view) {
      REXLOG_INFO("VR: editor presentation {} (0 immersive/default, 1 original mono photo camera, 2 stereo object window)",int(editor_view));
      s.last_editor_view=editor_view;
    }
    const bool photo_flat=editor_view==EditorView::Photo;
    const bool comfort=mode==0 && editor_view!=EditorView::Default;
    const bool window=mode!=0 || comfort;
    s.UpdateResolution(!scene || photo_flat);
    if(!s.session) s.Bind(static_cast<rex::ui::vulkan::VulkanDevice*>(vulkan::NativeRhiGetVulkanDevice(input.device)));
    if (mode!=s.last_mode) {
      s.trace.ResetWindow();
      s.trace_last_submit={};
      s.trace.have_scene=s.trace.have_yaw=false;
      s.recentered=false;
      s.window_facing={};
      s.room->Disable();
      rex::cvar::SetFlagByName("skate3_vr_status",mode==0 ? "Immersive VR" : "Stereo window; black background");
      s.last_mode=mode;
      REXLOG_INFO("VR: view mode {} (0 immersive, 1 window, 2 window + room)",mode);
    }
    const auto wait_start=std::chrono::steady_clock::now();
    const bool ready=s.session->PollEvents();
    const double poll_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wait_start).count();
    if(!ready || !s.session->BeginFrame(frame)) {
      s.trace_last_submit={}; s.trace.have_scene=s.trace.have_yaw=false;
      PublishVrControls({}); PublishWindowMotion({}); return s.BenchmarkDesktop(input,scene,render);
    }
    const double xr_wait_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wait_start).count();
    begun=true;
    TouchControls controls;
    ControllerHands hands;
    if(s.controllers) {
      try {
        controls=s.controllers->Sample(s.session->state()==XR_SESSION_STATE_FOCUSED);
        if(VrEditorPanelMode()==1) hands=s.controllers->LocateHands(s.session->local_space(),frame.display_time);
      }
      catch(const std::exception& error) {
        controls={}; PublishVrControls({}); PublishWindowMotion({}); s.controllers.reset();
        REXLOG_WARN("VR: controller input stopped: {}",error.what());
      }
    }
    constexpr auto valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    if(!frame.should_render || (frame.view_flags&valid)!=valid) {
      s.trace_last_submit={}; s.trace.have_scene=s.trace.have_yaw=false;
      PublishVrControls({}); PublishWindowMotion({});
      s.session->EndFrame(frame); begun=false; return s.BenchmarkDesktop(input,scene,render);
    }
    if (REXCVAR_GET(skate3_vr_recenter_requested)) {
      s.recentered=false;
      s.window_facing={};
      rex::cvar::SetFlagByName("skate3_vr_window_horizontal","0");
      rex::cvar::SetFlagByName("skate3_vr_recenter_requested","false");
    }
    if(!s.recentered) { s.reference=RecenterPose(frame.head.pose); s.recentered=true; }
    if (mode==2) {
      const bool active=s.room->Enable();
      const std::string status=active ? "Stereo window with room view" : "Stereo window; black background. "+s.room->error();
      if (REXCVAR_GET(skate3_vr_status)!=status) {
        rex::cvar::SetFlagByName("skate3_vr_status",status);
        REXLOG_INFO("VR: {}",status);
      }
    }
    const float window_width=float(comfort ? REXCVAR_GET(skate3_vr_menu_width) : REXCVAR_GET(skate3_vr_window_width));
    const float window_distance=float(comfort ? REXCVAR_GET(skate3_vr_menu_distance) : REXCVAR_GET(skate3_vr_window_distance));
    const float window_height=comfort ? 0.f : float(REXCVAR_GET(skate3_vr_window_height));
    const float aspect=PanelAspect(input.display_width,input.display_height,input.guest_output_width,input.guest_output_height);
    const float feather=WindowFeather(mode,s.room->layer()!=XR_NULL_HANDLE,REXCVAR_GET(skate3_vr_window_feather));
    const bool pause_menu=scene && rex::kernel::guest_presence::GameplayContextValue()==0;
    if (window && !(window_width>=0.5f && window_width<=6 &&
        window_distance>=0.5f && window_distance<=8 && window_height>=-2 && window_height<=2 &&
        aspect>0 && aspect<=10)) throw std::runtime_error("Invalid VR window size/position");
    const bool placing=VrEditorPanelMode()==1;
    const auto placement_revision=WindowPlacementRevision();
    const auto pointer=window && placing ? ReadWindowPointer() : WindowPointer{};
    auto panel_pose=WindowPose(comfort ? RecenterPose(frame.head.pose) : s.reference,window_distance,window_height,
        comfort ? 0.f : float(rex::cvar::Query<double>("skate3_vr_window_horizontal")));
    if(comfort) panel_pose=FaceWindowToHead(panel_pose,frame.head.pose.position);
    else if(window) panel_pose=s.window_facing.Update(panel_pose,frame.head.pose.position,
        REXCVAR_GET(skate3_vr_window_face_viewer),placing,pointer.holding,placement_revision);
    PublishVrControls(controls,placing);
    PublishWindowMotion({controls,hands,s.reference,aspect,panel_pose.orientation});
    const bool system_dialog=rex::kernel::xam::xeXamIsUIActive();
    const bool host_menu=settings_visible.load() || system_dialog;
    const bool fps_only=!host_menu && rex::cvar::Query<bool>("show_fps_counter");
    const bool host_ui=host_menu || fps_only;
    if (!host_ui || system_dialog!=s.settings_dialog || fps_only!=s.settings_fps_only)
      s.settings_presented=false;
    XrCompositionLayerProjectionView projections[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    bool rendered=false,world_rendered=false,eye_world[2]{};
    auto context=input;
    const auto image=vulkan::NativeRhiGetImage(input.guest_output);
    const auto start=std::chrono::steady_clock::now();
    const bool capture_now=REXCVAR_GET(skate3_vr_capture_once);
    if(capture_now) rex::cvar::SetFlagByName("skate3_vr_capture_once","false");
    s.capture_menu=!scene && (capture_now || s.submitted%600==300);
    const bool capture=capture_now || (REXCVAR_GET(skate3_vr_capture) && (s.capture_menu ||
        (!s.capture_done && scene && scene->generation>300 && scene->items.size()>100)));
    if(capture) REXLOG_INFO("VR: capture layout display={}x{} source={}x{} aspect={:.4f} scene={} menu={} window={} photo={}",
        input.display_width,input.display_height,input.guest_output_width,input.guest_output_height,aspect,
        scene?scene->generation:0,s.capture_menu,window,photo);
    double render_ms[2]{},copy_ms[2]{};
    bool overlap_recorded=false;
    double scene_copy_ms[2]{};
    uint32_t frustum_skips[2]{},world_draws[2]{},mesh_reuses=0,material_reuses=0;
    StereoShadows shadows;
    const bool compare=REXCVAR_GET(skate3_vr_compare_frame_work) && s.perf_samples<600;
    const bool share_work=compare ? s.perf_samples%2==0 : REXCVAR_GET(skate3_vr_share_frame_work);
    const auto scene_copy_start=std::chrono::steady_clock::now();
    auto render_scene=share_work && scene && !photo_flat ? s.CopyScene(*scene) : nullptr;
    const double shared_scene_copy_ms=render_scene ?
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-scene_copy_start).count() : 0;
    scene_copy_ms[0]=shared_scene_copy_ms;
    // Photo capture/card composition is a mono game operation, not two eye-side effects.
    for(int eye=0;eye<(photo_flat ? 1 : 2);++eye) {
      if(eye) context.cmd=vulkan::NativeRhiBeginFrame(context.device,image.image,image.view,true,context.guest_output_width,context.guest_output_height,&context.guest_output);
      EyeCamera camera; camera.scene=scene; camera.eye_index=eye;
      camera.shadows=share_work ? &shadows : nullptr;
      camera.render_scene=render_scene;
      camera.clear_hud_backdrop=!window && !photo && !object_editor && REXCVAR_GET(skate3_vr_clear_hud_backdrop);
      camera.capture_ui=capture && eye==0;
      camera.share_preparation=!photo && !object_editor && !pause_menu;
      if(scene && !scene->items.empty() && scene->proj[0]!=0 && !photo_flat) {
        if (window) {
          const XrPosef view_reference{panel_pose.orientation,frame.head.pose.position};
          MakeWindowEyeCamera(scene->view_proj,scene->proj,scene->cam_pos,view_reference,frame.eyes[eye],
              float(REXCVAR_GET(skate3_vr_world_scale)),window_width,
              camera.view_proj,camera.proj,camera.position,float(REXCVAR_GET(skate3_vr_window_depth)),aspect);
        } else {
          MakeEyeCamera(scene->view_proj,scene->proj,scene->cam_pos,s.reference,frame.eyes[eye],float(REXCVAR_GET(skate3_vr_world_scale)),camera.view_proj,camera.proj,camera.position,
              float(REXCVAR_GET(skate3_vr_camera_distance)),float(REXCVAR_GET(skate3_vr_camera_height)));
          MakeHudTransform(frame.head.pose,frame.eyes[eye],camera.proj,
              float(pause_menu ? REXCVAR_GET(skate3_vr_menu_width) : REXCVAR_GET(skate3_vr_hud_width)),aspect,
              float(pause_menu ? REXCVAR_GET(skate3_vr_menu_distance) : REXCVAR_GET(skate3_vr_hud_distance)),camera.hud_transform);
        }
        camera.override_camera=true;
        if(capture) {
          const auto& pose=frame.eyes[eye].pose; const auto& fov=frame.eyes[eye].fov;
          REXLOG_INFO("VR: eye {} local=({:.4f},{:.4f},{:.4f}) fov=({:.4f},{:.4f},{:.4f},{:.4f}) world=({:.4f},{:.4f},{:.4f})",eye,pose.position.x,pose.position.y,pose.position.z,fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown,camera.position[0],camera.position[1],camera.position[2]);
        }
      }
      const auto render_start=std::chrono::steady_clock::now();
      if (s.timing) vulkan::NativeRhiTimestamp(context.device,s.timing,eye*2,true);
      rendered=render(context,&camera);
      if (s.timing) vulkan::NativeRhiTimestamp(context.device,s.timing,eye*2+1,false);
      eye_world[eye]=camera.override_camera;
      render_ms[eye]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-render_start).count();
      if (!eye) render_ms[eye]+=shared_scene_copy_ms;
      scene_copy_ms[eye]+=camera.scene_copy_ms;
      frustum_skips[eye]=camera.frustum_skips; world_draws[eye]=camera.world_draws;
      mesh_reuses+=camera.mesh_reuses; material_reuses+=camera.material_reuses;
      if (left_pending) {
        // Right-eye commands are recorded, but share GPU targets with the left.
        const auto complete_start=std::chrono::steady_clock::now();
        s.CompleteTransfer(0);
        copy_ms[0]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-complete_start).count();
        left_pending=false;
      }
      if(!rendered) break;
      const bool defer_left=eye==0 && share_work && camera.override_camera && !window && !photo &&
          !object_editor && !pause_menu && !capture && vulkan::NativeRhiCanOverlapNextFrame(context.device);
      const auto copy_start=std::chrono::steady_clock::now();
      s.Transfer(context,eye,capture,nullptr,feather,aspect,pointer,defer_left);
      left_pending=defer_left;
      overlap_recorded|=defer_left;
      copy_ms[eye]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-copy_start).count();
      projections[eye].pose=frame.eyes[eye].pose; projections[eye].fov=frame.eyes[eye].fov;
      projections[eye].subImage.swapchain=s.eyes[eye].handle;
      projections[eye].subImage.imageRect.extent={int32_t(s.eyes[eye].width),int32_t(s.eyes[eye].height)};
    }
    if (render_scene) {
      const auto release_start=std::chrono::steady_clock::now();
      render_scene.reset();
      render_ms[0]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-release_start).count();
    }
    if(rendered) {
      // A transition can invalidate the world between eyes. Submit the flat
      // image to both eyes instead of mixing a world eye and a loading eye.
      world_rendered=eye_world[0] && eye_world[1];
      const int flat_eye=eye_world[0] && !eye_world[1] ? 1 : 0;
      if(capture && !s.capture_menu) { s.capture_done=true; REXLOG_INFO("VR: stereo capture same-scene={} distinct-pixels={}",scene->generation,s.eye_hashes[0]!=s.eye_hashes[1]); }
      XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION}; layer.space=s.session->local_space(); layer.viewCount=2; layer.views=projections;
      XrCompositionLayerQuad menu{XR_TYPE_COMPOSITION_LAYER_QUAD};
      menu.space=s.session->view_space(); menu.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
      menu.subImage=projections[flat_eye].subImage; menu.pose.orientation.w=1;
      const float distance=float(REXCVAR_GET(skate3_vr_menu_distance));
      const float width=float(REXCVAR_GET(skate3_vr_menu_width));
      if(!(distance>=0.5f && distance<=10 && width>=0.5f && width<=10)) throw std::runtime_error("Invalid VR menu size/distance");
      menu.pose.position.z=-distance;
      menu.size={width,width/aspect};
      const XrCompositionLayerBaseHeader* layers[4]{};
      uint32_t layer_count=0;
      XrCompositionLayerPassthroughFB background{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
      if (mode==2 && s.room->layer()) {
        background.flags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        background.layerHandle=s.room->layer();
        layers[layer_count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&background);
      }
      XrCompositionLayerQuad panels[2]={{XR_TYPE_COMPOSITION_LAYER_QUAD},{XR_TYPE_COMPOSITION_LAYER_QUAD}};
      if (window) {
        for (int eye=0;eye<(world_rendered?2:1);++eye) {
          auto& panel=panels[eye];
          if(feather>0) panel.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
          panel.space=s.session->local_space(); panel.pose=panel_pose;
          panel.size={window_width,window_width/aspect};
          panel.eyeVisibility=world_rendered ? (eye ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT) : XR_EYE_VISIBILITY_BOTH;
          panel.subImage=projections[world_rendered ? eye : flat_eye].subImage;
          layers[layer_count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&panel);
        }
      } else {
        layers[layer_count++]=world_rendered
            ? reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)
            : reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menu);
      }
      XrCompositionLayerQuad settings_panel{XR_TYPE_COMPOSITION_LAYER_QUAD};
      if (host_ui) {
        SettingsImage settings;
        auto reading=TryReadSettingsSurface(settings);
        if (reading && settings.system_dialog==system_dialog && settings.fps_only==fps_only &&
            (!fps_only || settings.generation!=s.settings_generation)) {
          s.Transfer(context,2,false,&settings);
          s.settings_presented=true; s.settings_editor=settings.editor_mode;
          s.settings_dialog=system_dialog;
          s.settings_fps_only=fps_only; s.settings_generation=settings.generation;
          s.fps_crop=FpsPanelCrop(settings.width,settings.height,s.eyes[2].width,s.eyes[2].height,settings.fps_region);
          const auto crop=settings.editor_mode ? EditorPanelRegion(settings.width,settings.height)
              : EditorRegion{0,0,settings.width,settings.height};
          s.settings_aspect=float(crop.width)/crop.height;
        }
        if (s.settings_presented) {
          settings_panel.space=s.session->view_space(); settings_panel.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
          settings_panel.subImage.swapchain=s.eyes[2].handle;
          settings_panel.subImage.imageRect.extent={int32_t(s.eyes[2].width),int32_t(s.eyes[2].height)};
          settings_panel.pose.orientation.w=1; settings_panel.pose.position.z=-2;
          settings_panel.size={2.4f,2.4f/s.settings_aspect};
          if (fps_only) {
            settings_panel.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            settings_panel.subImage.imageRect={{int32_t(s.fps_crop.x),int32_t(s.fps_crop.y)},
                {int32_t(s.fps_crop.width),int32_t(s.fps_crop.height)}};
            settings_panel.pose.position={0.55f,0.4f,-2.f};
            const float width=2.4f*float(s.fps_crop.width)/s.eyes[2].width;
            settings_panel.size={width,width/s.fps_crop.aspect};
          } else if (s.settings_editor) {
            const auto crop=EditorPanelRegion(s.eyes[2].width,s.eyes[2].height);
            settings_panel.subImage.imageRect={{int32_t(crop.x),int32_t(crop.y)},
                {int32_t(crop.width),int32_t(crop.height)}};
            settings_panel.pose.position={0,-0.65f,-1.7f};
            settings_panel.size={1.8f,1.8f/s.settings_aspect};
          }
          layers[layer_count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&settings_panel);
        }
      }
      const auto end_start=std::chrono::steady_clock::now();
      s.session->EndFrame(frame,layer_count,layers); begun=false;
      if (REXCVAR_GET(skate3_vr_perf_trace) && world_rendered && scene && !capture) {
        const auto now=std::chrono::steady_clock::now();
        const double interval=s.trace_last_submit.time_since_epoch().count() ?
            std::chrono::duration<double,std::milli>(now-s.trace_last_submit).count() : 0;
        s.trace_last_submit=now;
        const double gpu=s.timing && s.gpu_render_ms[0]>=0 && s.gpu_render_ms[1]>=0 ?
            s.gpu_render_ms[0]+s.gpu_render_ms[1] : -1;
        const double period=double(frame.display_period)/1e6;
        const double end_ms=std::chrono::duration<double,std::milli>(now-end_start).count();
        const double render=render_ms[0]+render_ms[1], transfer=copy_ms[0]+copy_ms[1];
        const double other_ms=std::max(0.0,interval-render-transfer-xr_wait_ms-end_ms);
        s.trace.Add(interval,period,render,transfer,xr_wait_ms,gpu,
                    scene->generation,scene->view_proj,scene->proj,
                    {poll_ms,frame.wait_ms,frame.begin_ms,frame.tracking_ms,end_ms,other_ms},uint32_t(scene->items.size()));
        if (s.trace.frames>=120) {
          const auto& t=s.trace;
          REXLOG_INFO("VR trace: frames={} scene={} items={} period={:.2f}ms submit-interval avg/max={:.2f}/{:.2f}ms over-1.5x={} repeated-scene={} yaw-travel={:.1f}deg CPU-render-max={:.2f}ms transfer-wait-max={:.2f}ms GPU-render-max={:.2f}ms GPU-samples={} XR-poll-and-begin-max={:.2f}ms",
              t.frames,scene->generation,scene->items.size(),period,t.intervals?t.interval_sum/t.intervals:0,
              t.interval_max,t.slow,t.repeated,t.yaw_travel*180/3.14159265358979323846,
              t.render_max,t.wait_max,t.gpu_samples?t.gpu_max:-1,t.gpu_samples,t.xr_wait_max);
          REXLOG_INFO("VR trace worst interval: scene={} interval={:.2f}ms yaw-delta={:.2f}deg CPU-render={:.2f}ms transfer-wait={:.2f}ms GPU-render={:.2f}ms XR-poll-and-begin={:.2f}ms",
              t.worst_scene,t.interval_max,t.worst_yaw_delta<0?-1:t.worst_yaw_delta*180/3.14159265358979323846,
              t.worst_render,t.worst_wait,t.worst_gpu,t.worst_xr_wait);
          const auto& xr=t.worst_xr;
          REXLOG_INFO("VR trace XR worst: scene={} items={} mode={} poll={:.2f}ms wait={:.2f}ms begin={:.2f}ms tracking={:.2f}ms end={:.2f}ms other={:.2f}ms",
              t.worst_scene,t.worst_items,mode,xr.poll,xr.wait,xr.begin,xr.tracking,xr.end,xr.other);
          s.trace.ResetWindow();
        }
      } else { s.trace_last_submit={}; s.trace.have_scene=s.trace.have_yaw=false; }
      if (compare && world_rendered && scene && scene->generation>300 && scene->items.size()>100 && !capture) {
        REXLOG_INFO("VR perf sample: mode={} display=headset items={} CPU-render={:.4f} CPU-scene-copy={:.4f} GPU-render={:.4f} GPU-copy={:.4f}",
            share_work?"shared":"separate",scene->items.size(),render_ms[0]+render_ms[1],
            scene_copy_ms[0]+scene_copy_ms[1],s.gpu_render_ms[0]+s.gpu_render_ms[1],s.gpu_copy_ms[0]+s.gpu_copy_ms[1]);
        ++s.perf_samples;
      }
      if(++s.submitted==1 || s.submitted%120==0) {
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        REXLOG_INFO("VR: submitted stereo frame {} scene={} items={} total={:.2f}ms CPU-render L/R={:.2f}/{:.2f}ms GPU-copy/wait L/R={:.2f}/{:.2f}ms",s.submitted,scene?scene->generation:0,scene?scene->items.size():0,ms,render_ms[0],render_ms[1],copy_ms[0],copy_ms[1]);
        if (s.timing) REXLOG_INFO("VR GPU: render L/R={:.3f}/{:.3f}ms transfer L/R={:.3f}/{:.3f}ms CPU-scene-copy L/R={:.3f}/{:.3f}ms shared-work={}",
            s.gpu_render_ms[0],s.gpu_render_ms[1],s.gpu_copy_ms[0],s.gpu_copy_ms[1],scene_copy_ms[0],scene_copy_ms[1],share_work);
        if (s.timing) REXLOG_INFO("VR transfer CPU: submit L/R={:.3f}/{:.3f}ms image-acquire/wait L/R={:.3f}/{:.3f}ms completion L/R={:.3f}/{:.3f}ms overlap-recording={}",
            s.transfer_submit_ms[0],s.transfer_submit_ms[1],s.transfer_image_ms[0],s.transfer_image_ms[1],s.transfer_fence_ms[0],s.transfer_fence_ms[1],overlap_recorded);
        REXLOG_INFO("VR preparation: right mesh-reuses={} material-reuses={}",mesh_reuses,material_reuses);
        REXLOG_INFO("VR visibility: captured={} world-draws L/R={}/{} frustum-skips L/R={}/{}",
            scene?scene->items.size():0,world_draws[0],world_draws[1],frustum_skips[0],frustum_skips[1]);
      }
    } else { s.session->EndFrame(frame); begun=false; }
    if(!rendered && capture_now) rex::cvar::SetFlagByName("skate3_vr_capture_once","true");
    return rendered;
  } catch(const std::exception& error) {
    if (left_pending) {
      try { s.CompleteTransfer(0); left_pending=false; } catch(...) {}
    }
    PublishVrControls({});
    PublishWindowMotion({});
    if(begun) { try { s.session->EndFrame(frame); } catch(...) {} }
    s.failed=true; REXLOG_ERROR("VR: stereo rendering stopped: {}",error.what());
    try { if(s.session) s.session->RequestExit(); } catch(...) {}
    return !left_pending && render(input,nullptr);
  }
}
}
