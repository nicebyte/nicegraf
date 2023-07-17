#include "vk_10.h"
#include "ngf-common/macros.h"

#define TO_STRING(str) #str
#define STRINGIFY(str) TO_STRING(str)

#if defined(_WIN32) || defined(_WIN64)
#define VK_LOADER_LIB "vulkan-1.dll"
#else
#if defined(__APPLE__)
#define VK_LOADER_LIB "libMoltenVK.dylib"
#else
#define VK_LOADER_LIB "libvulkan.so.1"
#endif
#endif

PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
PFN_vkCreateInstance vkCreateInstance;

PFN_vkCreateDevice vkCreateDevice;
PFN_vkDestroyInstance vkDestroyInstance;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
#if !defined(__APPLE__)
VK_GET_DEVICE_PRES_FN_TYPE VK_GET_DEVICE_PRES_FN;
#endif
VK_CREATE_SURFACE_FN_TYPE VK_CREATE_SURFACE_FN;
PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;

PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
PFN_vkAllocateMemory vkAllocateMemory;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
PFN_vkBindBufferMemory vkBindBufferMemory;
PFN_vkBindImageMemory vkBindImageMemory;
PFN_vkCmdBeginQuery vkCmdBeginQuery;
PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
PFN_vkCmdBindPipeline vkCmdBindPipeline;
PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
PFN_vkCmdBlitImage vkCmdBlitImage;
PFN_vkCmdClearAttachments vkCmdClearAttachments;
PFN_vkCmdClearColorImage vkCmdClearColorImage;
PFN_vkCmdClearDepthStencilImage vkCmdClearDepthStencilImage;
PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
PFN_vkCmdCopyImage vkCmdCopyImage;
PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults;
PFN_vkCmdDispatch vkCmdDispatch;
PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect;
PFN_vkCmdDraw vkCmdDraw;
PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect;
PFN_vkCmdDrawIndirect vkCmdDrawIndirect;
PFN_vkCmdEndQuery vkCmdEndQuery;
PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
PFN_vkCmdExecuteCommands vkCmdExecuteCommands;
PFN_vkCmdFillBuffer vkCmdFillBuffer;
PFN_vkCmdNextSubpass vkCmdNextSubpass;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
PFN_vkCmdPushConstants vkCmdPushConstants;
PFN_vkCmdResetEvent vkCmdResetEvent;
PFN_vkCmdResetQueryPool vkCmdResetQueryPool;
PFN_vkCmdResolveImage vkCmdResolveImage;
PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants;
PFN_vkCmdSetDepthBias vkCmdSetDepthBias;
PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds;
PFN_vkCmdSetEvent vkCmdSetEvent;
PFN_vkCmdSetLineWidth vkCmdSetLineWidth;
PFN_vkCmdSetScissor vkCmdSetScissor;
PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask;
PFN_vkCmdSetStencilReference vkCmdSetStencilReference;
PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask;
PFN_vkCmdSetViewport vkCmdSetViewport;
PFN_vkCmdUpdateBuffer vkCmdUpdateBuffer;
PFN_vkCmdWaitEvents vkCmdWaitEvents;
PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp;
PFN_vkCreateBuffer vkCreateBuffer;
PFN_vkCreateBufferView vkCreateBufferView;
PFN_vkCreateCommandPool vkCreateCommandPool;
PFN_vkCreateComputePipelines vkCreateComputePipelines;
PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
PFN_vkCreateEvent vkCreateEvent;
PFN_vkCreateFence vkCreateFence;
PFN_vkCreateFramebuffer vkCreateFramebuffer;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
PFN_vkCreateImage vkCreateImage;
PFN_vkCreateImageView vkCreateImageView;
PFN_vkCreatePipelineCache vkCreatePipelineCache;
PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
PFN_vkCreateQueryPool vkCreateQueryPool;
PFN_vkCreateRenderPass vkCreateRenderPass;
PFN_vkCreateSampler vkCreateSampler;
PFN_vkCreateSemaphore vkCreateSemaphore;
PFN_vkCreateShaderModule vkCreateShaderModule;
PFN_vkDestroyBuffer vkDestroyBuffer;
PFN_vkDestroyBufferView vkDestroyBufferView;
PFN_vkDestroyCommandPool vkDestroyCommandPool;
PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
PFN_vkDestroyDevice vkDestroyDevice;
PFN_vkDestroyEvent vkDestroyEvent;
PFN_vkDestroyFence vkDestroyFence;
PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
PFN_vkDestroyImage vkDestroyImage;
PFN_vkDestroyImageView vkDestroyImageView;
PFN_vkDestroyPipeline vkDestroyPipeline;
PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
PFN_vkDestroyQueryPool vkDestroyQueryPool;
PFN_vkDestroyRenderPass vkDestroyRenderPass;
PFN_vkDestroySampler vkDestroySampler;
PFN_vkDestroySemaphore vkDestroySemaphore;
PFN_vkDestroyShaderModule vkDestroyShaderModule;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
PFN_vkEndCommandBuffer vkEndCommandBuffer;
PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
PFN_vkFreeMemory vkFreeMemory;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
PFN_vkGetDeviceMemoryCommitment vkGetDeviceMemoryCommitment;
PFN_vkGetDeviceQueue vkGetDeviceQueue;
PFN_vkGetEventStatus vkGetEventStatus;
PFN_vkGetFenceStatus vkGetFenceStatus;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
PFN_vkGetImageSparseMemoryRequirements vkGetImageSparseMemoryRequirements;
PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;
PFN_vkGetPipelineCacheData vkGetPipelineCacheData;
PFN_vkGetQueryPoolResults vkGetQueryPoolResults;
PFN_vkGetRenderAreaGranularity vkGetRenderAreaGranularity;
PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
PFN_vkMapMemory vkMapMemory;
PFN_vkMergePipelineCaches vkMergePipelineCaches;
PFN_vkQueueBindSparse vkQueueBindSparse;
PFN_vkQueueSubmit vkQueueSubmit;
PFN_vkQueueWaitIdle vkQueueWaitIdle;
PFN_vkResetCommandBuffer vkResetCommandBuffer;
PFN_vkResetCommandPool vkResetCommandPool;
PFN_vkResetDescriptorPool vkResetDescriptorPool;
PFN_vkResetEvent vkResetEvent;
PFN_vkResetFences vkResetFences;
PFN_vkSetEvent vkSetEvent;
PFN_vkUnmapMemory vkUnmapMemory;
PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
PFN_vkWaitForFences vkWaitForFences;
PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
PFN_vkQueuePresentKHR vkQueuePresentKHR;
PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR;
PFN_vkDestroyDebugUtilsMessengerEXT    vkDestroyDebugUtilsMessengerEXT;

