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

inline VkDebugUtilsMessengerCreateInfoEXT
newDebugUtilsMessengerCreateInfoEXT(
    PFN_vkDebugUtilsMessengerCallbackEXT callback,
    VkDebugUtilsMessageSeverityFlagsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type
) {

    auto info = VkDebugUtilsMessengerCreateInfoEXT{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = severity;
    info.messageType = type;
    info.pfnUserCallback = callback;
    info.pUserData = nullptr;

    return info;
}
