#define VK_ENABLE_BETA_EXTENSIONS
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "settings_surface.h"
#include "editor.h"
#include <rex/ui/vulkan/presenter.h>
#include <rex/logging.h>
#include <rex/cvar.h>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <imgui_internal.h>
#include <cmath>

namespace skate3::vr {
namespace {
void Check(VkResult result,const char* operation) {
  if (result!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+": Vk "+std::to_string(result));
}
struct Surface {
  std::mutex mutex;
  std::unique_lock<std::mutex> painting;
  const rex::ui::vulkan::VulkanDevice* gpu=nullptr;
  SettingsImage image;
  VkDeviceMemory memory=VK_NULL_HANDLE;
  VkRenderPass pass=VK_NULL_HANDLE;
  VkFramebuffer framebuffer=VK_NULL_HANDLE;
  VkCommandPool pool=VK_NULL_HANDLE;
  VkCommandBuffer command=VK_NULL_HANDLE;
  VkFormat format=VK_FORMAT_UNDEFINED;
  std::unique_ptr<rex::ui::vulkan::VulkanUIDrawContext> context;
  bool pending=false,ready=false,failed=false,captured=false;
  uint32_t completed_frames=0;
  std::chrono::steady_clock::time_point last_fps_paint{};
  void Capture() {
    if (captured || completed_frames<30 || !rex::cvar::Query<bool>("skate3_vr_settings_test")) return;
    captured=true;
    const auto device=gpu->device();
    VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory host=VK_NULL_HANDLE; VkFence done=VK_NULL_HANDLE;
    try {
      if (format!=VK_FORMAT_B8G8R8A8_UNORM && format!=VK_FORMAT_R8G8B8A8_UNORM)
        throw std::runtime_error("Settings capture requires an 8-bit UNORM target");
      const VkDeviceSize bytes=VkDeviceSize(image.width)*image.height*4;
      VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size=bytes; info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      Check(vkCreateBuffer(device,&info,nullptr,&buffer),"Create settings capture buffer");
      VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device,buffer,&requirements);
      VkPhysicalDeviceMemoryProperties types; vkGetPhysicalDeviceMemoryProperties(gpu->physical_device(),&types);
      uint32_t type=0;
      constexpr auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      while (type<types.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<type)) ||
          (types.memoryTypes[type].propertyFlags&flags)!=flags)) ++type;
      if (type==types.memoryTypeCount) throw std::runtime_error("No settings capture memory");
      VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=type;
      Check(vkAllocateMemory(device,&allocation,nullptr,&host),"Allocate settings capture");
      Check(vkBindBufferMemory(device,buffer,host,0),"Bind settings capture");
      Check(vkResetCommandBuffer(command,0),"Reset settings capture commands");
      VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      Check(vkBeginCommandBuffer(command,&begin),"Begin settings capture");
      VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      barrier.image=image.image; barrier.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
      barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
      barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
      VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={image.width,image.height,1};
      vkCmdCopyImageToBuffer(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
      barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
      VkBufferMemoryBarrier to_host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
      to_host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; to_host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
      to_host.srcQueueFamilyIndex=to_host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; to_host.buffer=buffer; to_host.size=bytes;
      vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&to_host,0,nullptr);
      Check(vkEndCommandBuffer(command),"End settings capture");
      VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; Check(vkCreateFence(device,&fence_info,nullptr,&done),"Create settings capture fence");
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
      { auto queue=gpu->AcquireQueue(gpu->queue_family_graphics_compute(),0); Check(vkQueueSubmit(queue.queue(),1,&submit,done),"Submit settings capture"); }
      Check(vkWaitForFences(device,1,&done,VK_TRUE,UINT64_MAX),"Wait settings capture");
      void* pixels=nullptr; Check(vkMapMemory(device,host,0,bytes,0,&pixels),"Read settings capture");
      std::vector<char> rgb(size_t(image.width)*image.height*3);
      auto* source=static_cast<const unsigned char*>(pixels);
      for (size_t i=0;i<rgb.size()/3;++i) {
        rgb[i*3]=char(source[i*4+(format==VK_FORMAT_B8G8R8A8_UNORM?2:0)]);
        rgb[i*3+1]=char(source[i*4+1]); rgb[i*3+2]=char(source[i*4+(format==VK_FORMAT_B8G8R8A8_UNORM?0:2)]);
      }
      if (image.fps_only) {
        size_t transparent=0;
        for (size_t i=0;i<rgb.size()/3;++i) transparent+=source[i*4+3]==0;
        const auto& b=image.fps_region;
        REXLOG_INFO("VR: FPS capture bounds {},{} {}x{}; transparent pixels {}/{}",
            b.x,b.y,b.width,b.height,transparent,rgb.size()/3);
      }
      vkUnmapMemory(device,host);
      std::filesystem::create_directories("out"); std::ofstream file("out/vr-settings.ppm",std::ios::binary);
      file<<"P6\n"<<image.width<<" "<<image.height<<"\n255\n"; file.write(rgb.data(),rgb.size());
      if (!file) throw std::runtime_error("Cannot save settings capture");
      REXLOG_INFO("VR: settings surface captured to out/vr-settings.ppm ({}x{}); no headset validation",image.width,image.height);
    } catch (const std::exception& error) { REXLOG_ERROR("VR: settings diagnostic capture failed: {}",error.what()); }
    if (done) vkDestroyFence(device,done,nullptr);
    if (buffer) vkDestroyBuffer(device,buffer,nullptr);
    if (host) vkFreeMemory(device,host,nullptr);
  }
  void Destroy() {
    if (!gpu) return;
    const auto device=gpu->device();
    if (framebuffer) vkDestroyFramebuffer(device,framebuffer,nullptr);
    if (pass) vkDestroyRenderPass(device,pass,nullptr);
    if (image.view) vkDestroyImageView(device,image.view,nullptr);
    if (image.image) vkDestroyImage(device,image.image,nullptr);
    if (memory) vkFreeMemory(device,memory,nullptr);
    if (pool) vkDestroyCommandPool(device,pool,nullptr);
    framebuffer=VK_NULL_HANDLE; pass=VK_NULL_HANDLE; memory=VK_NULL_HANDLE;
    pool=VK_NULL_HANDLE; command=VK_NULL_HANDLE; image={}; ready=false;
    context.reset();
  }
  void Create(uint32_t width,uint32_t height,VkFormat target_format) {
    Destroy(); format=target_format;
    image.width=width; image.height=height;
    image.srgb=format==VK_FORMAT_R8G8B8A8_SRGB || format==VK_FORMAT_B8G8R8A8_SRGB;
    const auto device=gpu->device();
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType=VK_IMAGE_TYPE_2D; info.format=format; info.extent={width,height,1};
    info.mipLevels=1; info.arrayLayers=1; info.samples=VK_SAMPLE_COUNT_1_BIT;
    info.tiling=VK_IMAGE_TILING_OPTIMAL;
    info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    Check(vkCreateImage(device,&info,nullptr,&image.image),"Create VR settings image");
    VkMemoryRequirements requirements; vkGetImageMemoryRequirements(device,image.image,&requirements);
    VkPhysicalDeviceMemoryProperties types; vkGetPhysicalDeviceMemoryProperties(gpu->physical_device(),&types);
    uint32_t type=0;
    while (type<types.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<type)) ||
        !(types.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) ++type;
    if (type==types.memoryTypeCount) throw std::runtime_error("No VR settings image memory");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=type;
    Check(vkAllocateMemory(device,&allocation,nullptr,&memory),"Allocate VR settings image");
    Check(vkBindImageMemory(device,image.image,memory,0),"Bind VR settings image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image=image.image; view.viewType=VK_IMAGE_VIEW_TYPE_2D; view.format=format;
    view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    Check(vkCreateImageView(device,&view,nullptr,&image.view),"Create VR settings view");
    VkAttachmentDescription attachment{};
    attachment.format=format; attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1; subpass.pColorAttachments=&color;
    VkRenderPassCreateInfo render_pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass.attachmentCount=1; render_pass.pAttachments=&attachment;
    render_pass.subpassCount=1; render_pass.pSubpasses=&subpass;
    // Match the presenter's dependencies as well as its format: the existing
    // immediate-mode pipelines must be compatible with both render passes.
    VkSubpassDependency dependencies[2]{};
    dependencies[0]={VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_MEMORY_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_DEPENDENCY_BY_REGION_BIT};
    dependencies[1]={0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT,VK_DEPENDENCY_BY_REGION_BIT};
    render_pass.dependencyCount=2; render_pass.pDependencies=dependencies;
    Check(vkCreateRenderPass(device,&render_pass,nullptr,&pass),"Create VR settings render pass");
    VkFramebufferCreateInfo target{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    target.renderPass=pass; target.attachmentCount=1; target.pAttachments=&image.view;
    target.width=width; target.height=height; target.layers=1;
    Check(vkCreateFramebuffer(device,&target,nullptr,&framebuffer),"Create VR settings framebuffer");
    VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commands.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commands.queueFamilyIndex=gpu->queue_family_graphics_compute();
    Check(vkCreateCommandPool(device,&commands,nullptr,&pool),"Create VR settings command pool");
    VkCommandBufferAllocateInfo buffer{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    buffer.commandPool=pool; buffer.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; buffer.commandBufferCount=1;
    Check(vkAllocateCommandBuffers(device,&buffer,&command),"Allocate VR settings commands");
  }
} surface;
VkCommandBuffer Commands(void*) { return surface.pending ? surface.command : VK_NULL_HANDLE; }
void Submitted(void*,VkFence fence,VkResult result) {
  if (!surface.pending) return;
  if (result==VK_SUCCESS) {
    if (fence) result=vkWaitForFences(surface.gpu->device(),1,&fence,VK_TRUE,UINT64_MAX);
    else {
      auto queue=surface.gpu->AcquireQueue(surface.gpu->queue_family_graphics_compute(),0);
      result=vkQueueWaitIdle(queue.queue());
    }
  }
  surface.ready=result==VK_SUCCESS;
  if (surface.ready) { surface.image.generation=++surface.completed_frames; surface.Capture(); }
  if (!surface.ready) {
    surface.failed=true;
    REXLOG_ERROR("VR: settings submission failed: Vk {}",int(result));
  }
  surface.pending=false;
  surface.painting.unlock();
}
}
void InitializeSettingsSurface() {
  rex::ui::vulkan::VulkanPresenter::SetUiMirrorHooks({nullptr,Commands,Submitted});
}
void ShutdownSettingsSurface() {
  rex::ui::vulkan::VulkanPresenter::SetUiMirrorHooks({});
  std::lock_guard lock(surface.mutex);
  surface.Destroy(); surface.gpu=nullptr; surface.failed=false;
}
rex::ui::UIDrawContext* BeginSettingsSurface(rex::ui::UIDrawContext& desktop,bool system_dialog,bool fps_only) {
  surface.painting=std::unique_lock(surface.mutex);
  if (surface.failed) { surface.painting.unlock(); return nullptr; }
  const auto now=std::chrono::steady_clock::now();
  if (fps_only && now-surface.last_fps_paint<std::chrono::milliseconds(250)) {
    surface.painting.unlock(); return nullptr;
  }
  try {
    if (!desktop.presenter_or_null()) { surface.painting.unlock(); return nullptr; }
    auto& original=static_cast<rex::ui::vulkan::VulkanUIDrawContext&>(desktop);
    auto& presenter=static_cast<rex::ui::vulkan::VulkanPresenter&>(*desktop.presenter_or_null());
    surface.gpu=presenter.vulkan_device();
    const auto width=desktop.render_target_width(),height=desktop.render_target_height();
    if (!width || !height) { surface.painting.unlock(); return nullptr; }
    FpsPanelLayout fps_region;
    if (fps_only) {
      const auto* window=ImGui::FindWindowByName("##fps_overlay");
      if (!window || !window->Active) { surface.painting.unlock(); return nullptr; }
      const auto display=ImGui::GetIO().DisplaySize;
      if (display.x<=0 || display.y<=0) { surface.painting.unlock(); return nullptr; }
      const auto pixel=[](float value,uint32_t limit) {
        return uint32_t(std::clamp(value,0.f,float(limit)));
      };
      const auto x=pixel(std::floor(window->Pos.x*width/display.x),width);
      const auto y=pixel(std::floor(window->Pos.y*height/display.y),height);
      const auto right=pixel(std::ceil((window->Pos.x+window->Size.x)*width/display.x),width);
      const auto bottom=pixel(std::ceil((window->Pos.y+window->Size.y)*height/display.y),height);
      if (right<=x || bottom<=y) { surface.painting.unlock(); return nullptr; }
      fps_region={x,y,right-x,bottom-y};
    }
    if (surface.image.width!=width || surface.image.height!=height || surface.format!=original.render_pass_format())
      surface.Create(width,height,original.render_pass_format());
    surface.image.system_dialog=system_dialog;
    surface.image.fps_only=fps_only;
    surface.image.fps_region=fps_region;
    surface.image.editor_mode=system_dialog || fps_only ? 0 : VrEditorPanelMode();
    Check(vkResetCommandBuffer(surface.command,0),"Reset VR settings commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(surface.command,&begin),"Begin VR settings commands");
    VkClearValue clear{}; clear.color={{0.02f,0.025f,0.03f,1}};
    if (fps_only) clear.color={{0,0,0,0}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass=surface.pass; pass.framebuffer=surface.framebuffer;
    pass.renderArea.extent={width,height}; pass.clearValueCount=1; pass.pClearValues=&clear;
    vkCmdBeginRenderPass(surface.command,&pass,VK_SUBPASS_CONTENTS_INLINE);
    surface.context=std::make_unique<rex::ui::vulkan::VulkanUIDrawContext>(*desktop.presenter_or_null(),width,height,
        surface.command,original.submission_index_current(),original.submission_index_completed(),
        surface.pass,surface.format);
    if (fps_only) surface.last_fps_paint=now;
    return surface.context.get();
  } catch (const std::exception& error) {
    surface.failed=true; surface.ready=false;
    REXLOG_ERROR("VR: settings mirror unavailable: {}",error.what());
    surface.painting.unlock(); return nullptr;
  }
}
void EndSettingsSurface() {
  vkCmdEndRenderPass(surface.command);
  const auto result=vkEndCommandBuffer(surface.command);
  if (result!=VK_SUCCESS) {
    surface.failed=true; surface.ready=false;
    REXLOG_ERROR("VR: settings recording failed: Vk {}",int(result));
    surface.painting.unlock(); return;
  }
  // The presenter's submission includes uploads, desktop UI, then this pass.
  // Its completion fence covers all font/vertex buffers before reuse.
  surface.pending=true;
}
std::unique_lock<std::mutex> TryReadSettingsSurface(SettingsImage& image) {
  std::unique_lock lock(surface.mutex,std::try_to_lock);
  if (!lock || !surface.ready || surface.failed) return {};
  image=surface.image;
  return lock;
}
}
