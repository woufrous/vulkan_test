#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "validation.h"
#include "utils.h"

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
    private:
        struct Queue {
            uint32_t idx;
            VkQueue queue;
        };
    public:
        VulkanRenderer(GLFWwindow* win) noexcept :
        win_(win), inst_(), dbg_msngr_(), surf_(), dev_() {}

        ~VulkanRenderer() {
            for (auto img_view : sc_img_views_) {
                vkDestroyImageView(dev_.logical, img_view, nullptr);
            }
            vkDestroySwapchainKHR(dev_.logical, swap_chain_, nullptr);
            vkDestroyDevice(dev_.logical, nullptr);
#ifndef DEBUG
            if (dbg_msngr_ != nullptr) {
                auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(inst_, "vkDestroyDebugUtilsMessengerEXT")
                );
                func(inst_, dbg_msngr_, nullptr);
            }
#endif
            if (surf_ != nullptr) {
                vkDestroySurfaceKHR(inst_, surf_, nullptr);
            }
            vkDestroyInstance(inst_, nullptr);
        }

        void init() {
            create_instance();
#ifndef NDEBUG
            setup_dbg_msngr();
#endif
            create_surface();
            create_device();
            create_logical_device();
            create_swapchain();
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

        void create_surface() {
            auto res = glfwCreateWindowSurface(inst_, win_, nullptr, &surf_);
            if (res != VK_SUCCESS) {
                throw VulkanError("Surface creation failed", res);
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

            for (auto dev : devs) {
                // has present queue
                uint32_t queue_cnt;
                vkGetPhysicalDeviceQueueFamilyProperties(dev, &queue_cnt, nullptr);

                VkBool32 supported;
                for (uint32_t idx=0; idx<queue_cnt; ++idx) {
                    vkGetPhysicalDeviceSurfaceSupportKHR(dev, idx, surf_, &supported);
                    if (supported) {
                        dev_.physical = dev;
                        return;
                    }
                }
            }
        }

        void create_logical_device() {
            uint32_t qfam_cnt;
            vkGetPhysicalDeviceQueueFamilyProperties(dev_.physical, &qfam_cnt, nullptr);
            std::vector<VkQueueFamilyProperties> qfams(qfam_cnt);
            vkGetPhysicalDeviceQueueFamilyProperties(dev_.physical, &qfam_cnt, qfams.data());

            // find graphics queue
            auto gfx_queue_idx = filter_queues(qfams, [](const VkQueueFamilyProperties& qfam) -> bool {
                return (
                    (qfam.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
                    (qfam.queueCount > 0)
                );
            });
            if (!gfx_queue_idx) {
                throw std::runtime_error("No graphics queue found");
            }
            queues_.graphics.idx = *gfx_queue_idx;

            // find present queue
            auto present_queue_idx = std::optional<uint32_t>{std::nullopt};
            VkBool32 supported;
            for (uint32_t idx=0; idx<qfam_cnt; ++idx) {
                vkGetPhysicalDeviceSurfaceSupportKHR(dev_.physical, idx, surf_, &supported);
                if (supported) {
                    present_queue_idx = idx;
                    break;
                }
            }
            if (!present_queue_idx) {
                throw std::runtime_error("No present queue found");
            }
            queues_.present.idx = *present_queue_idx;

            auto idxs = std::set<uint32_t>{*present_queue_idx, *gfx_queue_idx};

            auto queue_create_infos = std::vector<VkDeviceQueueCreateInfo>{};
            auto q_prio = 1.0f;
            for (auto idx : idxs) {
                VkDeviceQueueCreateInfo queue_info{};
                queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queue_info.queueCount = 1;
                queue_info.queueFamilyIndex = idx;
                queue_info.pQueuePriorities = &q_prio;

                queue_create_infos.push_back(queue_info);
            };

            VkDeviceCreateInfo ldev_info{};
            ldev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            ldev_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
            ldev_info.pQueueCreateInfos = queue_create_infos.data();

            auto dev_exts = get_device_extensions();
            ldev_info.enabledExtensionCount = dev_exts.size();
            ldev_info.ppEnabledExtensionNames = dev_exts.data();

            auto res = vkCreateDevice(dev_.physical, &ldev_info, nullptr, &dev_.logical);
            if (res != VK_SUCCESS) {
                throw VulkanError("Device creation failed.", res);
            }

            vkGetDeviceQueue(dev_.logical, *gfx_queue_idx, 0, &queues_.graphics.queue);
            vkGetDeviceQueue(dev_.logical, *present_queue_idx, 0, &queues_.present.queue);
        }

        void create_swapchain() {
            auto sfc_caps = VkSurfaceCapabilitiesKHR{};
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev_.physical, surf_, &sfc_caps);

            uint32_t pres_mode_cnt;
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev_.physical, surf_, &pres_mode_cnt, nullptr);
            auto pres_modes = std::vector<VkPresentModeKHR>(pres_mode_cnt);
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev_.physical, surf_, &pres_mode_cnt, pres_modes.data());

            uint32_t sfc_fmt_cnt;
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev_.physical, surf_, &sfc_fmt_cnt, nullptr);
            auto sfc_fmts = std::vector<VkSurfaceFormatKHR>(sfc_fmt_cnt);
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev_.physical, surf_, &sfc_fmt_cnt, sfc_fmts.data());

            uint32_t img_cnt = std::min(sfc_caps.minImageCount+1, std::max(sfc_caps.maxImageCount, UINT32_MAX));

            auto sc_info = VkSwapchainCreateInfoKHR{};
            sc_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            sc_info.presentMode = pres_modes[0];
            sc_info.surface = surf_;
            sc_info.minImageCount = img_cnt;
            sc_info.imageFormat = sfc_fmts[0].format;
            sc_info.imageColorSpace = sfc_fmts[0].colorSpace;
            sc_info.imageExtent = get_image_extent(sfc_caps);
            sc_info.preTransform = sfc_caps.currentTransform;
            sc_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            sc_info.imageArrayLayers = 1;
            sc_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            sc_info.clipped = VK_TRUE;

            uint32_t queue_idxs[] = {
                queues_.graphics.idx,
                queues_.present.idx,
            };
            if (queues_.graphics.idx == queues_.present.idx) {
                sc_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            } else {
                sc_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                sc_info.queueFamilyIndexCount = static_cast<uint32_t>(sizeof(queue_idxs)/sizeof(queue_idxs[0]));
                sc_info.pQueueFamilyIndices = queue_idxs;
            }

            {
                auto res = vkCreateSwapchainKHR(dev_.logical, &sc_info, nullptr, &swap_chain_);
                if (res != VK_SUCCESS) {
                    throw VulkanError("Error creating swap chain", res);
                }
            }

            swapchain_settings_.format = sc_info.imageFormat;
            swapchain_settings_.extent = sc_info.imageExtent;

            vkGetSwapchainImagesKHR(dev_.logical, swap_chain_, &img_cnt, nullptr);
            sc_imgs_.resize(img_cnt);
            vkGetSwapchainImagesKHR(dev_.logical, swap_chain_, &img_cnt, sc_imgs_.data());

            sc_img_views_.resize(img_cnt);
            auto ivc_info = VkImageViewCreateInfo{};
            ivc_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ivc_info.format = swapchain_settings_.format;
            ivc_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivc_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ivc_info.subresourceRange.baseMipLevel = 0;
            ivc_info.subresourceRange.levelCount = 1;
            ivc_info.subresourceRange.baseArrayLayer = 0;
            ivc_info.subresourceRange.layerCount = 1;
            ivc_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivc_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivc_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivc_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            for (size_t i=0; i<img_cnt; ++i) {
                ivc_info.image = sc_imgs_[i];
                {
                    auto res = vkCreateImageView(dev_.logical, &ivc_info, nullptr, &sc_img_views_[i]);
                    if (res != VK_SUCCESS) {
                        throw VulkanError("Error creating ImageView", res);
                    }
                }
            }
        }

        VkExtent2D get_image_extent(const VkSurfaceCapabilitiesKHR& sfc_caps) const {
            if (sfc_caps.currentExtent.width != UINT32_MAX) {
                return sfc_caps.currentExtent;
            }

            int width;
            int height;
            glfwGetFramebufferSize(win_, &width, &height);

            auto ret = VkExtent2D{};
            ret.width = std::min(sfc_caps.maxImageExtent.width, static_cast<uint32_t>(width));
            ret.height = std::min(sfc_caps.maxImageExtent.height, static_cast<uint32_t>(height));

            return ret;
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

        std::vector<const char*> get_device_extensions() const noexcept {
            return std::vector<const char*>{
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            };
        }

        GLFWwindow* win_;
        VkInstance inst_;
#ifndef NDEBUG
        VkDebugUtilsMessengerEXT dbg_msngr_;
#endif
        VkSurfaceKHR surf_;
        struct {
            VkPhysicalDevice physical;
            VkDevice logical;
        } dev_;
        VkSwapchainKHR swap_chain_;
        struct {
            Queue graphics;
            Queue present;
        } queues_;

        struct {
            VkFormat format;
            VkExtent2D extent;
        } swapchain_settings_;
        std::vector<VkImage> sc_imgs_;
        std::vector<VkImageView> sc_img_views_;
};
