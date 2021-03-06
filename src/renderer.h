#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "buffer.h"
#include "descr.h"
#include "device.h"
#include "shader.h"
#include "texture.h"
#include "utils.h"
#include "validation.h"

class VulkanRenderer {
    private:
        struct Queue {
            uint32_t idx;
            VkQueue queue;
        };
    public:
        static const uint8_t MAX_FRAMES_IN_FLIGHT = 2;
        VulkanRenderer(GLFWwindow* win) noexcept :
        win_(win) {}

        void destroy() {
            vkDeviceWaitIdle(dev_.logical);
            for (size_t i=0; i<MAX_FRAMES_IN_FLIGHT; ++i) {
                vkDestroySemaphore(dev_.logical, render_finished_[i], nullptr);
                vkDestroySemaphore(dev_.logical, image_available_[i], nullptr);
                vkDestroyFence(dev_.logical, frame_done_[i], nullptr);
            }
            cleanup_swapchain();
            vkDestroyDescriptorSetLayout(dev_.logical, desc_set_layout_, nullptr);

            vkDestroyBuffer(dev_.logical, idx_buffer_, nullptr);
            vkFreeMemory(dev_.logical, idx_mem_, nullptr);
            vkDestroyBuffer(dev_.logical, vert_buffer_, nullptr);
            vkFreeMemory(dev_.logical, vert_mem_, nullptr);
            vkDestroyImageView(dev_.logical, tex_image_view_, nullptr);
            vkDestroyImage(dev_.logical, tex_image_, nullptr);
            vkFreeMemory(dev_.logical, tex_mem_, nullptr);

            vkDestroySampler(dev_.logical, tex_sampler_, nullptr);

            vkDestroyCommandPool(dev_.logical, command_pool_, nullptr);
            vkDestroyDevice(dev_.logical, nullptr);
#ifndef NDEBUG
            {
                auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(inst_, "vkDestroyDebugUtilsMessengerEXT")
                );
                func(inst_, dbg_msngr_, nullptr);
            }
#endif
            vkDestroySurfaceKHR(inst_, surf_, nullptr);
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
            create_render_pass();
            create_descriptor_set_layout();
            create_gfx_pipeline();
            create_framebuffers();
            create_command_pool();
            create_tex_image();
            tex_sampler_ = create_texture_sampler(dev_);
            create_vert_buffer();
            create_idx_buffer();
            create_uniform_buffers();
            create_desc_pool();
            create_desc_sets();
            create_command_buffers();
            create_semaphores();
        }

        void draw_frame() {
            vkWaitForFences(dev_.logical, 1, &frame_done_[curr_frame_], VK_TRUE, UINT64_MAX);

            uint32_t img_idx;
            {
                auto res = vkAcquireNextImageKHR(
                    dev_.logical, swap_chain_, UINT64_MAX,
                    image_available_[curr_frame_], VK_NULL_HANDLE, &img_idx
                );
                if (res == VK_ERROR_OUT_OF_DATE_KHR) {
                    recreate_swapchain();
                    return;
                } else if (res != VK_SUCCESS) {
                    throw VulkanError("Error aquiring Swapchain Image", res);
                }
            }

            if (frame_in_flight_[img_idx] != VK_NULL_HANDLE) {
                vkWaitForFences(dev_.logical, 1, &frame_in_flight_[img_idx], VK_TRUE, UINT64_MAX);
            }
            frame_in_flight_[img_idx] = frame_done_[curr_frame_];

            update_uniform_buffers(img_idx);

            auto submit_info = VkSubmitInfo{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            VkSemaphore wait_semas[] = {image_available_[curr_frame_]};
            VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            submit_info.waitSemaphoreCount = sizeof(wait_semas) / sizeof(wait_semas[0]);
            submit_info.pWaitSemaphores = wait_semas;
            submit_info.pWaitDstStageMask = wait_stages;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffers_[img_idx];
            VkSemaphore signal_semas[] = {render_finished_[curr_frame_]};
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = signal_semas;

            vkResetFences(dev_.logical, 1, &frame_done_[curr_frame_]);
            {
                auto res = vkQueueSubmit(queues_.graphics.queue, 1, &submit_info, frame_done_[curr_frame_]);
                if (res != VK_SUCCESS) {
                    throw VulkanError("Error submitting Queue", res);
                }
            }

            auto pres_info = VkPresentInfoKHR{};
            pres_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pres_info.waitSemaphoreCount = sizeof(signal_semas) / sizeof(signal_semas[0]);
            pres_info.pWaitSemaphores = signal_semas;
            VkSwapchainKHR swapchains[] = {swap_chain_};
            pres_info.swapchainCount = sizeof(swapchains) / sizeof(swapchains[0]);
            pres_info.pSwapchains = swapchains;
            pres_info.pImageIndices = &img_idx;
            pres_info.pResults = nullptr;

            {
                auto res = vkQueuePresentKHR(queues_.present.queue, &pres_info);
                if ((res == VK_ERROR_OUT_OF_DATE_KHR) || (res == VK_SUBOPTIMAL_KHR) || window_resized_) {
                    recreate_swapchain();
                } else if (res != VK_SUCCESS) {
                    throw VulkanError("Error presenting Queue", res);
                }
            }
            curr_frame_ = (curr_frame_+1) % MAX_FRAMES_IN_FLIGHT;
        }

        void update_uniform_buffers(uint32_t img_idx) {
            static auto t0 = std::chrono::high_resolution_clock::now();
            auto dt = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - t0);

            auto ubo = UniformBufferObject{};
            ubo.model = glm::rotate(
                glm::mat4(1.0f),
                dt.count() * glm::radians(90.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)
            );
            ubo.view = glm::lookAt(
                glm::vec3(2.0f, 2.0f, 2.0f),
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)
            );
            ubo.proj = glm::perspective(
                glm::radians(45.0f),
                swapchain_settings_.extent.width/(float)swapchain_settings_.extent.height,
                0.1f, 10.0f
            );

            void* data = nullptr;
            vkMapMemory(dev_.logical, uniform_mems_[img_idx], 0, sizeof(ubo), 0, &data);
            std::memcpy(data, reinterpret_cast<void*>(&ubo), sizeof(ubo));
            vkUnmapMemory(dev_.logical, uniform_mems_[img_idx]);
        }

        static void win_resize_handler(GLFWwindow* win, int width, int height) {
            auto renderer = reinterpret_cast<VulkanRenderer*>(glfwGetWindowUserPointer(win));
            renderer->window_resized_ = true;
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
                auto dev_features = VkPhysicalDeviceFeatures{};
                vkGetPhysicalDeviceFeatures(dev, &dev_features);

                VkBool32 supported;
                for (uint32_t idx=0; idx<queue_cnt; ++idx) {
                    vkGetPhysicalDeviceSurfaceSupportKHR(dev, idx, surf_, &supported);
                    if (supported && (dev_features.samplerAnisotropy == VK_TRUE)) {
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

            auto dev_features = VkPhysicalDeviceFeatures{};
            dev_features.samplerAnisotropy = VK_TRUE;

            VkDeviceCreateInfo ldev_info{};
            ldev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            ldev_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
            ldev_info.pQueueCreateInfos = queue_create_infos.data();
            ldev_info.pEnabledFeatures = &dev_features;

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

            for (size_t i=0; i<img_cnt; ++i) {
                sc_img_views_[i] = create_image_view(dev_.logical, sc_imgs_[i], swapchain_settings_.format);
            }
        }

        void cleanup_swapchain() {
            vkFreeCommandBuffers(dev_.logical, command_pool_, static_cast<uint32_t>(command_buffers_.size()), command_buffers_.data());
            for (auto fb : sc_framebuffers_) {
                vkDestroyFramebuffer(dev_.logical, fb, nullptr);
            }
            vkDestroyPipeline(dev_.logical, pipeline_, nullptr);
            vkDestroyPipelineLayout(dev_.logical, pl_layout_, nullptr);
            vkDestroyRenderPass(dev_.logical, render_pass_, nullptr);
            for (auto img_view : sc_img_views_) {
                vkDestroyImageView(dev_.logical, img_view, nullptr);
            }
            vkDestroySwapchainKHR(dev_.logical, swap_chain_, nullptr);

            for (size_t i=0; i<uniform_mems_.size(); ++i) {
                vkDestroyBuffer(dev_.logical, uniform_buffers_[i], nullptr);
                vkFreeMemory(dev_.logical, uniform_mems_[i], nullptr);
            }
            vkDestroyDescriptorPool(dev_.logical, desc_pool_, nullptr);
        }

        void recreate_swapchain() {
            vkDeviceWaitIdle(dev_.logical);

            cleanup_swapchain();

            create_swapchain();
            create_render_pass();
            create_gfx_pipeline();
            create_framebuffers();
            create_uniform_buffers();
            create_desc_pool();
            create_desc_sets();
            create_command_buffers();

            window_resized_ = false;
        }

        void create_gfx_pipeline() {
            auto frag_shdr_code = load_file("frag.spv");
            auto vert_shdr_code = load_file("vert.spv");

            auto frag_shdr = create_shader_module(dev_.logical, frag_shdr_code.data(), frag_shdr_code.size());
            auto vert_shdr = create_shader_module(dev_.logical, vert_shdr_code.data(), vert_shdr_code.size());

            auto pl_vert_info = VkPipelineShaderStageCreateInfo{};
            pl_vert_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pl_vert_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
            pl_vert_info.pName = "main";
            pl_vert_info.module = vert_shdr;

            auto pl_frag_info = VkPipelineShaderStageCreateInfo{};
            pl_frag_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pl_frag_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            pl_frag_info.pName = "main";
            pl_frag_info.module = frag_shdr;

            VkPipelineShaderStageCreateInfo shader_stages[] = { pl_vert_info, pl_frag_info };

            auto vert_binding_desc = Vertex::get_binding_desc();
            auto vert_attrib_desc = Vertex::get_attrib_desc();

            auto vert_input_info = VkPipelineVertexInputStateCreateInfo{};
            vert_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vert_input_info.vertexBindingDescriptionCount = 1;
            vert_input_info.pVertexBindingDescriptions = &vert_binding_desc;
            vert_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(vert_attrib_desc.size());
            vert_input_info.pVertexAttributeDescriptions = vert_attrib_desc.data();

            auto input_assembly_info = VkPipelineInputAssemblyStateCreateInfo{};
            input_assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            input_assembly_info.primitiveRestartEnable = VK_FALSE;

            auto viewport = VkViewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(swapchain_settings_.extent.width);
            viewport.height = static_cast<float>(swapchain_settings_.extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            auto scissor = VkRect2D{};
            scissor.offset = VkOffset2D{0, 0};
            scissor.extent = swapchain_settings_.extent;

            auto viewport_info = VkPipelineViewportStateCreateInfo{};
            viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_info.viewportCount = 1;
            viewport_info.pViewports = &viewport;
            viewport_info.scissorCount = 1;
            viewport_info.pScissors = &scissor;

            auto rasterizer_info = VkPipelineRasterizationStateCreateInfo{};
            rasterizer_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer_info.depthClampEnable = VK_FALSE;
            rasterizer_info.rasterizerDiscardEnable = VK_FALSE;
            rasterizer_info.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer_info.lineWidth = 1.0f;
            rasterizer_info.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizer_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer_info.depthBiasEnable = VK_FALSE;

            auto ms_info = VkPipelineMultisampleStateCreateInfo{};
            ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms_info.sampleShadingEnable = VK_FALSE;
            ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            auto blend_attachment_info = VkPipelineColorBlendAttachmentState{};
            blend_attachment_info.colorWriteMask = (
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT
            );
            blend_attachment_info.blendEnable = VK_FALSE;

            auto blend_global_info = VkPipelineColorBlendStateCreateInfo{};
            blend_global_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend_global_info.attachmentCount = 1;
            blend_global_info.pAttachments = &blend_attachment_info;
            blend_global_info.logicOpEnable = VK_FALSE;

            VkDynamicState dyn_states[] = {
                VkDynamicState::VK_DYNAMIC_STATE_VIEWPORT,
                VkDynamicState::VK_DYNAMIC_STATE_LINE_WIDTH,
            };
            auto dyn_state_info = VkPipelineDynamicStateCreateInfo{};
            dyn_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dyn_state_info.dynamicStateCount = sizeof(dyn_states) / sizeof(dyn_states[0]);
            dyn_state_info.pDynamicStates = dyn_states;

            auto pl_layout_info = VkPipelineLayoutCreateInfo{};
            pl_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pl_layout_info.setLayoutCount = 1;
            pl_layout_info.pSetLayouts = &desc_set_layout_;

            {
                auto res = vkCreatePipelineLayout(dev_.logical, &pl_layout_info, nullptr, &pl_layout_);
                if (res != VK_SUCCESS) {
                    throw VulkanError("Error creating PipelineLayout", res);
                }
            }

            auto pl_info = VkGraphicsPipelineCreateInfo{};
            pl_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pl_info.stageCount = 2;
            pl_info.pStages = shader_stages;
            pl_info.pVertexInputState = &vert_input_info;
            pl_info.pInputAssemblyState = &input_assembly_info;
            pl_info.pViewportState = &viewport_info;
            pl_info.pRasterizationState = &rasterizer_info;
            pl_info.pMultisampleState = &ms_info;
            pl_info.pDepthStencilState = nullptr;
            pl_info.pColorBlendState = &blend_global_info;
            pl_info.pDynamicState = nullptr;
            pl_info.layout = pl_layout_;
            pl_info.renderPass = render_pass_;
            pl_info.subpass = 0;
            pl_info.basePipelineHandle = VK_NULL_HANDLE;
            pl_info.basePipelineIndex = -1;

            {
                auto res = vkCreateGraphicsPipelines(dev_.logical, VK_NULL_HANDLE, 1, &pl_info, nullptr, &pipeline_);
                if (res != VK_SUCCESS) {
                    vkDestroyShaderModule(dev_.logical, frag_shdr, nullptr);
                    vkDestroyShaderModule(dev_.logical, vert_shdr, nullptr);
                    throw VulkanError("Error creating pipeline", res);
                }
            }

            vkDestroyShaderModule(dev_.logical, frag_shdr, nullptr);
            vkDestroyShaderModule(dev_.logical, vert_shdr, nullptr);
        }

        void create_framebuffers() {
            sc_framebuffers_.resize(sc_img_views_.size());

            for (size_t i=0; i<sc_img_views_.size(); ++i) {
                VkImageView attached[] = {
                    sc_img_views_[i],
                };

                auto fb_info = VkFramebufferCreateInfo{};
                fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fb_info.renderPass = render_pass_;
                fb_info.attachmentCount = sizeof(attached)/sizeof(attached[0]);
                fb_info.pAttachments = attached;
                fb_info.width = swapchain_settings_.extent.width;
                fb_info.height = swapchain_settings_.extent.height;
                fb_info.layers = 1;

                {
                    auto res = vkCreateFramebuffer(dev_.logical, &fb_info, nullptr, &sc_framebuffers_[i]);
                    if (res != VK_SUCCESS) {
                        throw VulkanError("Error creating Framebuffer", res);
                    }
                }
            }
        }

        void create_command_pool() {
            auto pool_info = VkCommandPoolCreateInfo{};
            pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_info.queueFamilyIndex = queues_.graphics.idx;
            pool_info.flags = 0;

            {
                auto res = vkCreateCommandPool(dev_.logical, &pool_info, nullptr, &command_pool_);
                if (res != VK_SUCCESS) {
                    throw VulkanError("Error creating CommandPool", res);
                }
            }
        }

        void create_tex_image() {
            auto tex = Texture("texture.jpg");

            auto buf_desc = BufferDesc{};
            buf_desc.size = tex.size();
            buf_desc.buf_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            buf_desc.mem_prop_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            auto staging_buf = VkBuffer{};
            auto staging_mem = VkDeviceMemory{};
            create_buffer(dev_, buf_desc, &staging_buf, &staging_mem);

            void* data = nullptr;
            vkMapMemory(dev_.logical, staging_mem, 0, buf_desc.size, 0, &data);
            std::memcpy(data, tex.data(), static_cast<size_t>(buf_desc.size));
            vkUnmapMemory(dev_.logical, staging_mem);

            auto img_desc = ImageDesc{};
            img_desc.width = tex.width();
            img_desc.height = tex.height();
            img_desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            img_desc.mem_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            create_image(dev_, img_desc, &tex_image_, &tex_mem_);

            {
                auto cmd_buf = OneTimeCommandBuffer(dev_.logical, command_pool_);
                auto cmd_executor = RAIICommandBufferExecutor(cmd_buf, queues_.graphics.queue);
                transition_image_layout(cmd_buf, tex_image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                copy_buffer_to_image(cmd_buf, staging_buf, tex_image_, tex.extent());
                transition_image_layout(cmd_buf, tex_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

            vkFreeMemory(dev_.logical, staging_mem, nullptr);
            vkDestroyBuffer(dev_.logical, staging_buf, nullptr);

            tex_image_view_ = create_image_view(dev_.logical, tex_image_, VK_FORMAT_R8G8B8A8_SRGB);
        }

        void create_vert_buffer() {
            auto buf_desc = BufferDesc{};
            buf_desc.size = vertices.size() * sizeof(vertices[0]);
            buf_desc.buf_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            buf_desc.mem_prop_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            auto staging_buf = VkBuffer{};
            auto staging_mem = VkDeviceMemory{};
            create_buffer(dev_, buf_desc, &staging_buf, &staging_mem);

            void* data = nullptr;
            vkMapMemory(dev_.logical, staging_mem, 0, buf_desc.size, 0, &data);
            std::memcpy(data, vertices.data(), static_cast<size_t>(buf_desc.size));
            vkUnmapMemory(dev_.logical, staging_mem);

            buf_desc.buf_usage_flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            buf_desc.mem_prop_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            create_buffer(dev_, buf_desc, &vert_buffer_, &vert_mem_);
            copy_buffer(dev_, queues_.graphics.queue, command_pool_, staging_buf, vert_buffer_, buf_desc.size);

            vkFreeMemory(dev_.logical, staging_mem, nullptr);
            vkDestroyBuffer(dev_.logical, staging_buf, nullptr);
        }

        void create_idx_buffer() {
            auto buf_desc = BufferDesc{};
            buf_desc.size = indices.size() * sizeof(indices[0]);
            buf_desc.buf_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            buf_desc.mem_prop_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            auto staging_buf = VkBuffer{};
            auto staging_mem = VkDeviceMemory{};
            create_buffer(dev_, buf_desc, &staging_buf, &staging_mem);

            void* data = nullptr;
            vkMapMemory(dev_.logical, staging_mem, 0, buf_desc.size, 0, &data);
            std::memcpy(data, indices.data(), static_cast<size_t>(buf_desc.size));
            vkUnmapMemory(dev_.logical, staging_mem);

            buf_desc.buf_usage_flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            buf_desc.mem_prop_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            create_buffer(dev_, buf_desc, &idx_buffer_, &idx_mem_);
            copy_buffer(dev_, queues_.graphics.queue, command_pool_, staging_buf, idx_buffer_, buf_desc.size);

            vkFreeMemory(dev_.logical, staging_mem, nullptr);
            vkDestroyBuffer(dev_.logical, staging_buf, nullptr);
        }

        void create_uniform_buffers() {
            auto buf_desc = BufferDesc{};
            buf_desc.size = sizeof(UniformBufferObject);
            buf_desc.buf_usage_flags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            buf_desc.mem_prop_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            uniform_buffers_.resize(sc_imgs_.size());
            uniform_mems_.resize(sc_imgs_.size());

            for (size_t i=0; i<sc_imgs_.size(); ++i) {
                create_buffer(dev_, buf_desc, &uniform_buffers_[i], &uniform_mems_[i]);
            }
        }

        void create_desc_pool() {
            auto pool_size = std::array<VkDescriptorPoolSize,2>{};
            pool_size[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            pool_size[0].descriptorCount = static_cast<uint32_t>(sc_imgs_.size());
            pool_size[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            pool_size[1].descriptorCount = static_cast<uint32_t>(sc_imgs_.size());

            auto desc_pool_info = VkDescriptorPoolCreateInfo{};
            desc_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            desc_pool_info.poolSizeCount = static_cast<uint32_t>(pool_size.size());
            desc_pool_info.pPoolSizes = pool_size.data();
            desc_pool_info.maxSets = static_cast<uint32_t>(sc_imgs_.size());

            {
                auto res = vkCreateDescriptorPool(dev_.logical, &desc_pool_info, nullptr, &desc_pool_);
                if (res != VK_SUCCESS) {
                    throw VulkanError("Error creating DescriptorPool", res);
                }
            }
        }

        void create_desc_sets() {
            auto layouts = std::vector<VkDescriptorSetLayout>(sc_imgs_.size(), desc_set_layout_);
            desc_sets_.resize(sc_imgs_.size());

            auto desc_set_info = VkDescriptorSetAllocateInfo{};
            desc_set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            desc_set_info.descriptorPool = desc_pool_;
            desc_set_info.descriptorSetCount = static_cast<uint32_t>(sc_imgs_.size());
            desc_set_info.pSetLayouts = layouts.data();
            {
                auto res = vkAllocateDescriptorSets(dev_.logical, &desc_set_info, desc_sets_.data());
                if (res != VK_SUCCESS) {
                    throw VulkanError("Error creating DescriptorSets", res);
                }
            }

            for (size_t i=0; i<sc_imgs_.size(); ++i) {
                auto buf_info = VkDescriptorBufferInfo{};
                buf_info.buffer = uniform_buffers_[i];
                buf_info.offset = 0;
                buf_info.range = sizeof(UniformBufferObject);

                auto img_info = VkDescriptorImageInfo{};
                img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                img_info.imageView = tex_image_view_;
                img_info.sampler = tex_sampler_;

                auto desc_write = std::array<VkWriteDescriptorSet,2>{};
                desc_write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                desc_write[0].dstSet = desc_sets_[i];
                desc_write[0].dstBinding = 0;
                desc_write[0].dstArrayElement = 0;
                desc_write[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                desc_write[0].descriptorCount = 1;
                desc_write[0].pBufferInfo = &buf_info;

                desc_write[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                desc_write[1].dstSet = desc_sets_[i];
                desc_write[1].dstBinding = 1;
                desc_write[1].dstArrayElement = 0;
                desc_write[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                desc_write[1].descriptorCount = 1;
                desc_write[1].pImageInfo = &img_info;

                vkUpdateDescriptorSets(dev_.logical, static_cast<uint32_t>(desc_write.size()), desc_write.data(), 0, nullptr);
            }
        }

        void create_command_buffers() {
            command_buffers_.resize(sc_framebuffers_.size());
            auto buffer_info = VkCommandBufferAllocateInfo{};
            buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            buffer_info.commandPool = command_pool_;
            buffer_info.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());

            {
                auto res = vkAllocateCommandBuffers(dev_.logical, &buffer_info, command_buffers_.data());
                if (res != VK_SUCCESS) {
                    throw VulkanError("Error creating CommandBuffers", res);
                }
            }

            auto begin_info = VkCommandBufferBeginInfo{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.pInheritanceInfo = nullptr;
            begin_info.flags = 0;
            for (size_t i=0; i<command_buffers_.size(); ++i) {
                {
                    auto res = vkBeginCommandBuffer(command_buffers_[i], &begin_info);
                    if (res != VK_SUCCESS)
                    {
                        throw VulkanError("Error begin CommandBuffer recording", res);
                    }
                }

                auto rp_begin_info = VkRenderPassBeginInfo{};
                rp_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_begin_info.framebuffer = sc_framebuffers_[i];
                rp_begin_info.renderPass = render_pass_;
                rp_begin_info.renderArea.offset = VkOffset2D{0, 0};
                rp_begin_info.renderArea.extent = swapchain_settings_.extent;
                auto clear_color = VkClearValue{0.0f, 0.0f, 0.0f, 1.0f};
                rp_begin_info.clearValueCount = 1;
                rp_begin_info.pClearValues = &clear_color;
                vkCmdBeginRenderPass(command_buffers_[i], &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindPipeline(command_buffers_[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
                VkBuffer buffers[] = {vert_buffer_};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(command_buffers_[i], 0, 1, buffers, offsets);
                vkCmdBindIndexBuffer(command_buffers_[i], idx_buffer_, 0, VK_INDEX_TYPE_UINT16);
                vkCmdBindDescriptorSets(command_buffers_[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pl_layout_, 0, 1, &desc_sets_[i], 0, nullptr);
                vkCmdDrawIndexed(command_buffers_[i], static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

                vkCmdEndRenderPass(command_buffers_[i]);
                {
                    auto res = vkEndCommandBuffer(command_buffers_[i]);
                    if (res != VK_SUCCESS)
                    {
                        throw VulkanError("Error begin CommandBuffer recording", res);
                    }
                }
            }
        }

        void create_semaphores() {
            image_available_.resize(MAX_FRAMES_IN_FLIGHT);
            render_finished_.resize(MAX_FRAMES_IN_FLIGHT);
            frame_done_.resize(MAX_FRAMES_IN_FLIGHT);
            frame_in_flight_.resize(sc_imgs_.size(), VK_NULL_HANDLE);

            auto sema_info = VkSemaphoreCreateInfo{};
            sema_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            auto fence_info = VkFenceCreateInfo{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            for (size_t i=0; i<MAX_FRAMES_IN_FLIGHT; ++i) {
                {
                    auto res = vkCreateSemaphore(dev_.logical, &sema_info, nullptr, &image_available_[i]);
                    if (res != VK_SUCCESS) {
                        throw VulkanError("Error creating Semaphore", res);
                    }
                }
                {
                    auto res = vkCreateSemaphore(dev_.logical, &sema_info, nullptr, &render_finished_[i]);
                    if (res != VK_SUCCESS) {
                        throw VulkanError("Error creating Semaphore", res);
                    }
                }
                {
                    auto res = vkCreateFence(dev_.logical, &fence_info, nullptr, &frame_done_[i]);
                    if (res != VK_SUCCESS) {
                        throw VulkanError("Error creating Fence", res);
                    }
                }
            }
        }

        void create_render_pass() {
            auto color_attachment = VkAttachmentDescription{};
            color_attachment.format = swapchain_settings_.format;
            color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            auto color_attachment_ref = VkAttachmentReference{};
            color_attachment_ref.attachment = 0;
            color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            auto subpass = VkSubpassDescription{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &color_attachment_ref;

            auto subpass_dep = VkSubpassDependency{};
            subpass_dep.srcSubpass = VK_SUBPASS_EXTERNAL;
            subpass_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            subpass_dep.srcAccessMask = 0;
            subpass_dep.dstSubpass = 0;
            subpass_dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            subpass_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            auto renderpass_info = VkRenderPassCreateInfo{};
            renderpass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderpass_info.attachmentCount = 1;
            renderpass_info.pAttachments = &color_attachment;
            renderpass_info.subpassCount = 1;
            renderpass_info.pSubpasses = &subpass;
            renderpass_info.dependencyCount = 1;
            renderpass_info.pDependencies = &subpass_dep;

            {
                auto res = vkCreateRenderPass(dev_.logical, &renderpass_info, nullptr, &render_pass_);
                if (res != VK_SUCCESS) {
                    throw VulkanError("Error creating RenderPass", res);
                }
            }
        }

        void create_descriptor_set_layout() {
            auto vb_binding = VkDescriptorSetLayoutBinding{};
            vb_binding.binding = 0;
            vb_binding.descriptorCount = 1;
            vb_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            vb_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

            auto sampler_binding = VkDescriptorSetLayoutBinding{};
            sampler_binding.binding = 1;
            sampler_binding.descriptorCount = 1;
            sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            sampler_binding.pImmutableSamplers = nullptr;

            auto bindings = std::array<VkDescriptorSetLayoutBinding,2>{vb_binding, sampler_binding};

            auto dsl_info = VkDescriptorSetLayoutCreateInfo{};
            dsl_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            dsl_info.bindingCount = static_cast<uint32_t>(bindings.size());
            dsl_info.pBindings = bindings.data();

            {
                auto res = vkCreateDescriptorSetLayout(dev_.logical, &dsl_info, nullptr, &desc_set_layout_);
                if (res != VK_SUCCESS) {
                    throw VulkanError("Error creating DescriptorSetLayout", res);
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
        VulkanDevice dev_;
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
        std::vector<VkFramebuffer> sc_framebuffers_;
        VkRenderPass render_pass_;
        VkDescriptorSetLayout desc_set_layout_;
        VkPipelineLayout pl_layout_;
        VkPipeline pipeline_;
        VkCommandPool command_pool_;
        VkDescriptorPool desc_pool_;
        std::vector<VkCommandBuffer> command_buffers_;
        std::vector<VkDescriptorSet> desc_sets_;

        VkBuffer vert_buffer_;
        VkDeviceMemory vert_mem_;
        VkBuffer idx_buffer_;
        VkDeviceMemory idx_mem_;
        std::vector<VkBuffer> uniform_buffers_;
        std::vector<VkDeviceMemory> uniform_mems_;
        VkImage tex_image_;
        VkImageView tex_image_view_;
        VkDeviceMemory tex_mem_;

        VkSampler tex_sampler_;

        std::vector<VkSemaphore> image_available_;
        std::vector<VkSemaphore> render_finished_;
        std::vector<VkFence> frame_done_;
        std::vector<VkFence> frame_in_flight_;
        uint8_t curr_frame_ = 0;
        bool window_resized_ = false;
};
