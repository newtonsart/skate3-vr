#include "window_blend.h"
#include "vr_color_spirv.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition) { if(!condition) throw std::runtime_error("VR color GPU check failed"); }
void Vk(VkResult result) { Check(result==VK_SUCCESS); }
struct Image { VkImage handle{}; VkImageView view{}; VkDeviceMemory memory{}; };
struct TestGpu {
  VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
  VkCommandPool pool{}; VkCommandBuffer command{};
  VkDescriptorSetLayout descriptors{}; VkDescriptorPool descriptor_pool{}; VkDescriptorSet set{};
  VkPipelineLayout layout{}; VkPipeline pipeline{}; VkSampler sampler{};
  VkBuffer buffer{}; VkDeviceMemory memory{};
  std::array<Image,2> images{};
  static constexpr uint32_t size=64;
  ~TestGpu() {
    if(device) {
      vkDeviceWaitIdle(device);
      if(pipeline) vkDestroyPipeline(device,pipeline,nullptr);
      if(layout) vkDestroyPipelineLayout(device,layout,nullptr);
      if(descriptor_pool) vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
      if(descriptors) vkDestroyDescriptorSetLayout(device,descriptors,nullptr);
      if(sampler) vkDestroySampler(device,sampler,nullptr);
      for(auto image:images) {
        if(image.view) vkDestroyImageView(device,image.view,nullptr);
        if(image.handle) vkDestroyImage(device,image.handle,nullptr);
        if(image.memory) vkFreeMemory(device,image.memory,nullptr);
      }
      if(buffer) vkDestroyBuffer(device,buffer,nullptr);
      if(memory) vkFreeMemory(device,memory,nullptr);
      if(pool) vkDestroyCommandPool(device,pool,nullptr);
      vkDestroyDevice(device,nullptr);
    }
    if(instance) vkDestroyInstance(instance,nullptr);
  }
  uint32_t Memory(uint32_t bits,VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties; vkGetPhysicalDeviceMemoryProperties(physical,&properties);
    for(uint32_t i=0;i<properties.memoryTypeCount;++i)
      if((bits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&flags)==flags) return i;
    throw std::runtime_error("No suitable GPU test memory");
  }
  void Initialize() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&app;
    Vk(vkCreateInstance(&info,nullptr,&instance));
    uint32_t count=0; Vk(vkEnumeratePhysicalDevices(instance,&count,nullptr)); Check(count>0);
    std::vector<VkPhysicalDevice> devices(count); Vk(vkEnumeratePhysicalDevices(instance,&count,devices.data())); physical=devices[0];
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical,&properties);
    std::printf("VR color GPU: %s\n",properties.deviceName);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> queues(count); vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,queues.data());
    uint32_t family=0; while(family<count && !(queues[family].queueFlags&VK_QUEUE_COMPUTE_BIT)) ++family; Check(family<count);
    float priority=1; VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex=family; queue_info.queueCount=1; queue_info.pQueuePriorities=&priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; device_info.queueCreateInfoCount=1; device_info.pQueueCreateInfos=&queue_info;
    Vk(vkCreateDevice(physical,&device_info,nullptr,&device)); vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; commands.queueFamilyIndex=family; commands.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    Vk(vkCreateCommandPool(device,&commands,nullptr,&pool));
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocate.commandPool=pool; allocate.commandBufferCount=1; allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    Vk(vkAllocateCommandBuffers(device,&allocate,&command));
    for(auto& image:images) {
      VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; create.imageType=VK_IMAGE_TYPE_2D; create.format=VK_FORMAT_R8G8B8A8_UNORM;
      create.extent={size,size,1}; create.mipLevels=create.arrayLayers=1; create.samples=VK_SAMPLE_COUNT_1_BIT;
      create.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      Vk(vkCreateImage(device,&create,nullptr,&image.handle));
      VkMemoryRequirements required; vkGetImageMemoryRequirements(device,image.handle,&required);
      VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize=required.size;
      allocation.memoryTypeIndex=Memory(required.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      Vk(vkAllocateMemory(device,&allocation,nullptr,&image.memory)); Vk(vkBindImageMemory(device,image.handle,image.memory,0));
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=image.handle; view.viewType=VK_IMAGE_VIEW_TYPE_2D;
      view.format=create.format; view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; Vk(vkCreateImageView(device,&view,nullptr,&image.view));
    }
    VkBufferCreateInfo readback{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; readback.size=size*size*4; readback.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Vk(vkCreateBuffer(device,&readback,nullptr,&buffer)); VkMemoryRequirements required; vkGetBufferMemoryRequirements(device,buffer,&required);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize=required.size;
    allocation.memoryTypeIndex=Memory(required.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Vk(vkAllocateMemory(device,&allocation,nullptr,&memory)); Vk(vkBindBufferMemory(device,buffer,memory,0));
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo descriptor_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; descriptor_info.bindingCount=2; descriptor_info.pBindings=bindings;
    Vk(vkCreateDescriptorSetLayout(device,&descriptor_info,nullptr,&descriptors));
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool_info.maxSets=1; pool_info.poolSizeCount=2; pool_info.pPoolSizes=sizes;
    Vk(vkCreateDescriptorPool(device,&pool_info,nullptr,&descriptor_pool));
    VkDescriptorSetAllocateInfo descriptor_allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; descriptor_allocate.descriptorPool=descriptor_pool;
    descriptor_allocate.descriptorSetCount=1; descriptor_allocate.pSetLayouts=&descriptors; Vk(vkAllocateDescriptorSets(device,&descriptor_allocate,&set));
    VkSamplerCreateInfo sampling{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sampling.magFilter=sampling.minFilter=VK_FILTER_LINEAR;
    sampling.addressModeU=sampling.addressModeV=sampling.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Vk(vkCreateSampler(device,&sampling,nullptr,&sampler));
    VkDescriptorImageInfo image_info[2]={{sampler,images[0].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,images[1].view,VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[2]{};
    for(uint32_t i=0;i<2;++i) { writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet=set; writes[i].dstBinding=i;
      writes[i].descriptorCount=1; writes[i].descriptorType=bindings[i].descriptorType; writes[i].pImageInfo=&image_info[i]; }
    vkUpdateDescriptorSets(device,2,writes,0,nullptr);
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(skate3::vr::ColorParameters)};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; layout_info.setLayoutCount=1; layout_info.pSetLayouts=&descriptors;
    layout_info.pushConstantRangeCount=1; layout_info.pPushConstantRanges=&push; Vk(vkCreatePipelineLayout(device,&layout_info,nullptr,&layout));
    VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; shader_info.codeSize=sizeof(kVrColorSpirv); shader_info.pCode=kVrColorSpirv;
    VkShaderModule shader; Vk(vkCreateShaderModule(device,&shader_info,nullptr,&shader));
    VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; compute.layout=layout;
    compute.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; compute.stage.module=shader; compute.stage.pName="main";
    const auto result=vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline); vkDestroyShaderModule(device,shader,nullptr); Vk(result);
  }
  void Barrier(uint32_t image,VkImageLayout old_layout,VkImageLayout new_layout,VkAccessFlags src,VkAccessFlags dst) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image=images[image].handle;
    barrier.oldLayout=old_layout; barrier.newLayout=new_layout; barrier.srcAccessMask=src; barrier.dstAccessMask=dst;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
  }
  void Run(const skate3::vr::ColorParameters& parameters) {
    Vk(vkResetCommandBuffer(command,0)); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; Vk(vkBeginCommandBuffer(command,&begin));
    Barrier(0,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
    const VkClearColorValue color{{128.f/255,64.f/255,192.f/255,77.f/255}};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdClearColorImage(command,images[0].handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
    Barrier(0,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    Barrier(1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline); vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
    vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters); vkCmdDispatch(command,size/16,size/16,1);
    Barrier(1,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={size,size,1};
    vkCmdCopyImageToBuffer(command,images[1].handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
    Vk(vkEndCommandBuffer(command)); VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
    Vk(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); Vk(vkQueueWaitIdle(queue));
    void* mapped=nullptr; Vk(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped));
    const auto* pixels=static_cast<const uint8_t*>(mapped);
    bool correct=true;
    for(uint32_t y=0;y<size;++y) for(uint32_t x=0;x<size;++x) {
      auto smooth=[](float t) { t=std::clamp(t,0.f,1.f); return t*t*(3-2*t); };
      const float u=(x+0.5f)/size,v=(y+0.5f)/size;
      const float alpha=parameters.feather>0 ? smooth(std::min(u,1-u)*parameters.aspect/parameters.feather)*smooth(std::min(v,1-v)/parameters.feather) : 77.f/255;
      for(int channel=0;channel<4;++channel) {
        float expected=alpha;
        if(channel<3) {
          expected=color.float32[channel];
          if(parameters.decode_color) expected=expected<=0.04045f ? expected/12.92f : std::pow((expected+0.055f)/1.055f,2.4f);
          if(parameters.feather>0) expected*=alpha;
        }
        correct &= std::abs(pixels[(y*size+x)*4+channel]/255.f-expected)<2.f/255;
      }
    }
    vkUnmapMemory(device,memory); Check(correct);
  }
};
}
int main() {
  try {
    TestGpu gpu; gpu.Initialize();
    for(uint32_t decode : {0u,1u}) for(float feather : {0.f,0.04f,0.08f}) for(float aspect : {1.f,16.f/9,9.f/16})
      gpu.Run({decode,feather,aspect,0});
    std::puts("PASS: 18 GPU readbacks; opaque/feathered, gamma toggle, premultiplied alpha and panel aspects");
  } catch(const std::exception& error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
