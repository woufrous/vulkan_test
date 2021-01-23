#pragma once

#include <iostream>

#include <vulkan/vulkan.h>

inline VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
    void*                                            pUserData) {

    std::cerr << "Validation msg: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}
