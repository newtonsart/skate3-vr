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
#include "stereo_math.h"
#include "cull_frustum.h"
#include "gpu_timing.h"
#include "vr_color_spirv.h"
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/graphics/vulkan/native_rhi_vulkan.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>
#include <algorithm>
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
REXCVAR_DEFINE_BOOL(skate3_vr_capture, false, "VR", "Capture one rendered stereo scene to local out/vr-eye-*.ppm");
REXCVAR_DEFINE_DOUBLE(skate3_vr_menu_distance, 2.0, "VR", "Menu panel distance in meters");
REXCVAR_DEFINE_DOUBLE(skate3_vr_menu_width, 2.4, "VR", "Menu panel width in meters");
REXCVAR_DEFINE_DOUBLE(skate3_vr_hud_width, 1.6, "VR", "Readable HUD panel width in meters").range(0.5,3.0);
REXCVAR_DEFINE_DOUBLE(skate3_vr_hud_distance, 2.0, "VR", "HUD convergence distance in meters").range(0.5,5.0);
REXCVAR_DEFINE_BOOL(skate3_vr_linear_color, true, "VR", "Decode display color before OpenXR composition");
REXCVAR_DEFINE_BOOL(skate3_vr_expand_game_frustum, true, "VR", "Expand the matched main-camera culling planes for head turns");
REXCVAR_DEFINE_BOOL(skate3_vr_share_frame_work, false, "VR", "Share the scene copy and light-space shadows between eyes (headset validation pending)");
REXCVAR_DEFINE_BOOL(skate3_vr_gpu_timing, false, "VR", "Measure GPU render and transfer times per eye")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(skate3_vr_compare_frame_work, false, "VR", "Log 600 alternating separate/shared-work samples; desktop camera if XR is inactive")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

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
  rex::ui::vulkan::VulkanDevice* gpu=nullptr;
  VkDevice device=VK_NULL_HANDLE;
  VkCommandPool pool=VK_NULL_HANDLE;
  VkCommandBuffer command=VK_NULL_HANDLE;
  VkFence fence=VK_NULL_HANDLE;
  VkQueryPool timing=VK_NULL_HANDLE;
  uint32_t timestamp_bits=0;
  float timestamp_period=0;
  double gpu_render_ms[2]{-1,-1},gpu_copy_ms[2]{-1,-1};
  uint32_t perf_samples=0;
  VkDescriptorSetLayout color_layout=VK_NULL_HANDLE;
  VkDescriptorPool color_pool=VK_NULL_HANDLE;
  VkDescriptorSet color_set=VK_NULL_HANDLE;
  VkSampler sampler=VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout=VK_NULL_HANDLE;
  VkPipeline pipeline=VK_NULL_HANDLE;
  EyeSwapchain eyes[2];
  XrPosef reference{};
  bool recentered=false;
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
      session.reset();
    }
  }
  void Bind(rex::ui::vulkan::VulkanDevice* graphics) {
    gpu=graphics; device=gpu->device();
    session=std::make_unique<OpenXrSession>(xr);
    auto& queue=*gpu->queue_families()[gpu->queue_family_graphics_compute()].queues[0];
    session->Bind(gpu->vulkan_instance()->instance(),gpu->physical_device(),device,gpu->queue_family_graphics_compute(),&queue.mutex);
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
    for(int e=0;e<2;++e) {
      auto& eye=eyes[e]; eye.width=xr.views()[e].recommendedImageRectWidth; eye.height=xr.views()[e].recommendedImageRectHeight;
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
    if (!REXCVAR_GET(skate3_vr_compare_frame_work) || !timing || perf_samples>=600 ||
        !scene || scene->generation<=300 || scene->items.size()<=100)
      return render(input,nullptr);
    // Duplicate the desktop camera to measure actual GPU work without a worn
    // headset. This is not a headset frame-rate or stereo-visual validation.
    auto context=input;
    const auto image=vulkan::NativeRhiGetImage(input.guest_output);
    StereoShadows shadows;
    const bool share=perf_samples%2==0;
    double cpu_ms=0,copy_ms=0,gpu_ms=0;
    const auto copy_start=std::chrono::steady_clock::now();
    auto render_scene=share ? std::make_shared<native_scene::FrameScene>(*scene) : nullptr;
    if (share) cpu_ms=copy_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-copy_start).count();
    for (int eye=0;eye<2;++eye) {
      if (eye) context.cmd=vulkan::NativeRhiBeginFrame(context.device,image.image,image.view,true,
          context.guest_output_width,context.guest_output_height,&context.guest_output);
      EyeCamera camera; camera.scene=scene; camera.eye_index=eye;
      camera.render_scene=render_scene;
      camera.override_camera=true; camera.shadows=share?&shadows:nullptr;
      std::copy(scene->view_proj,scene->view_proj+16,camera.view_proj);
      std::copy(scene->proj,scene->proj+16,camera.proj);
      std::copy(scene->cam_pos,scene->cam_pos+3,camera.position);
      const auto start=std::chrono::steady_clock::now();
      vulkan::NativeRhiTimestamp(context.device,timing,eye*2,true);
      const bool rendered=render(context,&camera);
      vulkan::NativeRhiTimestamp(context.device,timing,eye*2+1,false);
      cpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      copy_ms+=camera.scene_copy_ms;
      if (!vulkan::NativeRhiFlushForExternal(context.device)) throw std::runtime_error("Benchmark GPU submission failed");
      if (!rendered || !camera.override_camera) return rendered;
      const double duration=ReadGpuDuration(eye*2);
      if (duration<0) return rendered;
      gpu_ms+=duration;
    }
    const auto release_start=std::chrono::steady_clock::now();
    render_scene.reset();
    cpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-release_start).count();
    REXLOG_INFO("VR perf sample: mode={} display=desktop items={} CPU-render={:.4f} CPU-scene-copy={:.4f} GPU-render={:.4f} GPU-copy=-1",
        share?"shared":"separate",scene->items.size(),cpu_ms,copy_ms,gpu_ms);
    if (++perf_samples==600) REXLOG_INFO("VR: desktop shared-work comparison complete (600 samples)");
    return true;
  }
  void Transfer(const NativeGuestOutputRenderContext& context,int e,bool capture) {
    auto& eye=eyes[e]; uint32_t index=0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    {
      auto queue=gpu->AcquireQueue(gpu->queue_family_graphics_compute(),0);
      XrCheck(xrAcquireSwapchainImage(eye.handle,&acquire,&index),"Acquire eye image");
    }
    bool waited=false;
    try {
      XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
      XrCheck(xrWaitSwapchainImage(eye.handle,&wait),"Wait eye image"); waited=true;
      if(!vulkan::NativeRhiFlushForExternal(context.device)) throw std::runtime_error("Game GPU submission failed");
      if(capture) PrepareCapture(VkDeviceSize(context.guest_output_width)*context.guest_output_height*4);
      auto image=vulkan::NativeRhiGetImage(context.guest_output);
      const bool linear=REXCVAR_GET(skate3_vr_linear_color);
      VkCheck(vkResetCommandBuffer(command,0),"Reset XR command buffer");
      VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      VkCheck(vkBeginCommandBuffer(command,&begin),"Begin XR transfer");
      if (timing) {
        vkCmdResetQueryPool(command,timing,4+e*2,2);
        vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timing,4+e*2);
      }
      VkImageMemoryBarrier barriers[2]{};
      for(auto& b:barriers) { b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; }
      barriers[0].image=image.image; barriers[0].oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barriers[0].newLayout=linear?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barriers[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT; barriers[0].dstAccessMask=linear?VK_ACCESS_SHADER_READ_BIT:VK_ACCESS_TRANSFER_READ_BIT;
      barriers[1].image=eye.images.at(index).image; barriers[1].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      barriers[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
      if(linear) {
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
        blit.srcOffsets[1]={int32_t(context.guest_output_width),int32_t(context.guest_output_height),1};
        blit.dstOffsets[1]={int32_t(eye.width),int32_t(eye.height),1};
        vkCmdBlitImage(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,eye.images[index].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
      }
      if(capture) {
        if(linear) {
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
      barriers[0].oldLayout=(capture||!linear)?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barriers[0].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barriers[0].srcAccessMask=(capture||!linear)?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_SHADER_READ_BIT; barriers[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
      barriers[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barriers[1].newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barriers[1].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
      if (timing) vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timing,5+e*2);
      VkCheck(vkEndCommandBuffer(command),"End XR transfer");
      VkCheck(vkResetFences(device,1,&fence),"Reset XR fence");
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
      { auto queue=gpu->AcquireQueue(gpu->queue_family_graphics_compute(),0); VkCheck(vkQueueSubmit(queue.queue(),1,&submit,fence),"Submit XR transfer"); }
      // Correctness baseline: all writes finish before handing ownership to XR.
      VkCheck(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"Complete XR transfer");
      if (timing) {
        gpu_render_ms[e]=ReadGpuDuration(e*2);
        gpu_copy_ms[e]=ReadGpuDuration(4+e*2);
      }
      if(capture) SaveCapture(context.guest_output_width,context.guest_output_height,e);
      XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      {
        auto queue=gpu->AcquireQueue(gpu->queue_family_graphics_compute(),0);
        XrCheck(xrReleaseSwapchainImage(eye.handle,&release),"Release eye image");
      }
      waited=false;
    } catch(...) {
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
  if(!REXCVAR_GET(skate3_vr)) return;
  std::lock_guard lock(mutex);
  state=std::make_unique<State>(); state->xr.Initialize(); state->xr.DetectHeadset();
  rex::cvar::SetFlagByName("skate3_native_render_scene_occlusion_cull", "false");
  rex::cvar::SetFlagByName("skate3_native_render_scene_occlusion_cull_guest", "false");
  rex::cvar::SetFlagByName("skate3_native_render_scene_ssao", "false");
  rex::cvar::SetFlagByName("skate3_native_render_scene_ssr", "false");
  rex::ui::vulkan::SetRuntimeHooks({state.get(),CreateInstance,SelectGpu,CreateDevice});
  REXLOG_INFO("VR: {} on {}",state->xr.headset(),state->xr.runtime());
}
void ShutdownGameVr() {
  std::lock_guard lock(mutex);
  rex::ui::vulkan::SetRuntimeHooks({}); state.reset();
}
void RecenterGameVr() {
  std::lock_guard lock(mutex);
  if(state) { state->recentered=false; state->capture_done=false; }
}
void CaptureGameVr() {
  std::lock_guard lock(mutex);
  if (state) {
    state->capture_done=false;
    rex::cvar::SetFlagByName("skate3_vr_capture","true");
    REXLOG_INFO("VR: stereo capture requested");
  }
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
bool GameVrEnabled() { return REXCVAR_GET(skate3_vr); }
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
  TrackingFrame frame; bool begun=false;
  try {
    if(!s.session) s.Bind(static_cast<rex::ui::vulkan::VulkanDevice*>(vulkan::NativeRhiGetVulkanDevice(input.device)));
    if(!s.session->PollEvents() || !s.session->BeginFrame(frame)) return s.BenchmarkDesktop(input,scene,render);
    begun=true;
    constexpr auto valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    if(!frame.should_render || (frame.view_flags&valid)!=valid) {
      s.session->EndFrame(frame); begun=false; return s.BenchmarkDesktop(input,scene,render);
    }
    if(!s.recentered) { s.reference=RecenterPose(frame.head.pose); s.recentered=true; }
    XrCompositionLayerProjectionView projections[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    bool rendered=false,world_rendered=false;
    auto context=input;
    const auto image=vulkan::NativeRhiGetImage(input.guest_output);
    const auto start=std::chrono::steady_clock::now();
    s.capture_menu=!scene && s.submitted%600==300;
    const bool capture=REXCVAR_GET(skate3_vr_capture) && (s.capture_menu ||
        (!s.capture_done && scene && scene->generation>300 && scene->items.size()>100));
    double render_ms[2]{},copy_ms[2]{};
    double scene_copy_ms[2]{};
    StereoShadows shadows;
    const bool compare=REXCVAR_GET(skate3_vr_compare_frame_work) && s.perf_samples<600;
    const bool share_work=compare ? s.perf_samples%2==0 : REXCVAR_GET(skate3_vr_share_frame_work);
    const auto scene_copy_start=std::chrono::steady_clock::now();
    auto render_scene=share_work && scene ? std::make_shared<native_scene::FrameScene>(*scene) : nullptr;
    const double shared_scene_copy_ms=render_scene ?
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-scene_copy_start).count() : 0;
    scene_copy_ms[0]=shared_scene_copy_ms;
    for(int eye=0;eye<2;++eye) {
      if(eye) context.cmd=vulkan::NativeRhiBeginFrame(context.device,image.image,image.view,true,context.guest_output_width,context.guest_output_height,&context.guest_output);
      EyeCamera camera; camera.scene=scene; camera.eye_index=eye;
      camera.shadows=share_work ? &shadows : nullptr;
      camera.render_scene=render_scene;
      if(scene && !scene->items.empty() && scene->proj[0]!=0) {
        MakeEyeCamera(scene->view_proj,scene->proj,scene->cam_pos,s.reference,frame.eyes[eye],float(REXCVAR_GET(skate3_vr_world_scale)),camera.view_proj,camera.proj,camera.position);
        camera.override_camera=true;
        MakeHudTransform(frame.head.pose,frame.eyes[eye],camera.proj,
            float(REXCVAR_GET(skate3_vr_hud_width)),float(context.guest_output_width)/float(context.guest_output_height),
            float(REXCVAR_GET(skate3_vr_hud_distance)),camera.hud_transform);
        if(capture) {
          const auto& pose=frame.eyes[eye].pose; const auto& fov=frame.eyes[eye].fov;
          REXLOG_INFO("VR: eye {} local=({:.4f},{:.4f},{:.4f}) fov=({:.4f},{:.4f},{:.4f},{:.4f}) world=({:.4f},{:.4f},{:.4f})",eye,pose.position.x,pose.position.y,pose.position.z,fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown,camera.position[0],camera.position[1],camera.position[2]);
        }
      }
      const auto render_start=std::chrono::steady_clock::now();
      if (s.timing) vulkan::NativeRhiTimestamp(context.device,s.timing,eye*2,true);
      rendered=render(context,&camera);
      if (s.timing) vulkan::NativeRhiTimestamp(context.device,s.timing,eye*2+1,false);
      world_rendered=world_rendered || camera.override_camera;
      render_ms[eye]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-render_start).count();
      if (!eye) render_ms[eye]+=shared_scene_copy_ms;
      scene_copy_ms[eye]+=camera.scene_copy_ms;
      if(!rendered) break;
      const auto copy_start=std::chrono::steady_clock::now();
      s.Transfer(context,eye,capture);
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
      if(capture && !s.capture_menu) { s.capture_done=true; REXLOG_INFO("VR: stereo capture same-scene={} distinct-pixels={}",scene->generation,s.eye_hashes[0]!=s.eye_hashes[1]); }
      XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION}; layer.space=s.session->local_space(); layer.viewCount=2; layer.views=projections;
      XrCompositionLayerQuad menu{XR_TYPE_COMPOSITION_LAYER_QUAD};
      menu.space=s.session->view_space(); menu.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
      menu.subImage=projections[0].subImage; menu.pose.orientation.w=1;
      const float distance=float(REXCVAR_GET(skate3_vr_menu_distance));
      const float width=float(REXCVAR_GET(skate3_vr_menu_width));
      if(!(distance>=0.5f && distance<=10 && width>=0.5f && width<=10)) throw std::runtime_error("Invalid VR menu size/distance");
      menu.pose.position.z=-distance;
      menu.size={width,width*float(context.guest_output_height)/float(context.guest_output_width)};
      const auto* header=world_rendered
          ? reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)
          : reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menu);
      s.session->EndFrame(frame,1,&header); begun=false;
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
      }
    } else { s.session->EndFrame(frame); begun=false; }
    return rendered;
  } catch(const std::exception& error) {
    if(begun) { try { s.session->EndFrame(frame); } catch(...) {} }
    s.failed=true; REXLOG_ERROR("VR: stereo rendering stopped: {}",error.what());
    try { if(s.session) s.session->RequestExit(); } catch(...) {}
    return render(input,nullptr);
  }
}
}
