// VulkanMemoryAllocator implementation translation unit.
// This file must be compiled exactly ONCE in the project.
// VMA_STATIC_VULKAN_FUNCTIONS 0 + VMA_DYNAMIC_VULKAN_FUNCTIONS 1 tells VMA
// to resolve Vulkan function pointers at runtime via volk (not linked directly).

#include <volk.h>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>
