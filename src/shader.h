#pragma once

#include <vulkan/vulkan.h>

#include "utils.h"

VkShaderModule create_shader_module(VkDevice dev, const uint8_t* shader_code, size_t size) {
    auto shader_create_info = VkShaderModuleCreateInfo{};
    shader_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_create_info.pCode = reinterpret_cast<const uint32_t*>(shader_code);
    shader_create_info.codeSize = size;

    VkShaderModule ret;
    auto err = vkCreateShaderModule(dev, &shader_create_info, nullptr, &ret);
    if (err != VK_SUCCESS) {
        throw VulkanError("Error creating ShaderModule", err);
    }

    return ret;
}
