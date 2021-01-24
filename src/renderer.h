#pragma once

#include <exception>
#include <memory>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "validation.h"

class VulkanError : public std::runtime_error {
    public:
        VulkanError(const char* what, VkResult ec) :
        std::runtime_error(what), ec_(ec) {}

        VkResult get_error() const noexcept {
            return ec_;
        }
    private:
        VkResult ec_;
};

class VulkanRenderer {
    public:
        VulkanRenderer(GLFWwindow* win) noexcept :
        win_(win), inst_(), dbg_msngr_(nullptr), dev_() {}

        ~VulkanRenderer() {
            vkDestroyDevice(dev_.logical, nullptr);
#ifndef DEBUG
            if (dbg_msngr_ != nullptr) {
                auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(inst_, "vkDestroyDebugUtilsMessengerEXT")
                );
                func(inst_, dbg_msngr_, nullptr);
            }
#endif
            vkDestroyInstance(inst_, nullptr);
        }

        void init() {
            create_instance();
#ifndef NDEBUG
            setup_dbg_msngr();
#endif
            create_device();
            create_logical_device();
        }

    private:
        void create_instance() {
            VkApplicationInfo app_info{};
            app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app_info.pApplicationName = "Vulkan Test";
            app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
            app_info.pEngineName = "Vulkan Test Engine";
            app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
            app_info.apiVersion = VK_API_VERSION_1_1;

            VkInstanceCreateInfo inst_info{};
            inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

            inst_info.pApplicationInfo = &app_info;
            // set required extension
            auto exts = required_extensions();
            inst_info.enabledExtensionCount = static_cast<uint32_t>(exts.size());
            inst_info.ppEnabledExtensionNames = exts.data();
#ifndef NDEBUG
            // set validation layers
            auto layers = this->required_layers();
            inst_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
            inst_info.ppEnabledLayerNames = layers.data();

            auto inst_msngr = newDebugUtilsMessengerCreateInfoEXT(debug_callback, (
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                ), (
                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                )
            );
            inst_info.pNext = &inst_msngr;
#endif

            auto res = vkCreateInstance(&inst_info, nullptr, &inst_);
            if (res != VK_SUCCESS) {
                throw VulkanError("Instance creation failed", res);
            }
        }

        void create_device() {
            uint32_t dev_cnt = 0;
            vkEnumeratePhysicalDevices(inst_, &dev_cnt, nullptr);
            if (dev_cnt == 0) {
                throw std::runtime_error("No physical device found");
            }

            auto devs = std::vector<VkPhysicalDevice>(dev_cnt);
            vkEnumeratePhysicalDevices(inst_, &dev_cnt, devs.data());

            dev_.physical = devs[0];
        }

        void create_logical_device() {
            uint32_t qfam_cnt;
            vkGetPhysicalDeviceQueueFamilyProperties(dev_.physical, &qfam_cnt, nullptr);
            std::vector<VkQueueFamilyProperties> qfams(qfam_cnt);
            vkGetPhysicalDeviceQueueFamilyProperties(dev_.physical, &qfam_cnt, qfams.data());

            uint32_t q_idx = 0;
            for (auto& qfam : qfams) {
                if ((qfam.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
                    (qfam.queueCount > 0)) {
                    break;
                }
            }

            VkDeviceQueueCreateInfo queue_info{};
            queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info.queueCount = 1;
            queue_info.queueFamilyIndex = q_idx;
            auto q_prio = 1.0f;
            queue_info.pQueuePriorities = &q_prio;

            VkDeviceCreateInfo ldev_info{};
            ldev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            ldev_info.queueCreateInfoCount = 1;
            ldev_info.pQueueCreateInfos = &queue_info;

            auto res = vkCreateDevice(dev_.physical, &ldev_info, nullptr, &dev_.logical);
            if (res != VK_SUCCESS) {
                throw VulkanError("Device creation failed.", res);
            }

            vkGetDeviceQueue(dev_.logical, q_idx, 0, &queues_.graphics);
        }

#ifndef NDEBUG
        void setup_dbg_msngr() {
            auto info = newDebugUtilsMessengerCreateInfoEXT(debug_callback, (
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                ), (
                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                )
            );
            auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(inst_, "vkCreateDebugUtilsMessengerEXT")
            );
            if (func == nullptr) {
                throw VulkanError("Error finding proc address", VK_ERROR_EXTENSION_NOT_PRESENT);
            }
            if (auto res = func(inst_, &info, nullptr, &dbg_msngr_); res != VK_SUCCESS) {
                throw VulkanError("Error creating debug messenger", res);
            }
        }

        std::vector<const char*> required_layers() const noexcept {
            auto ret = std::vector<const char*> {
                "VK_LAYER_KHRONOS_validation",
            };
            return ret;
        }
#endif

        std::vector<const char*> required_extensions() const noexcept {
            auto ret = std::vector<const char*> {
                VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            };

            uint32_t ext_cnt = 0;
            auto glfw_exts = glfwGetRequiredInstanceExtensions(&ext_cnt);
            for (; ext_cnt>0; --ext_cnt) {
                ret.push_back(glfw_exts[ext_cnt-1]);
            }

            return ret;
        }

        GLFWwindow* win_;
        VkInstance inst_;
#ifndef NDEBUG
        VkDebugUtilsMessengerEXT dbg_msngr_;
#endif
        struct {
            VkPhysicalDevice physical;
            VkDevice logical;
        } dev_;
        struct {
            VkQueue graphics;
        } queues_;
};
