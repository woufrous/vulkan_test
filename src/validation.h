#pragma once

#include <iostream>

#include <vulkan/vulkan.h>

inline VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
    void*                                            pUserData
) {
    (void)messageTypes;
    (void)pUserData;

    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
            std::cerr << "[E] ";
        } break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
            std::cerr << "[W] ";
        } break;
        default: {
            std::cerr << "[D] ";
        } break;
    }
    std::cerr << pCallbackData->pMessage << std::endl;
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
