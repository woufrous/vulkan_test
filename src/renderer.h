#pragma once

#include <exception>
#include <memory>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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
        win_(win), inst_(), dev_() {}

        ~VulkanRenderer() {
            vkDestroyInstance(inst_, nullptr);
        }

        void init() {
            create_instance();
            create_device();
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
            inst_info.ppEnabledExtensionNames = glfwGetRequiredInstanceExtensions(&(inst_info.enabledExtensionCount));

            auto res = vkCreateInstance(&inst_info, nullptr, &inst_);
            if (res != VK_SUCCESS) {
                throw VulkanError("Instance creation failed", res);
            }
        }

        void create_device() {
            uint32_t dev_cnt = 0;
            vkEnumeratePhysicalDevices(inst_, &dev_cnt, nullptr);

            auto devs = std::vector<VkPhysicalDevice>(5);
            vkEnumeratePhysicalDevices(inst_, &dev_cnt, devs.data());

            dev_.physical = devs[0];
        }

        GLFWwindow* win_;
        VkInstance inst_;
        struct {
            VkPhysicalDevice physical;
            VkDevice logical;
        } dev_;
};
