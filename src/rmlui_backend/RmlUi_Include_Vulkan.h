#pragma once

// RmlUi's sample Vulkan backend ships a private GLAD loader and VMA copy. NodeSpireTD instead
// records UI into VulkanContext, so both renderers must use the engine's Volk dispatch and the
// same VMA version and allocator ABI.
#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>