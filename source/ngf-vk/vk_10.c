#include "vk_10.h"
#include "ngf-common/macros.h"

#define TO_STRING(str) #str
#define STRINGIFY(str) TO_STRING(str)
#if defined(_WIN32) || defined(_WIN64)
#define VK_LOADER_LIB "vulkan-1.dll"
#define VK_HIDE_SYMBOL
#else
#define VK_HIDE_SYMBOL __attribute__((visibility("hidden")))
#if defined(__APPLE__)
#define VK_LOADER_LIB "libMoltenVK.dylib"
#else
#define VK_LOADER_LIB "libvulkan.so.1"
#endif
#endif
#ifdef __clang__
#pragma clang diagnostic ignored "-Wnullability-completeness"
#if __has_warning("-Wcast-function-type-mismatch")
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#endif
#endif

VK_HIDE_SYMBOL PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
VK_HIDE_SYMBOL PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
VK_HIDE_SYMBOL PFN_vkCreateInstance vkCreateInstance;

VK_HIDE_SYMBOL PFN_vkCreateDevice vkCreateDevice;
VK_HIDE_SYMBOL PFN_vkDestroyInstance vkDestroyInstance;
VK_HIDE_SYMBOL PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
VK_HIDE_SYMBOL PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties;
VK_HIDE_SYMBOL PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
VK_HIDE_SYMBOL PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
#if !defined(__APPLE__)
VK_HIDE_SYMBOL VK_GET_DEVICE_PRES_FN_TYPE VK_GET_DEVICE_PRES_FN;
#endif
VK_HIDE_SYMBOL VK_CREATE_SURFACE_FN_TYPE VK_CREATE_SURFACE_FN;
VK_HIDE_SYMBOL PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
VK_HIDE_SYMBOL PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
VK_HIDE_SYMBOL PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT;
VK_HIDE_SYMBOL PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT;