bool vkl_init_loader(void) {
  ModuleHandle vkdll = LoadLibraryA(VK_LOADER_LIB);

  if (!vkdll) {
    return false;
  }

  vkGetInstanceProcAddr =
    (PFN_vkGetInstanceProcAddr)GetProcAddress(vkdll,
      "vkGetInstanceProcAddr");
  vkCreateInstance =
    (PFN_vkCreateInstance)vkGetInstanceProcAddr(VK_NULL_HANDLE,
      "vkCreateInstance");
  vkEnumerateInstanceLayerProperties =
    (PFN_vkEnumerateInstanceLayerProperties)vkGetInstanceProcAddr(VK_NULL_HANDLE,
        "vkEnumerateInstanceLayerProperties");
  return true;
}
#if !defined(__APPLE__)
extern VK_GET_DEVICE_PRES_FN_TYPE VK_GET_DEVICE_PRES_FN;
#endif
extern VK_CREATE_SURFACE_FN_TYPE VK_CREATE_SURFACE_FN;
void vkl_init_instance(VkInstance inst) {
  vkCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(inst, "vkCreateDevice");
  vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(inst, "vkDestroyInstance");
  vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)vkGetInstanceProcAddr(inst, "vkEnumerateDeviceExtensionProperties");
  vkEnumerateDeviceLayerProperties = (PFN_vkEnumerateDeviceLayerProperties)vkGetInstanceProcAddr(inst, "vkEnumerateDeviceLayerProperties");
  vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(inst, "vkEnumeratePhysicalDevices");
  vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(inst, "vkGetDeviceProcAddr");
  vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceFeatures");
  vkGetPhysicalDeviceFormatProperties = (PFN_vkGetPhysicalDeviceFormatProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceFormatProperties");
  vkGetPhysicalDeviceImageFormatProperties = (PFN_vkGetPhysicalDeviceImageFormatProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceImageFormatProperties");
  vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceMemoryProperties");
  vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceProperties");
  vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceQueueFamilyProperties");
  vkGetPhysicalDeviceSparseImageFormatProperties = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceSparseImageFormatProperties");
#if !defined(__APPLE__)
  VK_GET_DEVICE_PRES_FN = (VK_GET_DEVICE_PRES_FN_TYPE)vkGetInstanceProcAddr(inst, STRINGIFY(VK_GET_DEVICE_PRES_FN));
#endif
  VK_CREATE_SURFACE_FN = (VK_CREATE_SURFACE_FN_TYPE)vkGetInstanceProcAddr(inst, STRINGIFY(VK_CREATE_SURFACE_FN));
  vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(inst, "vkDestroySurfaceKHR");
  vkGetPhysicalDeviceSurfaceSupportKHR =
    (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vkGetInstanceProcAddr(
      inst,
      "vkGetPhysicalDeviceSurfaceSupportKHR");
  vkCreateDebugUtilsMessengerEXT =
   (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT");
  vkGetPhysicalDeviceSurfacePresentModesKHR = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceSurfacePresentModesKHR");
  vkGetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceSurfaceFormatsKHR");
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  vkGetPhysicalDeviceFeatures2KHR = (PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceFeatures2KHR");
  vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      inst,
      "vkDestroyDebugUtilsMessengerEXT");
}

void vkl_init_device(VkDevice dev) {
  vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)vkGetDeviceProcAddr(dev, "vkAllocateCommandBuffers");
  vkAllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)vkGetDeviceProcAddr(dev, "vkAllocateDescriptorSets");
  vkAllocateMemory = (PFN_vkAllocateMemory)vkGetDeviceProcAddr(dev, "vkAllocateMemory");
  vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vkGetDeviceProcAddr(dev, "vkBeginCommandBuffer");
  vkBindBufferMemory = (PFN_vkBindBufferMemory)vkGetDeviceProcAddr(dev, "vkBindBufferMemory");
  vkBindImageMemory = (PFN_vkBindImageMemory)vkGetDeviceProcAddr(dev, "vkBindImageMemory");
  vkCmdBeginQuery = (PFN_vkCmdBeginQuery)vkGetDeviceProcAddr(dev, "vkCmdBeginQuery");
  vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)vkGetDeviceProcAddr(dev, "vkCmdBeginRenderPass");
  vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)vkGetDeviceProcAddr(dev, "vkCmdBindDescriptorSets");
  vkCmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer)vkGetDeviceProcAddr(dev, "vkCmdBindIndexBuffer");
  vkCmdBindPipeline = (PFN_vkCmdBindPipeline)vkGetDeviceProcAddr(dev, "vkCmdBindPipeline");
  vkCmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers)vkGetDeviceProcAddr(dev, "vkCmdBindVertexBuffers");
  vkCmdBlitImage = (PFN_vkCmdBlitImage)vkGetDeviceProcAddr(dev, "vkCmdBlitImage");
  vkCmdClearAttachments = (PFN_vkCmdClearAttachments)vkGetDeviceProcAddr(dev, "vkCmdClearAttachments");
  vkCmdClearColorImage = (PFN_vkCmdClearColorImage)vkGetDeviceProcAddr(dev, "vkCmdClearColorImage");
  vkCmdClearDepthStencilImage = (PFN_vkCmdClearDepthStencilImage)vkGetDeviceProcAddr(dev, "vkCmdClearDepthStencilImage");
  vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer)vkGetDeviceProcAddr(dev, "vkCmdCopyBuffer");
  vkCmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)vkGetDeviceProcAddr(dev, "vkCmdCopyBufferToImage");
  vkCmdCopyImage = (PFN_vkCmdCopyImage)vkGetDeviceProcAddr(dev, "vkCmdCopyImage");
  vkCmdCopyImageToBuffer = (PFN_vkCmdCopyImageToBuffer)vkGetDeviceProcAddr(dev, "vkCmdCopyImageToBuffer");
  vkCmdCopyQueryPoolResults = (PFN_vkCmdCopyQueryPoolResults)vkGetDeviceProcAddr(dev, "vkCmdCopyQueryPoolResults");
  vkCmdDispatch = (PFN_vkCmdDispatch)vkGetDeviceProcAddr(dev, "vkCmdDispatch");
  vkCmdDispatchIndirect = (PFN_vkCmdDispatchIndirect)vkGetDeviceProcAddr(dev, "vkCmdDispatchIndirect");
  vkCmdDraw = (PFN_vkCmdDraw)vkGetDeviceProcAddr(dev, "vkCmdDraw");
  vkCmdDrawIndexed = (PFN_vkCmdDrawIndexed)vkGetDeviceProcAddr(dev, "vkCmdDrawIndexed");
  vkCmdDrawIndexedIndirect = (PFN_vkCmdDrawIndexedIndirect)vkGetDeviceProcAddr(dev, "vkCmdDrawIndexedIndirect");
  vkCmdDrawIndirect = (PFN_vkCmdDrawIndirect)vkGetDeviceProcAddr(dev, "vkCmdDrawIndirect");
  vkCmdEndQuery = (PFN_vkCmdEndQuery)vkGetDeviceProcAddr(dev, "vkCmdEndQuery");
  vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)vkGetDeviceProcAddr(dev, "vkCmdEndRenderPass");
  vkCmdExecuteCommands = (PFN_vkCmdExecuteCommands)vkGetDeviceProcAddr(dev, "vkCmdExecuteCommands");
  vkCmdFillBuffer = (PFN_vkCmdFillBuffer)vkGetDeviceProcAddr(dev, "vkCmdFillBuffer");
  vkCmdNextSubpass = (PFN_vkCmdNextSubpass)vkGetDeviceProcAddr(dev, "vkCmdNextSubpass");
  vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)vkGetDeviceProcAddr(dev, "vkCmdPipelineBarrier");
  vkCmdPushConstants = (PFN_vkCmdPushConstants)vkGetDeviceProcAddr(dev, "vkCmdPushConstants");
  vkCmdResetEvent = (PFN_vkCmdResetEvent)vkGetDeviceProcAddr(dev, "vkCmdResetEvent");
  vkCmdResetQueryPool = (PFN_vkCmdResetQueryPool)vkGetDeviceProcAddr(dev, "vkCmdResetQueryPool");
  vkCmdResolveImage = (PFN_vkCmdResolveImage)vkGetDeviceProcAddr(dev, "vkCmdResolveImage");
  vkCmdSetBlendConstants = (PFN_vkCmdSetBlendConstants)vkGetDeviceProcAddr(dev, "vkCmdSetBlendConstants");
  vkCmdSetDepthBias = (PFN_vkCmdSetDepthBias)vkGetDeviceProcAddr(dev, "vkCmdSetDepthBias");
  vkCmdSetDepthBounds = (PFN_vkCmdSetDepthBounds)vkGetDeviceProcAddr(dev, "vkCmdSetDepthBounds");
  vkCmdSetEvent = (PFN_vkCmdSetEvent)vkGetDeviceProcAddr(dev, "vkCmdSetEvent");
  vkCmdSetLineWidth = (PFN_vkCmdSetLineWidth)vkGetDeviceProcAddr(dev, "vkCmdSetLineWidth");
  vkCmdSetScissor = (PFN_vkCmdSetScissor)vkGetDeviceProcAddr(dev, "vkCmdSetScissor");
  vkCmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask)vkGetDeviceProcAddr(dev, "vkCmdSetStencilCompareMask");
  vkCmdSetStencilReference = (PFN_vkCmdSetStencilReference)vkGetDeviceProcAddr(dev, "vkCmdSetStencilReference");
  vkCmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask)vkGetDeviceProcAddr(dev, "vkCmdSetStencilWriteMask");
  vkCmdSetViewport = (PFN_vkCmdSetViewport)vkGetDeviceProcAddr(dev, "vkCmdSetViewport");
  vkCmdUpdateBuffer = (PFN_vkCmdUpdateBuffer)vkGetDeviceProcAddr(dev, "vkCmdUpdateBuffer");
  vkCmdWaitEvents = (PFN_vkCmdWaitEvents)vkGetDeviceProcAddr(dev, "vkCmdWaitEvents");
  vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)vkGetDeviceProcAddr(dev, "vkCmdWriteTimestamp");
  vkCreateBuffer = (PFN_vkCreateBuffer)vkGetDeviceProcAddr(dev, "vkCreateBuffer");
  vkCreateBufferView = (PFN_vkCreateBufferView)vkGetDeviceProcAddr(dev, "vkCreateBufferView");
  vkCreateCommandPool = (PFN_vkCreateCommandPool)vkGetDeviceProcAddr(dev, "vkCreateCommandPool");
  vkCreateComputePipelines = (PFN_vkCreateComputePipelines)vkGetDeviceProcAddr(dev, "vkCreateComputePipelines");
  vkCreateDescriptorPool = (PFN_vkCreateDescriptorPool)vkGetDeviceProcAddr(dev, "vkCreateDescriptorPool");
  vkCreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)vkGetDeviceProcAddr(dev, "vkCreateDescriptorSetLayout");
  vkCreateEvent = (PFN_vkCreateEvent)vkGetDeviceProcAddr(dev, "vkCreateEvent");
  vkCreateFence = (PFN_vkCreateFence)vkGetDeviceProcAddr(dev, "vkCreateFence");
  vkCreateFramebuffer = (PFN_vkCreateFramebuffer)vkGetDeviceProcAddr(dev, "vkCreateFramebuffer");
  vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)vkGetDeviceProcAddr(dev, "vkCreateGraphicsPipelines");
  vkCreateImage = (PFN_vkCreateImage)vkGetDeviceProcAddr(dev, "vkCreateImage");
  vkCreateImageView = (PFN_vkCreateImageView)vkGetDeviceProcAddr(dev, "vkCreateImageView");
  vkCreatePipelineCache = (PFN_vkCreatePipelineCache)vkGetDeviceProcAddr(dev, "vkCreatePipelineCache");
  vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)vkGetDeviceProcAddr(dev, "vkCreatePipelineLayout");
  vkCreateQueryPool = (PFN_vkCreateQueryPool)vkGetDeviceProcAddr(dev, "vkCreateQueryPool");
  vkCreateRenderPass = (PFN_vkCreateRenderPass)vkGetDeviceProcAddr(dev, "vkCreateRenderPass");
  vkCreateSampler = (PFN_vkCreateSampler)vkGetDeviceProcAddr(dev, "vkCreateSampler");
  vkCreateSemaphore = (PFN_vkCreateSemaphore)vkGetDeviceProcAddr(dev, "vkCreateSemaphore");
  vkCreateShaderModule = (PFN_vkCreateShaderModule)vkGetDeviceProcAddr(dev, "vkCreateShaderModule");
  vkDestroyBuffer = (PFN_vkDestroyBuffer)vkGetDeviceProcAddr(dev, "vkDestroyBuffer");
  vkDestroyBufferView = (PFN_vkDestroyBufferView)vkGetDeviceProcAddr(dev, "vkDestroyBufferView");
  vkDestroyCommandPool = (PFN_vkDestroyCommandPool)vkGetDeviceProcAddr(dev, "vkDestroyCommandPool");
  vkDestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)vkGetDeviceProcAddr(dev, "vkDestroyDescriptorPool");
  vkDestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)vkGetDeviceProcAddr(dev, "vkDestroyDescriptorSetLayout");
  vkDestroyDevice = (PFN_vkDestroyDevice)vkGetDeviceProcAddr(dev, "vkDestroyDevice");
  vkDestroyEvent = (PFN_vkDestroyEvent)vkGetDeviceProcAddr(dev, "vkDestroyEvent");
  vkDestroyFence = (PFN_vkDestroyFence)vkGetDeviceProcAddr(dev, "vkDestroyFence");
  vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)vkGetDeviceProcAddr(dev, "vkDestroyFramebuffer");
  vkDestroyImage = (PFN_vkDestroyImage)vkGetDeviceProcAddr(dev, "vkDestroyImage");
  vkDestroyImageView = (PFN_vkDestroyImageView)vkGetDeviceProcAddr(dev, "vkDestroyImageView");
  vkDestroyPipeline = (PFN_vkDestroyPipeline)vkGetDeviceProcAddr(dev, "vkDestroyPipeline");
  vkDestroyPipelineCache = (PFN_vkDestroyPipelineCache)vkGetDeviceProcAddr(dev, "vkDestroyPipelineCache");
  vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)vkGetDeviceProcAddr(dev, "vkDestroyPipelineLayout");
  vkDestroyQueryPool = (PFN_vkDestroyQueryPool)vkGetDeviceProcAddr(dev, "vkDestroyQueryPool");
  vkDestroyRenderPass = (PFN_vkDestroyRenderPass)vkGetDeviceProcAddr(dev, "vkDestroyRenderPass");
  vkDestroySampler = (PFN_vkDestroySampler)vkGetDeviceProcAddr(dev, "vkDestroySampler");
  vkDestroySemaphore = (PFN_vkDestroySemaphore)vkGetDeviceProcAddr(dev, "vkDestroySemaphore");
  vkDestroyShaderModule = (PFN_vkDestroyShaderModule)vkGetDeviceProcAddr(dev, "vkDestroyShaderModule");
  vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vkGetDeviceProcAddr(dev, "vkDeviceWaitIdle");
  vkEndCommandBuffer = (PFN_vkEndCommandBuffer)vkGetDeviceProcAddr(dev, "vkEndCommandBuffer");
  vkFlushMappedMemoryRanges = (PFN_vkFlushMappedMemoryRanges)vkGetDeviceProcAddr(dev, "vkFlushMappedMemoryRanges");
  vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)vkGetDeviceProcAddr(dev, "vkFreeCommandBuffers");
  vkFreeDescriptorSets = (PFN_vkFreeDescriptorSets)vkGetDeviceProcAddr(dev, "vkFreeDescriptorSets");
  vkFreeMemory = (PFN_vkFreeMemory)vkGetDeviceProcAddr(dev, "vkFreeMemory");
  vkGetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)vkGetDeviceProcAddr(dev, "vkGetBufferMemoryRequirements");
  vkGetDeviceMemoryCommitment = (PFN_vkGetDeviceMemoryCommitment)vkGetDeviceProcAddr(dev, "vkGetDeviceMemoryCommitment");
  vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetDeviceProcAddr(dev, "vkGetDeviceQueue");
  vkGetEventStatus = (PFN_vkGetEventStatus)vkGetDeviceProcAddr(dev, "vkGetEventStatus");
  vkGetFenceStatus = (PFN_vkGetFenceStatus)vkGetDeviceProcAddr(dev, "vkGetFenceStatus");
  vkGetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)vkGetDeviceProcAddr(dev, "vkGetImageMemoryRequirements");
  vkGetImageSparseMemoryRequirements = (PFN_vkGetImageSparseMemoryRequirements)vkGetDeviceProcAddr(dev, "vkGetImageSparseMemoryRequirements");
  vkGetImageSubresourceLayout = (PFN_vkGetImageSubresourceLayout)vkGetDeviceProcAddr(dev, "vkGetImageSubresourceLayout");
  vkGetPipelineCacheData = (PFN_vkGetPipelineCacheData)vkGetDeviceProcAddr(dev, "vkGetPipelineCacheData");
  vkGetQueryPoolResults = (PFN_vkGetQueryPoolResults)vkGetDeviceProcAddr(dev, "vkGetQueryPoolResults");
  vkGetRenderAreaGranularity = (PFN_vkGetRenderAreaGranularity)vkGetDeviceProcAddr(dev, "vkGetRenderAreaGranularity");
  vkInvalidateMappedMemoryRanges = (PFN_vkInvalidateMappedMemoryRanges)vkGetDeviceProcAddr(dev, "vkInvalidateMappedMemoryRanges");
  vkMapMemory = (PFN_vkMapMemory)vkGetDeviceProcAddr(dev, "vkMapMemory");
  vkMergePipelineCaches = (PFN_vkMergePipelineCaches)vkGetDeviceProcAddr(dev, "vkMergePipelineCaches");
  vkQueueBindSparse = (PFN_vkQueueBindSparse)vkGetDeviceProcAddr(dev, "vkQueueBindSparse");
  vkQueueSubmit = (PFN_vkQueueSubmit)vkGetDeviceProcAddr(dev, "vkQueueSubmit");
  vkQueueWaitIdle = (PFN_vkQueueWaitIdle)vkGetDeviceProcAddr(dev, "vkQueueWaitIdle");
  vkResetCommandBuffer = (PFN_vkResetCommandBuffer)vkGetDeviceProcAddr(dev, "vkResetCommandBuffer");
  vkResetCommandPool = (PFN_vkResetCommandPool)vkGetDeviceProcAddr(dev, "vkResetCommandPool");
  vkResetDescriptorPool = (PFN_vkResetDescriptorPool)vkGetDeviceProcAddr(dev, "vkResetDescriptorPool");
  vkResetEvent = (PFN_vkResetEvent)vkGetDeviceProcAddr(dev, "vkResetEvent");
  vkResetFences = (PFN_vkResetFences)vkGetDeviceProcAddr(dev, "vkResetFences");
  vkSetEvent = (PFN_vkSetEvent)vkGetDeviceProcAddr(dev, "vkSetEvent");
  vkUnmapMemory = (PFN_vkUnmapMemory)vkGetDeviceProcAddr(dev, "vkUnmapMemory");
  vkUpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)vkGetDeviceProcAddr(dev, "vkUpdateDescriptorSets");
  vkWaitForFences = (PFN_vkWaitForFences)vkGetDeviceProcAddr(dev, "vkWaitForFences");
  vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(dev, "vkCreateSwapchainKHR");
  vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(dev, "vkDestroySwapchainKHR");
  vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(dev, "vkGetSwapchainImagesKHR");
  vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)vkGetDeviceProcAddr(dev, "vkAcquireNextImageKHR");
  vkQueuePresentKHR = (PFN_vkQueuePresentKHR)vkGetDeviceProcAddr(dev, "vkQueuePresentKHR");
}

