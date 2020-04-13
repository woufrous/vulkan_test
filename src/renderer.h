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
            vkDestroyDevice(dev_.logical, nullptr);
            vkDestroyInstance(inst_, nullptr);
        }

        void init() {
            create_instance();
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

        std::vector<const char*> required_extensions() const noexcept {
            auto ret = std::vector<const char*> {
                VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
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
        struct {
            VkPhysicalDevice physical;
            VkDevice logical;
        } dev_;
        struct {
            VkQueue graphics;
        } queues_;
};