VK_HIDE_SYMBOL PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
VK_HIDE_SYMBOL PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
VK_HIDE_SYMBOL PFN_vkAllocateMemory vkAllocateMemory;
VK_HIDE_SYMBOL PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
VK_HIDE_SYMBOL PFN_vkBindBufferMemory vkBindBufferMemory;
VK_HIDE_SYMBOL PFN_vkBindImageMemory vkBindImageMemory;
VK_HIDE_SYMBOL PFN_vkCmdBeginQuery vkCmdBeginQuery;
VK_HIDE_SYMBOL PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
VK_HIDE_SYMBOL PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
VK_HIDE_SYMBOL PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
VK_HIDE_SYMBOL PFN_vkCmdBindPipeline vkCmdBindPipeline;
VK_HIDE_SYMBOL PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
VK_HIDE_SYMBOL PFN_vkCmdBlitImage vkCmdBlitImage;
VK_HIDE_SYMBOL PFN_vkCmdClearAttachments vkCmdClearAttachments;
VK_HIDE_SYMBOL PFN_vkCmdClearColorImage vkCmdClearColorImage;
VK_HIDE_SYMBOL PFN_vkCmdClearDepthStencilImage vkCmdClearDepthStencilImage;
VK_HIDE_SYMBOL PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
VK_HIDE_SYMBOL PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
VK_HIDE_SYMBOL PFN_vkCmdCopyImage vkCmdCopyImage;
VK_HIDE_SYMBOL PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
VK_HIDE_SYMBOL PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults;
VK_HIDE_SYMBOL PFN_vkCmdDispatch vkCmdDispatch;
VK_HIDE_SYMBOL PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect;
VK_HIDE_SYMBOL PFN_vkCmdDraw vkCmdDraw;
VK_HIDE_SYMBOL PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
VK_HIDE_SYMBOL PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect;
VK_HIDE_SYMBOL PFN_vkCmdDrawIndirect vkCmdDrawIndirect;
VK_HIDE_SYMBOL PFN_vkCmdEndQuery vkCmdEndQuery;
VK_HIDE_SYMBOL PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
VK_HIDE_SYMBOL PFN_vkCmdExecuteCommands vkCmdExecuteCommands;
VK_HIDE_SYMBOL PFN_vkCmdFillBuffer vkCmdFillBuffer;
VK_HIDE_SYMBOL PFN_vkCmdNextSubpass vkCmdNextSubpass;
VK_HIDE_SYMBOL PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
VK_HIDE_SYMBOL PFN_vkCmdPipelineBarrier2 vkCmdPipelineBarrier2;
VK_HIDE_SYMBOL PFN_vkCmdPushConstants vkCmdPushConstants;
VK_HIDE_SYMBOL PFN_vkCmdResetEvent vkCmdResetEvent;
VK_HIDE_SYMBOL PFN_vkCmdResetQueryPool vkCmdResetQueryPool;
VK_HIDE_SYMBOL PFN_vkCmdResolveImage vkCmdResolveImage;
VK_HIDE_SYMBOL PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants;
VK_HIDE_SYMBOL PFN_vkCmdSetDepthBias vkCmdSetDepthBias;
VK_HIDE_SYMBOL PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds;
VK_HIDE_SYMBOL PFN_vkCmdSetEvent vkCmdSetEvent;
VK_HIDE_SYMBOL PFN_vkCmdSetLineWidth vkCmdSetLineWidth;
VK_HIDE_SYMBOL PFN_vkCmdSetScissor vkCmdSetScissor;
VK_HIDE_SYMBOL PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask;
VK_HIDE_SYMBOL PFN_vkCmdSetStencilReference vkCmdSetStencilReference;
VK_HIDE_SYMBOL PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask;
VK_HIDE_SYMBOL PFN_vkCmdSetViewport vkCmdSetViewport;
VK_HIDE_SYMBOL PFN_vkCmdUpdateBuffer vkCmdUpdateBuffer;
VK_HIDE_SYMBOL PFN_vkCmdWaitEvents vkCmdWaitEvents;
VK_HIDE_SYMBOL PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp;
VK_HIDE_SYMBOL PFN_vkCreateBuffer vkCreateBuffer;
VK_HIDE_SYMBOL PFN_vkCreateBufferView vkCreateBufferView;
VK_HIDE_SYMBOL PFN_vkCreateCommandPool vkCreateCommandPool;
VK_HIDE_SYMBOL PFN_vkCreateComputePipelines vkCreateComputePipelines;
VK_HIDE_SYMBOL PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
VK_HIDE_SYMBOL PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
VK_HIDE_SYMBOL PFN_vkCreateEvent vkCreateEvent;
VK_HIDE_SYMBOL PFN_vkCreateFence vkCreateFence;
VK_HIDE_SYMBOL PFN_vkCreateFramebuffer vkCreateFramebuffer;
VK_HIDE_SYMBOL PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
VK_HIDE_SYMBOL PFN_vkCreateImage vkCreateImage;
VK_HIDE_SYMBOL PFN_vkCreateImageView vkCreateImageView;
VK_HIDE_SYMBOL PFN_vkCreatePipelineCache vkCreatePipelineCache;
VK_HIDE_SYMBOL PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
VK_HIDE_SYMBOL PFN_vkCreateQueryPool vkCreateQueryPool;
VK_HIDE_SYMBOL PFN_vkCreateRenderPass vkCreateRenderPass;
VK_HIDE_SYMBOL PFN_vkCreateSampler vkCreateSampler;
VK_HIDE_SYMBOL PFN_vkCreateSemaphore vkCreateSemaphore;
VK_HIDE_SYMBOL PFN_vkCreateShaderModule vkCreateShaderModule;
VK_HIDE_SYMBOL PFN_vkDestroyBuffer vkDestroyBuffer;
VK_HIDE_SYMBOL PFN_vkDestroyBufferView vkDestroyBufferView;
VK_HIDE_SYMBOL PFN_vkDestroyCommandPool vkDestroyCommandPool;
VK_HIDE_SYMBOL PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
VK_HIDE_SYMBOL PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
VK_HIDE_SYMBOL PFN_vkDestroyDevice vkDestroyDevice;
VK_HIDE_SYMBOL PFN_vkDestroyEvent vkDestroyEvent;
VK_HIDE_SYMBOL PFN_vkDestroyFence vkDestroyFence;
VK_HIDE_SYMBOL PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
VK_HIDE_SYMBOL PFN_vkDestroyImage vkDestroyImage;
VK_HIDE_SYMBOL PFN_vkDestroyImageView vkDestroyImageView;
VK_HIDE_SYMBOL PFN_vkDestroyPipeline vkDestroyPipeline;
VK_HIDE_SYMBOL PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
VK_HIDE_SYMBOL PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
VK_HIDE_SYMBOL PFN_vkDestroyQueryPool vkDestroyQueryPool;
VK_HIDE_SYMBOL PFN_vkDestroyRenderPass vkDestroyRenderPass;
VK_HIDE_SYMBOL PFN_vkDestroySampler vkDestroySampler;
VK_HIDE_SYMBOL PFN_vkDestroySemaphore vkDestroySemaphore;
VK_HIDE_SYMBOL PFN_vkDestroyShaderModule vkDestroyShaderModule;
VK_HIDE_SYMBOL PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
VK_HIDE_SYMBOL PFN_vkEndCommandBuffer vkEndCommandBuffer;
VK_HIDE_SYMBOL PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
VK_HIDE_SYMBOL PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
VK_HIDE_SYMBOL PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
VK_HIDE_SYMBOL PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
VK_HIDE_SYMBOL PFN_vkFreeMemory vkFreeMemory;
VK_HIDE_SYMBOL PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
VK_HIDE_SYMBOL PFN_vkGetDeviceMemoryCommitment vkGetDeviceMemoryCommitment;
VK_HIDE_SYMBOL PFN_vkGetDeviceQueue vkGetDeviceQueue;
VK_HIDE_SYMBOL PFN_vkGetEventStatus vkGetEventStatus;
VK_HIDE_SYMBOL PFN_vkGetFenceStatus vkGetFenceStatus;
VK_HIDE_SYMBOL PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
VK_HIDE_SYMBOL PFN_vkGetImageSparseMemoryRequirements vkGetImageSparseMemoryRequirements;
VK_HIDE_SYMBOL PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;
VK_HIDE_SYMBOL PFN_vkGetPipelineCacheData vkGetPipelineCacheData;
VK_HIDE_SYMBOL PFN_vkGetQueryPoolResults vkGetQueryPoolResults;
VK_HIDE_SYMBOL PFN_vkGetRenderAreaGranularity vkGetRenderAreaGranularity;
VK_HIDE_SYMBOL PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
VK_HIDE_SYMBOL PFN_vkMapMemory vkMapMemory;
VK_HIDE_SYMBOL PFN_vkMergePipelineCaches vkMergePipelineCaches;
VK_HIDE_SYMBOL PFN_vkQueueBindSparse vkQueueBindSparse;
VK_HIDE_SYMBOL PFN_vkQueueSubmit vkQueueSubmit;
VK_HIDE_SYMBOL PFN_vkQueueWaitIdle vkQueueWaitIdle;
VK_HIDE_SYMBOL PFN_vkResetCommandBuffer vkResetCommandBuffer;
VK_HIDE_SYMBOL PFN_vkResetCommandPool vkResetCommandPool;
VK_HIDE_SYMBOL PFN_vkResetDescriptorPool vkResetDescriptorPool;
VK_HIDE_SYMBOL PFN_vkResetEvent vkResetEvent;
VK_HIDE_SYMBOL PFN_vkResetFences vkResetFences;
VK_HIDE_SYMBOL PFN_vkSetEvent vkSetEvent;
VK_HIDE_SYMBOL PFN_vkUnmapMemory vkUnmapMemory;
VK_HIDE_SYMBOL PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
VK_HIDE_SYMBOL PFN_vkWaitForFences vkWaitForFences;
VK_HIDE_SYMBOL PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
VK_HIDE_SYMBOL PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
VK_HIDE_SYMBOL PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
VK_HIDE_SYMBOL PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
VK_HIDE_SYMBOL PFN_vkQueuePresentKHR vkQueuePresentKHR;
VK_HIDE_SYMBOL PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR;
VK_HIDE_SYMBOL PFN_vkDestroyDebugUtilsMessengerEXT    vkDestroyDebugUtilsMessengerEXT;



