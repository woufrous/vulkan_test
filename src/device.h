#pragma once

#include <vulkan/vulkan.h>

struct VulkanDevice {
    VkPhysicalDevice physical;
    VkDevice logical;
};
