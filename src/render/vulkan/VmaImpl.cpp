// Vulkan Memory Allocator implementation unit. Function pointers come from
// volk (see VulkanDevice.cpp, vmaImportVulkanFunctionsFromVolk).
#include <volk.h>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>