bool vkl_init_loader(void) {
  ngfi_module_handle vkdll = LoadLibraryA(VK_LOADER_LIB);

  if (!vkdll) { return false; }

  vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(vkdll, "vkGetInstanceProcAddr");
  vkCreateInstance =
      (PFN_vkCreateInstance)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
  vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)
      vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties");

  vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)
      vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
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
  vkCmdBeginDebugUtilsLabelEXT =
      (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(inst, "vkCmdBeginDebugUtilsLabelEXT");
  vkCmdEndDebugUtilsLabelEXT =
      (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(inst, "vkCmdEndDebugUtilsLabelEXT");
}

void vkl_init_device(VkDevice dev, bool sync2_supported) {
  vkAllocateCommandBuffers =
      (PFN_vkAllocateCommandBuffers)vkGetDeviceProcAddr(dev, "vkAllocateCommandBuffers");
  vkAllocateDescriptorSets =
      (PFN_vkAllocateDescriptorSets)vkGetDeviceProcAddr(dev, "vkAllocateDescriptorSets");
  vkAllocateMemory     = (PFN_vkAllocateMemory)vkGetDeviceProcAddr(dev, "vkAllocateMemory");
  vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vkGetDeviceProcAddr(dev, "vkBeginCommandBuffer");
  vkBindBufferMemory   = (PFN_vkBindBufferMemory)vkGetDeviceProcAddr(dev, "vkBindBufferMemory");
  vkBindImageMemory    = (PFN_vkBindImageMemory)vkGetDeviceProcAddr(dev, "vkBindImageMemory");
  vkCmdBeginQuery      = (PFN_vkCmdBeginQuery)vkGetDeviceProcAddr(dev, "vkCmdBeginQuery");
  vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)vkGetDeviceProcAddr(dev, "vkCmdBeginRenderPass");
  vkCmdBindDescriptorSets =
      (PFN_vkCmdBindDescriptorSets)vkGetDeviceProcAddr(dev, "vkCmdBindDescriptorSets");
  vkCmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer)vkGetDeviceProcAddr(dev, "vkCmdBindIndexBuffer");
  vkCmdBindPipeline    = (PFN_vkCmdBindPipeline)vkGetDeviceProcAddr(dev, "vkCmdBindPipeline");
  vkCmdBindVertexBuffers =
      (PFN_vkCmdBindVertexBuffers)vkGetDeviceProcAddr(dev, "vkCmdBindVertexBuffers");
  vkCmdBlitImage = (PFN_vkCmdBlitImage)vkGetDeviceProcAddr(dev, "vkCmdBlitImage");
  vkCmdClearAttachments =
      (PFN_vkCmdClearAttachments)vkGetDeviceProcAddr(dev, "vkCmdClearAttachments");
  vkCmdClearColorImage = (PFN_vkCmdClearColorImage)vkGetDeviceProcAddr(dev, "vkCmdClearColorImage");
  vkCmdClearDepthStencilImage =
      (PFN_vkCmdClearDepthStencilImage)vkGetDeviceProcAddr(dev, "vkCmdClearDepthStencilImage");
  vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer)vkGetDeviceProcAddr(dev, "vkCmdCopyBuffer");
  vkCmdCopyBufferToImage =
      (PFN_vkCmdCopyBufferToImage)vkGetDeviceProcAddr(dev, "vkCmdCopyBufferToImage");
  vkCmdCopyImage = (PFN_vkCmdCopyImage)vkGetDeviceProcAddr(dev, "vkCmdCopyImage");
  vkCmdCopyImageToBuffer =
      (PFN_vkCmdCopyImageToBuffer)vkGetDeviceProcAddr(dev, "vkCmdCopyImageToBuffer");
  vkCmdCopyQueryPoolResults =
      (PFN_vkCmdCopyQueryPoolResults)vkGetDeviceProcAddr(dev, "vkCmdCopyQueryPoolResults");
  vkCmdDispatch = (PFN_vkCmdDispatch)vkGetDeviceProcAddr(dev, "vkCmdDispatch");
  vkCmdDispatchIndirect =
      (PFN_vkCmdDispatchIndirect)vkGetDeviceProcAddr(dev, "vkCmdDispatchIndirect");
  vkCmdDraw        = (PFN_vkCmdDraw)vkGetDeviceProcAddr(dev, "vkCmdDraw");
  vkCmdDrawIndexed = (PFN_vkCmdDrawIndexed)vkGetDeviceProcAddr(dev, "vkCmdDrawIndexed");
  vkCmdDrawIndexedIndirect =
      (PFN_vkCmdDrawIndexedIndirect)vkGetDeviceProcAddr(dev, "vkCmdDrawIndexedIndirect");
  vkCmdDrawIndirect    = (PFN_vkCmdDrawIndirect)vkGetDeviceProcAddr(dev, "vkCmdDrawIndirect");
  vkCmdEndQuery        = (PFN_vkCmdEndQuery)vkGetDeviceProcAddr(dev, "vkCmdEndQuery");
  vkCmdEndRenderPass   = (PFN_vkCmdEndRenderPass)vkGetDeviceProcAddr(dev, "vkCmdEndRenderPass");
  vkCmdExecuteCommands = (PFN_vkCmdExecuteCommands)vkGetDeviceProcAddr(dev, "vkCmdExecuteCommands");
  vkCmdFillBuffer      = (PFN_vkCmdFillBuffer)vkGetDeviceProcAddr(dev, "vkCmdFillBuffer");
  vkCmdNextSubpass     = (PFN_vkCmdNextSubpass)vkGetDeviceProcAddr(dev, "vkCmdNextSubpass");
  vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)vkGetDeviceProcAddr(dev, "vkCmdPipelineBarrier");
  vkCmdPushConstants   = (PFN_vkCmdPushConstants)vkGetDeviceProcAddr(dev, "vkCmdPushConstants");
  vkCmdResetEvent      = (PFN_vkCmdResetEvent)vkGetDeviceProcAddr(dev, "vkCmdResetEvent");
  vkCmdResetQueryPool  = (PFN_vkCmdResetQueryPool)vkGetDeviceProcAddr(dev, "vkCmdResetQueryPool");
  vkCmdResolveImage    = (PFN_vkCmdResolveImage)vkGetDeviceProcAddr(dev, "vkCmdResolveImage");
  vkCmdSetBlendConstants =
      (PFN_vkCmdSetBlendConstants)vkGetDeviceProcAddr(dev, "vkCmdSetBlendConstants");
  vkCmdSetDepthBias   = (PFN_vkCmdSetDepthBias)vkGetDeviceProcAddr(dev, "vkCmdSetDepthBias");
  vkCmdSetDepthBounds = (PFN_vkCmdSetDepthBounds)vkGetDeviceProcAddr(dev, "vkCmdSetDepthBounds");
  vkCmdSetEvent       = (PFN_vkCmdSetEvent)vkGetDeviceProcAddr(dev, "vkCmdSetEvent");
  vkCmdSetLineWidth   = (PFN_vkCmdSetLineWidth)vkGetDeviceProcAddr(dev, "vkCmdSetLineWidth");
  vkCmdSetScissor     = (PFN_vkCmdSetScissor)vkGetDeviceProcAddr(dev, "vkCmdSetScissor");
  vkCmdSetStencilCompareMask =
      (PFN_vkCmdSetStencilCompareMask)vkGetDeviceProcAddr(dev, "vkCmdSetStencilCompareMask");
  vkCmdSetStencilReference =
      (PFN_vkCmdSetStencilReference)vkGetDeviceProcAddr(dev, "vkCmdSetStencilReference");
  vkCmdSetStencilWriteMask =
      (PFN_vkCmdSetStencilWriteMask)vkGetDeviceProcAddr(dev, "vkCmdSetStencilWriteMask");
  vkCmdSetViewport    = (PFN_vkCmdSetViewport)vkGetDeviceProcAddr(dev, "vkCmdSetViewport");
  vkCmdUpdateBuffer   = (PFN_vkCmdUpdateBuffer)vkGetDeviceProcAddr(dev, "vkCmdUpdateBuffer");
  vkCmdWaitEvents     = (PFN_vkCmdWaitEvents)vkGetDeviceProcAddr(dev, "vkCmdWaitEvents");
  vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)vkGetDeviceProcAddr(dev, "vkCmdWriteTimestamp");
  vkCreateBuffer      = (PFN_vkCreateBuffer)vkGetDeviceProcAddr(dev, "vkCreateBuffer");
  vkCreateBufferView  = (PFN_vkCreateBufferView)vkGetDeviceProcAddr(dev, "vkCreateBufferView");
  vkCreateCommandPool = (PFN_vkCreateCommandPool)vkGetDeviceProcAddr(dev, "vkCreateCommandPool");
  vkCreateComputePipelines =
      (PFN_vkCreateComputePipelines)vkGetDeviceProcAddr(dev, "vkCreateComputePipelines");
  vkCreateDescriptorPool =
      (PFN_vkCreateDescriptorPool)vkGetDeviceProcAddr(dev, "vkCreateDescriptorPool");
  vkCreateDescriptorSetLayout =
      (PFN_vkCreateDescriptorSetLayout)vkGetDeviceProcAddr(dev, "vkCreateDescriptorSetLayout");
  vkCreateEvent       = (PFN_vkCreateEvent)vkGetDeviceProcAddr(dev, "vkCreateEvent");
  vkCreateFence       = (PFN_vkCreateFence)vkGetDeviceProcAddr(dev, "vkCreateFence");
  vkCreateFramebuffer = (PFN_vkCreateFramebuffer)vkGetDeviceProcAddr(dev, "vkCreateFramebuffer");
  vkCreateGraphicsPipelines =
      (PFN_vkCreateGraphicsPipelines)vkGetDeviceProcAddr(dev, "vkCreateGraphicsPipelines");
  vkCreateImage     = (PFN_vkCreateImage)vkGetDeviceProcAddr(dev, "vkCreateImage");
  vkCreateImageView = (PFN_vkCreateImageView)vkGetDeviceProcAddr(dev, "vkCreateImageView");
  vkCreatePipelineCache =
      (PFN_vkCreatePipelineCache)vkGetDeviceProcAddr(dev, "vkCreatePipelineCache");
  vkCreatePipelineLayout =
      (PFN_vkCreatePipelineLayout)vkGetDeviceProcAddr(dev, "vkCreatePipelineLayout");
  vkCreateQueryPool    = (PFN_vkCreateQueryPool)vkGetDeviceProcAddr(dev, "vkCreateQueryPool");
  vkCreateRenderPass   = (PFN_vkCreateRenderPass)vkGetDeviceProcAddr(dev, "vkCreateRenderPass");
  vkCreateSampler      = (PFN_vkCreateSampler)vkGetDeviceProcAddr(dev, "vkCreateSampler");
  vkCreateSemaphore    = (PFN_vkCreateSemaphore)vkGetDeviceProcAddr(dev, "vkCreateSemaphore");
  vkCreateShaderModule = (PFN_vkCreateShaderModule)vkGetDeviceProcAddr(dev, "vkCreateShaderModule");
  vkDestroyBuffer      = (PFN_vkDestroyBuffer)vkGetDeviceProcAddr(dev, "vkDestroyBuffer");
  vkDestroyBufferView  = (PFN_vkDestroyBufferView)vkGetDeviceProcAddr(dev, "vkDestroyBufferView");
  vkDestroyCommandPool = (PFN_vkDestroyCommandPool)vkGetDeviceProcAddr(dev, "vkDestroyCommandPool");
  vkDestroyDescriptorPool =
      (PFN_vkDestroyDescriptorPool)vkGetDeviceProcAddr(dev, "vkDestroyDescriptorPool");
  vkDestroyDescriptorSetLayout =
      (PFN_vkDestroyDescriptorSetLayout)vkGetDeviceProcAddr(dev, "vkDestroyDescriptorSetLayout");
  vkDestroyDevice      = (PFN_vkDestroyDevice)vkGetDeviceProcAddr(dev, "vkDestroyDevice");
  vkDestroyEvent       = (PFN_vkDestroyEvent)vkGetDeviceProcAddr(dev, "vkDestroyEvent");
  vkDestroyFence       = (PFN_vkDestroyFence)vkGetDeviceProcAddr(dev, "vkDestroyFence");
  vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)vkGetDeviceProcAddr(dev, "vkDestroyFramebuffer");
  vkDestroyImage       = (PFN_vkDestroyImage)vkGetDeviceProcAddr(dev, "vkDestroyImage");
  vkDestroyImageView   = (PFN_vkDestroyImageView)vkGetDeviceProcAddr(dev, "vkDestroyImageView");
  vkDestroyPipeline    = (PFN_vkDestroyPipeline)vkGetDeviceProcAddr(dev, "vkDestroyPipeline");
  vkDestroyPipelineCache =
      (PFN_vkDestroyPipelineCache)vkGetDeviceProcAddr(dev, "vkDestroyPipelineCache");
  vkDestroyPipelineLayout =
      (PFN_vkDestroyPipelineLayout)vkGetDeviceProcAddr(dev, "vkDestroyPipelineLayout");
  vkDestroyQueryPool  = (PFN_vkDestroyQueryPool)vkGetDeviceProcAddr(dev, "vkDestroyQueryPool");
  vkDestroyRenderPass = (PFN_vkDestroyRenderPass)vkGetDeviceProcAddr(dev, "vkDestroyRenderPass");
  vkDestroySampler    = (PFN_vkDestroySampler)vkGetDeviceProcAddr(dev, "vkDestroySampler");
  vkDestroySemaphore  = (PFN_vkDestroySemaphore)vkGetDeviceProcAddr(dev, "vkDestroySemaphore");
  vkDestroyShaderModule =
      (PFN_vkDestroyShaderModule)vkGetDeviceProcAddr(dev, "vkDestroyShaderModule");
  vkDeviceWaitIdle   = (PFN_vkDeviceWaitIdle)vkGetDeviceProcAddr(dev, "vkDeviceWaitIdle");
  vkEndCommandBuffer = (PFN_vkEndCommandBuffer)vkGetDeviceProcAddr(dev, "vkEndCommandBuffer");
  vkFlushMappedMemoryRanges =
      (PFN_vkFlushMappedMemoryRanges)vkGetDeviceProcAddr(dev, "vkFlushMappedMemoryRanges");
  vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)vkGetDeviceProcAddr(dev, "vkFreeCommandBuffers");
  vkFreeDescriptorSets = (PFN_vkFreeDescriptorSets)vkGetDeviceProcAddr(dev, "vkFreeDescriptorSets");
  vkFreeMemory         = (PFN_vkFreeMemory)vkGetDeviceProcAddr(dev, "vkFreeMemory");
  vkGetBufferMemoryRequirements =
      (PFN_vkGetBufferMemoryRequirements)vkGetDeviceProcAddr(dev, "vkGetBufferMemoryRequirements");
  vkGetDeviceMemoryCommitment =
      (PFN_vkGetDeviceMemoryCommitment)vkGetDeviceProcAddr(dev, "vkGetDeviceMemoryCommitment");
  vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetDeviceProcAddr(dev, "vkGetDeviceQueue");
  vkGetEventStatus = (PFN_vkGetEventStatus)vkGetDeviceProcAddr(dev, "vkGetEventStatus");
  vkGetFenceStatus = (PFN_vkGetFenceStatus)vkGetDeviceProcAddr(dev, "vkGetFenceStatus");
  vkGetImageMemoryRequirements =
      (PFN_vkGetImageMemoryRequirements)vkGetDeviceProcAddr(dev, "vkGetImageMemoryRequirements");
  vkGetImageSparseMemoryRequirements = (PFN_vkGetImageSparseMemoryRequirements)vkGetDeviceProcAddr(
      dev,
      "vkGetImageSparseMemoryRequirements");
  vkGetImageSubresourceLayout =
      (PFN_vkGetImageSubresourceLayout)vkGetDeviceProcAddr(dev, "vkGetImageSubresourceLayout");
  vkGetPipelineCacheData =
      (PFN_vkGetPipelineCacheData)vkGetDeviceProcAddr(dev, "vkGetPipelineCacheData");
  vkGetQueryPoolResults =
      (PFN_vkGetQueryPoolResults)vkGetDeviceProcAddr(dev, "vkGetQueryPoolResults");
  vkGetRenderAreaGranularity =
      (PFN_vkGetRenderAreaGranularity)vkGetDeviceProcAddr(dev, "vkGetRenderAreaGranularity");
  vkInvalidateMappedMemoryRanges = (PFN_vkInvalidateMappedMemoryRanges)vkGetDeviceProcAddr(
      dev,
      "vkInvalidateMappedMemoryRanges");
  vkMapMemory = (PFN_vkMapMemory)vkGetDeviceProcAddr(dev, "vkMapMemory");
  vkMergePipelineCaches =
      (PFN_vkMergePipelineCaches)vkGetDeviceProcAddr(dev, "vkMergePipelineCaches");
  vkQueueBindSparse    = (PFN_vkQueueBindSparse)vkGetDeviceProcAddr(dev, "vkQueueBindSparse");
  vkQueueSubmit        = (PFN_vkQueueSubmit)vkGetDeviceProcAddr(dev, "vkQueueSubmit");
  vkQueueWaitIdle      = (PFN_vkQueueWaitIdle)vkGetDeviceProcAddr(dev, "vkQueueWaitIdle");
  vkResetCommandBuffer = (PFN_vkResetCommandBuffer)vkGetDeviceProcAddr(dev, "vkResetCommandBuffer");
  vkResetCommandPool   = (PFN_vkResetCommandPool)vkGetDeviceProcAddr(dev, "vkResetCommandPool");
  vkResetDescriptorPool =
      (PFN_vkResetDescriptorPool)vkGetDeviceProcAddr(dev, "vkResetDescriptorPool");
  vkResetEvent  = (PFN_vkResetEvent)vkGetDeviceProcAddr(dev, "vkResetEvent");
  vkResetFences = (PFN_vkResetFences)vkGetDeviceProcAddr(dev, "vkResetFences");
  vkSetEvent    = (PFN_vkSetEvent)vkGetDeviceProcAddr(dev, "vkSetEvent");
  vkUnmapMemory = (PFN_vkUnmapMemory)vkGetDeviceProcAddr(dev, "vkUnmapMemory");
  vkUpdateDescriptorSets =
      (PFN_vkUpdateDescriptorSets)vkGetDeviceProcAddr(dev, "vkUpdateDescriptorSets");
  vkWaitForFences      = (PFN_vkWaitForFences)vkGetDeviceProcAddr(dev, "vkWaitForFences");
  vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(dev, "vkCreateSwapchainKHR");
  vkDestroySwapchainKHR =
      (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(dev, "vkDestroySwapchainKHR");
  vkGetSwapchainImagesKHR =
      (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(dev, "vkGetSwapchainImagesKHR");
  vkAcquireNextImageKHR =
      (PFN_vkAcquireNextImageKHR)vkGetDeviceProcAddr(dev, "vkAcquireNextImageKHR");
  vkQueuePresentKHR = (PFN_vkQueuePresentKHR)vkGetDeviceProcAddr(dev, "vkQueuePresentKHR");
  if (sync2_supported) {
    vkCmdPipelineBarrier2 = (PFN_vkCmdPipelineBarrier2)vkGetDeviceProcAddr(dev, "vkCmdPipelineBarrier2KHR");
  }
}
